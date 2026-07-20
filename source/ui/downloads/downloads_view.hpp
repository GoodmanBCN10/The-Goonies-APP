#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/app_settings.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "ui/common/message_cells.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/downloads/details_activity.hpp"
#include "ui/downloads/download_cell.hpp"
#include "ui/downloads/file_picker.hpp"

namespace pipensx::ui {

class MainView : public brls::Box {
public:
    MainView(DownloadManager* manager, GameMetadataService* metadata,
             AppSettings* settings)
        : brls::Box(brls::Axis::COLUMN), manager_(manager), metadata_(metadata),
          settings_(settings) {
        this->setAlignItems(brls::AlignItems::STRETCH);

        brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
        centerBox->setAlignItems(brls::AlignItems::STRETCH);
        centerBox->setPadding(40, 80, 40, 80);
        centerBox->setGrow(1.0f);

        brls::Label* titleLabel = new brls::Label();
        titleLabel->setText("Cola de Descargas");
        titleLabel->setFontSize(45);
        titleLabel->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
        titleLabel->setMarginBottom(20);
        centerBox->addView(titleLabel);

        scrollFrame_ = new brls::ScrollingFrame();
        scrollFrame_->setGrow(1);
        
        list_ = new brls::Box(brls::Axis::COLUMN);
        list_->setPadding(6, 32, 6, 32);
        scrollFrame_->setContentView(list_);
        
        centerBox->addView(scrollFrame_);
        
        addView(centerBox);
        
        refresh();
        timer_.setCallback([this] {
            refresh();
            if (fastRefresh_) {
                fastRefresh_ = false;
                timer_.setPeriod(750);
            }
        });
        registerAction("Import torrent", brls::BUTTON_X, [this](brls::View*) {
            openFilePicker();
            return true;
        });
        registerAction("Volver", brls::BUTTON_B, [](brls::View* view) {
            brls::Application::popActivity();
            return true;
        });
        startRefreshing();
    }

    ~MainView() override {
        timer_.stop();
    }

    void openDetails(const std::string& taskId) {
        brls::Application::pushActivity(
            new DetailsActivity(taskId, manager_));
    }

    void openFilePicker() {
        brls::Application::pushActivity(new FilePickerActivity(manager_));
    }

    void openRowMenu(const std::string& taskId) {
        DownloadTask task;
        bool found = false;
        for (const auto& candidate : manager_->snapshot())
            if (candidate.id == taskId) {
                task = candidate;
                found = true;
                break;
            }
        if (!found)
            return;

        std::vector<std::string> labels;
        auto runners =
            std::make_shared<std::vector<std::function<void()>>>();
        auto add = [&](const std::string& label, std::function<void()> run) {
            labels.push_back(label);
            runners->push_back(std::move(run));
        };

        add("Details", [this, taskId] { openDetails(taskId); });

        bool active = task.status == DownloadStatus::Queued ||
                      task.status == DownloadStatus::Checking ||
                      task.status == DownloadStatus::Downloading ||
                      task.status == DownloadStatus::Installing ||
                      task.status == DownloadStatus::Committing ||
                      task.status == DownloadStatus::Verifying;
        if (active)
            add("Pause", [this, taskId] {
                manager_->pause(taskId);
                startRefreshing(true);
            });
        if (task.status == DownloadStatus::Paused ||
            task.status == DownloadStatus::Error)
            add("Resume", [this, taskId] {
                manager_->resume(taskId);
                startRefreshing(true);
            });
        if (task.status == DownloadStatus::Completed)
            add("Verify", [this, taskId] {
                manager_->verify(taskId);
                startRefreshing(true);
            });
        add("Remove", [this, taskId] { openRemoveDialog(taskId); });

        auto* dropdown = new brls::Dropdown(
            task.name, labels, [runners](int selected) {
                if (selected < 0 ||
                    selected >= static_cast<int>(runners->size()))
                    return;
                auto run = (*runners)[selected];
                brls::sync([run] { run(); });
            });
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    void openRemoveDialog(const std::string& taskId) {
        auto* dialog =
            new brls::Dialog("Remove this download from The Goonies Installer?");
        dialog->addButton("Keep downloaded data", [this, taskId] {
            std::string error;
            if (!manager_->remove(taskId, false, error))
                brls::Application::notify(error);
            else
                startRefreshing(true);
        });
        dialog->addButton("Delete downloaded data", [this, taskId] {
            std::string error;
            if (!manager_->remove(taskId, true, error))
                brls::Application::notify(error);
            else
                startRefreshing(true);
        });
        dialog->addButton("Cancel", [] {});
        dialog->open();
    }

    void startRefreshing(bool fast = false) {
        timer_.stop();
        fastRefresh_ = fast;
        timer_.start(fast ? 100 : 750);
    }

    void stopRefreshing() {
        timer_.stop();
    }

    GameMetadataService* metadataService() const { return metadata_; }

private:
    bool containsFocus(brls::View* focused) const {
        for (brls::View* view = focused; view; view = view->getParent())
            if (view == this)
                return true;
        return false;
    }

    EmptyStateView* ensureEmptyState() {
        if (emptyState_)
            return emptyState_;
        emptyState_ = new EmptyStateView();
        emptyState_->setContent(
            "Downloads are empty",
            "Import a .torrent file to start a download or stream install.",
            "Import .torrent", [this] { openFilePicker(); });
        list_->addView(emptyState_);
        return emptyState_;
    }

    void refresh() {
        auto next = manager_->snapshot();
        if (settings_ && !settings_->get().showCompletedDownloads) {
            next.erase(std::remove_if(next.begin(), next.end(),
                [](const DownloadTask& task) {
                    return task.status == DownloadStatus::Completed ||
                           task.status == DownloadStatus::Installed;
                }), next.end());
        }
        uint64_t settingsGeneration = settings_ ? settings_->generation() : 0;
        bool settingsChanged = settingsGeneration != settingsGeneration_;
        bool structureChanged = !initialized_ || settingsChanged ||
                                next.size() != tasks_.size();
        bool changed = structureChanged;
        if (!structureChanged) {
            for (size_t i = 0; i < next.size(); ++i) {
                if (next[i].id != tasks_[i].id ||
                    next[i].status != tasks_[i].status) {
                    structureChanged = true;
                    changed = true;
                    break;
                }
                if (next[i].completedBytes != tasks_[i].completedBytes ||
                    next[i].speedBytesPerSecond !=
                        tasks_[i].speedBytesPerSecond ||
                    next[i].peers != tasks_[i].peers) {
                    changed = true;
                    break;
                }
                if (next[i].packagesInstalled !=
                        tasks_[i].packagesInstalled ||
                    next[i].installedBytes != tasks_[i].installedBytes ||
                    next[i].currentPackage != tasks_[i].currentPackage) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed)
            return;
            
        float offset = scrollFrame_->getContentOffsetY();
        brls::View* focused = brls::Application::getCurrentFocus();
        bool ownsFocus = containsFocus(focused);
        std::string focusedTaskId = "";
        
        // Find which task is focused by looking at the index
        if (ownsFocus && focused) {
            for (size_t i = 0; i < list_->getChildren().size(); ++i) {
                if (list_->getChildren()[i] == focused) {
                    if (i < tasks_.size()) {
                        focusedTaskId = tasks_[i].id;
                    }
                    break;
                }
            }
        }

        if (structureChanged) {
            list_->clearViews();
            emptyState_ = nullptr;
            
            if (next.empty()) {
                ensureEmptyState()->setVisibility(brls::Visibility::VISIBLE);
            } else {
                for (const auto& task : next) {
                    auto* cell = new DownloadCell();
                    cell->setTask(task, metadata_);
                    std::string taskId = task.id;
                    cell->registerClickAction([this, taskId](brls::View*) {
                        openRowMenu(taskId);
                        return true;
                    });
                    list_->addView(cell);
                    
                    if (task.id == focusedTaskId && ownsFocus) {
                        brls::Application::giveFocus(cell);
                    }
                }
            }
        } else {
            // Update in place
            if (list_->getChildren().size() == next.size()) {
                for (size_t i = 0; i < next.size(); ++i) {
                    auto* view = list_->getChildren()[i];
                    if (auto* cell = dynamic_cast<DownloadCell*>(view)) {
                        cell->setTask(next[i], metadata_);
                    }
                }
            }
        }

        tasks_ = std::move(next);
        settingsGeneration_ = settingsGeneration;
        initialized_ = true;

        if (ownsFocus) {
            // Re-focus if needed
        }
    }

    DownloadManager* manager_;
    GameMetadataService* metadata_;
    AppSettings* settings_;

    brls::ScrollingFrame* scrollFrame_;
    brls::Box* list_;
    EmptyStateView* emptyState_ = nullptr;
    
    std::vector<DownloadTask> tasks_;
    uint64_t settingsGeneration_ = 0;
    bool initialized_ = false;

    brls::RepeatingTimer timer_;
    bool fastRefresh_ = false;
};

} // namespace pipensx::ui
