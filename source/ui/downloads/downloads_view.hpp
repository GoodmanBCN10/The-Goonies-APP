#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>
#include <borealis/views/bottom_bar.hpp>
#include "app/app_settings.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "ui/common/message_cells.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/downloads/details_activity.hpp"
#include "ui/downloads/download_cell.hpp"
#include "ui/downloads/file_picker.hpp"
#include "ui/common/progress_bar.hpp"

namespace pipensx::ui {

class PanelBox : public brls::Box {
public:
    PanelBox() : brls::Box(brls::Axis::COLUMN) {}

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 15.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 25)); // Semi-transparent white
        nvgFill(vg);
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }
};

class MainView : public brls::Box {
public:
    MainView(DownloadManager* manager, GameMetadataService* metadata, AppSettings* settings)
        : brls::Box(brls::Axis::COLUMN), manager_(manager), metadata_(metadata), settings_(settings) {
        this->setAlignItems(brls::AlignItems::STRETCH);

        brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
        centerBox->setAlignItems(brls::AlignItems::STRETCH);
        centerBox->setPadding(40, 80, 40, 80);
        centerBox->setGrow(1.0f);

        brls::Label* titleLabel = new brls::Label();
        titleLabel->setText("Cola de Descargas");
        titleLabel->setFontSize(45);
        titleLabel->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
        titleLabel->setMarginBottom(10);
        centerBox->addView(titleLabel);

        summary_ = new brls::Label();
        summary_->setFontSize(15);
        summary_->setMarginBottom(20);
        summary_->setTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
        summary_->setVisibility(brls::Visibility::GONE);
        centerBox->addView(summary_);

        activeInstallBox_ = new PanelBox();
        activeInstallBox_->setPadding(20, 20, 20, 20);
        activeInstallBox_->setMarginBottom(20);
        
        brls::Box* activeTopRow = new brls::Box(brls::Axis::ROW);
        activeTopRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        activeTopRow->setMarginBottom(10);
        
        activeFilenameLabel_ = new brls::Label();
        activeFilenameLabel_->setText("No hay descargas activas");
        activeFilenameLabel_->setFontSize(22);
        activeFilenameLabel_->setTextColor(nvgRGB(255, 255, 255));
        activeTopRow->addView(activeFilenameLabel_);
        
        speedLabel_ = new brls::Label();
        speedLabel_->setText("");
        speedLabel_->setFontSize(22);
        speedLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
        activeTopRow->addView(speedLabel_);
        
        activeInstallBox_->addView(activeTopRow);
        
        progressBar_ = new ProgressBar();
        progressBar_->setHeight(15);
        progressBar_->setProgress(0.0f);
        progressBar_->setMarginBottom(10);
        activeInstallBox_->addView(progressBar_);
        
        progressTextLabel_ = new brls::Label();
        progressTextLabel_->setText("");
        progressTextLabel_->setFontSize(18);
        progressTextLabel_->setTextColor(nvgRGB(200, 200, 200));
        activeInstallBox_->addView(progressTextLabel_);
        
        centerBox->addView(activeInstallBox_);

        historyBox_ = new PanelBox();
        historyBox_->setPadding(20, 20, 20, 20);
        historyBox_->setGrow(1.0f);
        
        historyTitleLabel_ = new brls::Label();
        historyTitleLabel_->setText("En cola");
        historyTitleLabel_->setFontSize(22);
        historyTitleLabel_->setTextColor(nvgRGB(255, 255, 255));
        historyTitleLabel_->setMarginBottom(20);
        historyBox_->addView(historyTitleLabel_);
        
        scrollFrame_ = new brls::ScrollingFrame();
        scrollFrame_->setWidthPercentage(100.0f);
        scrollFrame_->setGrow(1.0f);
        
        list_ = new brls::Box(brls::Axis::COLUMN);
        list_->setWidthPercentage(100.0f);
        
        scrollFrame_->setContentView(list_);
        historyBox_->addView(scrollFrame_);
        
        centerBox->addView(historyBox_);
        
        addView(centerBox);
        addView(new brls::BottomBar());
        
        refresh();
        timer_.setCallback([this] {
            refresh();
            if (fastRefresh_) {
                fastRefresh_ = false;
                timer_.setPeriod(750);
            }
        });
        registerAction("Import torrent", brls::BUTTON_X, [this](brls::View*) { openFilePicker(); return true; });
        registerAction("Volver", brls::BUTTON_B, [](brls::View* view) { brls::Application::popActivity(); return true; });
        startRefreshing();
        registerAction("Pausar/Reanudar", brls::BUTTON_Y, [this](brls::View*) { pauseResumeAll(); return true; });
        registerAction("Limpiar finalizadas", brls::BUTTON_LB, [this](brls::View*) { clearCompleted(); return true; });
    }

    ~MainView() override {
        timer_.stop();
    }

    void openDetails(const std::string& taskId) {
        brls::Application::pushActivity(new DetailsActivity(taskId, manager_));
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
        if (!found) return;

        std::vector<std::string> labels;
        auto runners = std::make_shared<std::vector<std::function<void()>>>();
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
        if (active) add("Pause", [this, taskId] { manager_->pause(taskId); startRefreshing(true); });
        if (task.status == DownloadStatus::Paused || task.status == DownloadStatus::Error)
            add("Resume", [this, taskId] { manager_->resume(taskId); startRefreshing(true); });
        if (task.status == DownloadStatus::Completed)
            add("Verify", [this, taskId] { manager_->verify(taskId); startRefreshing(true); });
        
        if (task.status == DownloadStatus::Queued) {
            std::vector<std::string> queuedIds;
            for (const auto& candidate : manager_->snapshot())
                if (candidate.status == DownloadStatus::Queued) queuedIds.push_back(candidate.id);
            size_t pos = 0;
            bool is_queued = false;
            for (size_t i = 0; i < queuedIds.size(); ++i) {
                if (queuedIds[i] == taskId) { pos = i; is_queued = true; break; }
            }
            if (is_queued) {
                if (pos > 0) {
                    add("Subir en la cola", [this, taskId] { std::string error; manager_->moveTask(taskId, true, error); startRefreshing(true); });
                    add("Mover al principio", [this, taskId] { std::string error; manager_->moveToFront(taskId, error); startRefreshing(true); });
                }
                if (pos + 1 < queuedIds.size()) {
                    add("Bajar en la cola", [this, taskId] { std::string error; manager_->moveTask(taskId, false, error); startRefreshing(true); });
                }
            }
        }
        add("Remove", [this, taskId] { openRemoveDialog(taskId); });

        auto* dropdown = new brls::Dropdown(
            task.name, labels, [runners](int selected) {
                if (selected < 0 || selected >= static_cast<int>(runners->size())) return;
                auto run = (*runners)[selected];
                brls::sync([run] { run(); });
            });
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    void openRemoveDialog(const std::string& taskId) {
        auto* dialog = new brls::Dialog("Remove this download from The Goonies Installer?");
        dialog->addButton("Keep downloaded data", [this, taskId] {
            std::string error;
            if (!manager_->remove(taskId, false, error)) brls::Application::notify(error);
            else startRefreshing(true);
        });
        dialog->addButton("Delete downloaded data", [this, taskId] {
            std::string error;
            if (!manager_->remove(taskId, true, error)) brls::Application::notify(error);
            else startRefreshing(true);
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
    bool isPausable(DownloadStatus status) const { return status == DownloadStatus::Queued || status == DownloadStatus::Checking || status == DownloadStatus::Downloading || status == DownloadStatus::Installing || status == DownloadStatus::Committing || status == DownloadStatus::Verifying; }
    bool hasPausableTask(const std::vector<DownloadTask>& tasks) const { for (const auto& t : tasks) if (isPausable(t.status)) return true; return false; }
    void pauseAll() { for (const auto& t : manager_->snapshot()) if (isPausable(t.status)) manager_->pause(t.id); startRefreshing(true); }
    void resumeAll() { for (const auto& t : manager_->snapshot()) if (t.status == DownloadStatus::Paused || t.status == DownloadStatus::Error) manager_->resume(t.id); startRefreshing(true); }
    void pauseResumeAll() { if (hasPausableTask(manager_->snapshot())) pauseAll(); else resumeAll(); }
    void clearCompleted() { std::vector<std::string> ids; for (const auto& t : manager_->snapshot()) if (t.status == DownloadStatus::Completed || t.status == DownloadStatus::Installed) ids.push_back(t.id); if (ids.empty()) { brls::Application::notify("No hay descargas finalizadas para limpiar."); return; } for (const auto& id : ids) { std::string err; manager_->remove(id, true, err); } startRefreshing(true); }
    std::string summaryText(const std::vector<DownloadTask>& tasks) const { if (tasks.empty()) return ""; const pipensx::QueueSummary s = pipensx::summarizeQueue(tasks, now_ms()); std::string t = "Descargando: " + std::to_string(s.downloading) + " | En cola: " + std::to_string(s.queued) + " | Instalando: " + std::to_string(s.installing); if (s.paused) t += " | Pausadas: " + std::to_string(s.paused); if (s.errors) t += " | Errores: " + std::to_string(s.errors); const uint64_t speed = s.downloadSpeedBps + s.installSpeedBps; if (speed) t += " | Velocidad: " + formatSpeed(speed); if (s.etaSeconds) t += " | ETA: " + formatEta(s.totalRemainingBytes, speed); return t; }
    bool containsFocus(brls::View* focused) const {
        for (brls::View* view = focused; view; view = view->getParent())
            if (view == this) return true;
        return false;
    }

    EmptyStateView* ensureEmptyState() {
        if (emptyState_) return emptyState_;
        emptyState_ = new EmptyStateView();
        emptyState_->setContent(
            "Downloads are empty",
            "Import a .torrent file to start a download or stream install.",
            "Import .torrent", [this] { openFilePicker(); });
        
        // Mover hacia arriba en la pantalla de descargas
        emptyState_->setJustifyContent(brls::JustifyContent::FLEX_START);
        emptyState_->setPaddingTop(20);

        list_->addView(emptyState_);
        return emptyState_;
    }

    void refresh() {
        if (!initialized_) {
        } else {
            auto stack = brls::Application::getActivitiesStack();
            if (!stack.empty() && stack.back()->getContentView() != this) return;
        }

        std::vector<DownloadTask> next = manager_->snapshot();
        if (settings_ && !settings_->get().showCompletedDownloads) {
            next.erase(std::remove_if(next.begin(), next.end(),
                [](const DownloadTask& task) {
                    return task.status == DownloadStatus::Completed || task.status == DownloadStatus::Installed;
                }), next.end());
        }
        uint64_t settingsGeneration = settings_ ? settings_->generation() : 0;
        bool settingsChanged = settingsGeneration != settingsGeneration_;
        bool structureChanged = !initialized_ || settingsChanged || next.size() != tasks_.size();
        bool changed = structureChanged;
        if (!structureChanged) {
            for (size_t i = 0; i < next.size(); ++i) {
                if (next[i].id != tasks_[i].id || next[i].status != tasks_[i].status) { structureChanged = true; changed = true; break; }
                if (next[i].completedBytes != tasks_[i].completedBytes || next[i].speedBytesPerSecond != tasks_[i].speedBytesPerSecond || next[i].peers != tasks_[i].peers) { changed = true; break; }
                if (next[i].packagesInstalled != tasks_[i].packagesInstalled || next[i].installedBytes != tasks_[i].installedBytes || next[i].currentPackage != tasks_[i].currentPackage) { changed = true; break; }
            }
        }
        std::string st = summaryText(next);
        if (!st.empty()) { summary_->setText(st); summary_->setVisibility(brls::Visibility::VISIBLE); }
        else summary_->setVisibility(brls::Visibility::GONE);
        updateActionHint(brls::BUTTON_Y, hasPausableTask(next) ? "Pausar todo" : "Reanudar todo");

        // Update Active Box
        const DownloadTask* activeTask = nullptr;
        for (const auto& task : next) {
            if (task.status == DownloadStatus::Downloading || task.status == DownloadStatus::Installing || task.status == DownloadStatus::Committing || task.status == DownloadStatus::Checking) {
                activeTask = &task;
                break;
            }
        }
        if (activeTask) {
            activeFilenameLabel_->setText(activeTask->name);
            if (activeTask->status == DownloadStatus::Downloading) {
                speedLabel_->setText(formatSpeed(activeTask->speedBytesPerSecond));
            } else {
                speedLabel_->setText("");
            }
            float progress = (activeTask->status == DownloadStatus::Installing || activeTask->status == DownloadStatus::Committing) ? installProgressOf(*activeTask) : progressOf(*activeTask);
            progressBar_->setProgress(progress);

            std::string meta = formatBytes(activeTask->completedBytes) + " / " + formatBytes(activeTask->totalBytes);
            if (activeTask->status == DownloadStatus::Installing || activeTask->status == DownloadStatus::Committing) {
                meta = "Package " + std::to_string(activeTask->packagesInstalled + 1) + " / " + std::to_string(activeTask->packageCount);
            }
            meta += " (" + std::to_string(int(progress * 100)) + "%)";
            progressTextLabel_->setText(meta);
        } else {
            activeFilenameLabel_->setText("No hay descargas activas");
            speedLabel_->setText("");
            progressBar_->setProgress(0.0f);
            progressTextLabel_->setText("");
        }

        historyTitleLabel_->setText("En cola (" + std::to_string(next.size()) + ")");

        if (!changed) return;
            
        float offset = scrollFrame_->getContentOffsetY();
        brls::View* focused = brls::Application::getCurrentFocus();
        bool ownsFocus = containsFocus(focused);
        std::string focusedTaskId = "";
        
        if (ownsFocus && focused) {
            for (size_t i = 0; i < list_->getChildren().size(); ++i) {
                brls::View* child = list_->getChildren()[i];
                bool childOwnsFocus = false;
                for (brls::View* v = focused; v; v = v->getParent()) {
                    if (v == child) { childOwnsFocus = true; break; }
                }
                if (childOwnsFocus) {
                    if (i < tasks_.size()) focusedTaskId = tasks_[i].id;
                    break;
                }
            }
        }

        if (structureChanged) {
            if (ownsFocus) {
                this->setFocusable(true);
                brls::Application::giveFocus(this);
            }
            list_->clearViews();
            emptyState_ = nullptr;
            
            if (next.empty()) {
                ensureEmptyState()->setVisibility(brls::Visibility::VISIBLE);
                if (ownsFocus) brls::Application::giveFocus(emptyState_);
            } else {
                bool focusRestored = false;
                for (const auto& task : next) {
                    auto* cell = new DownloadCell();
                    cell->setTask(task, metadata_);
                    std::string taskId = task.id;
                    cell->registerClickAction([this, taskId](brls::View*) {
                        openRowMenu(taskId);
                        return true;
                    });
                    list_->addView(cell);
                    
                    if (ownsFocus && !focusRestored && (task.id == focusedTaskId || focusedTaskId.empty())) {
                        brls::Application::giveFocus(cell);
                        focusRestored = true;
                    }
                }
                
                if (ownsFocus && !focusRestored && !list_->getChildren().empty()) {
                    brls::Application::giveFocus(list_->getChildren()[0]);
                }
            }
            if (ownsFocus) {
                this->setFocusable(false);
            }
        } else {
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
    }

    DownloadManager* manager_;
    GameMetadataService* metadata_;
    AppSettings* settings_;

    brls::Label* summary_ = nullptr;

    PanelBox* activeInstallBox_ = nullptr;
    brls::Label* activeFilenameLabel_ = nullptr;
    brls::Label* speedLabel_ = nullptr;
    ProgressBar* progressBar_ = nullptr;
    brls::Label* progressTextLabel_ = nullptr;

    PanelBox* historyBox_ = nullptr;
    brls::Label* historyTitleLabel_ = nullptr;

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
