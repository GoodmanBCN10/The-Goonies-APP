#pragma once

#include <borealis.hpp>
#include "app/simple_download_manager.hpp"

namespace goonies::ui {

class DownloadCell : public brls::Box {
public:
    DownloadCell(std::shared_ptr<goonies::DLTask> task, goonies::SimpleDownloadManager* manager) 
        : brls::Box(brls::Axis::ROW), task_(task), manager_(manager) 
    {
        this->setFocusable(true);
        this->setPadding(20);
        this->setMarginBottom(10);
        this->setCornerRadius(10.0f);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setBackgroundColor(brls::Application::getTheme().getColor("brls/sidebar/background"));

        auto* right = new brls::Box(brls::Axis::COLUMN);
        right->setGrow(1);
        
        auto* title = new brls::Label();
        title->setText(task->title);
        title->setFontSize(24);
        title->setMarginBottom(10);
        right->addView(title);
        
        status_ = new brls::Label();
        status_->setFontSize(16);
        right->addView(status_);

        this->addView(right);
        
        this->registerAction(manager->settings()->get().language == 2 ? "Cancel/Delete" : "Cancelar/Borrar", brls::BUTTON_X, [this, manager, task](brls::View*) {
            auto* d = new brls::Dialog((manager->settings()->get().language == 2 ? "Delete " : "¿Borrar ") + task->title + "?");
            d->addButton(manager->settings()->get().language == 2 ? "Yes" : "Si", [manager, task]() {
                manager->remove(task->id);
            });
            d->addButton(manager->settings()->get().language == 2 ? "No" : "No", []{});
            d->open();
            return true;
        });

        updateStatus();
    }
    
    void updateStatus() {
        std::string s;
        if (task_->status == Status::Queued) s = manager_->settings()->get().language == 2 ? "Queued..." : "En cola...";
        else if (task_->status == Status::Downloading) {
            if (task_->total > 0 && task_->progress >= task_->total) {
                s = "Extrayendo... (Espera, puede tardar unos minutos)";
            } else {
                s = task_->total_parts > 1 ? ((manager_->settings()->get().language == 2 ? "Downloading Part " : "Descargando Parte ") + std::to_string(task_->current_part) + (manager_->settings()->get().language == 2 ? " of " : " de ") + std::to_string(task_->total_parts) + "... ") : (manager_->settings()->get().language == 2 ? "Downloading... " : "Descargando... ");
                if (task_->total > 0) {
                    s += std::to_string(task_->progress / 1024 / 1024) + " MB / " + std::to_string(task_->total / 1024 / 1024) + " MB";
                }
            }
        }
        else if (task_->status == Status::Extracting) {
            s = task_->total_parts > 1 ? ((manager_->settings()->get().language == 2 ? "Extracting Part " : "Extrayendo Parte ") + std::to_string(task_->current_part) + (manager_->settings()->get().language == 2 ? " of " : " de ") + std::to_string(task_->total_parts) + "... ") : (manager_->settings()->get().language == 2 ? "Extracting... " : "Extrayendo... ");
            if (task_->total > 0) {
                int percentage = (task_->progress * 100) / task_->total;
                s += std::to_string(percentage) + "% (" + std::to_string(task_->progress / 1024 / 1024) + " MB / " + std::to_string(task_->total / 1024 / 1024) + " MB)";
            }
        }
        else if (task_->status == Status::Completed) s = manager_->settings()->get().language == 2 ? "Completed & Extracted." : "Completado y extraido.";
        else if (task_->status == Status::Error) s = "Error: " + task_->error;
        
        if (last_s_ != s) {
            status_->setText(s);
            last_s_ = s;
        }
    }

private:
    std::shared_ptr<goonies::DLTask> task_;
    goonies::SimpleDownloadManager* manager_;
    brls::Label* status_;
    std::string last_s_;
};

class DownloadsView : public brls::Box {
public:
    DownloadsView(goonies::SimpleDownloadManager* manager) 
        : brls::Box(brls::Axis::COLUMN), manager_(manager)
    {
        this->setGrow(1.0f);
        this->setAlignItems(brls::AlignItems::STRETCH);
        this->setPadding(40, 80, 40, 80);

        auto* titleLabel = new brls::Label();
        titleLabel->setText(manager_->settings()->get().language == 2 ? "Downloads Queue" : "Cola de Descargas");
        titleLabel->setFontSize(45);
        titleLabel->setMarginBottom(40);
        this->addView(titleLabel);

        scroll_ = new brls::ScrollingFrame();
        scroll_->setGrow(1.0f);
        list_ = new brls::Box(brls::Axis::COLUMN);
        scroll_->setContentView(list_);
        this->addView(scroll_);
        
        // MASK PARENT ACTIONS so they don't appear in the bottom bar at all!
        // By registering them with hidden=true, they consume the button without drawing it, 
        // preventing TabFrame or Sidebar from showing them.
        this->registerAction("", brls::BUTTON_Y, [](brls::View*){return true;}, true);
        this->registerAction("", brls::BUTTON_BACK, [](brls::View*){return true;}, true);
        this->registerAction("", brls::BUTTON_START, [](brls::View*){return true;}, true);
    }
    
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        // limit refresh to approx 10 times a second to save cpu
        frames_++;
        if (frames_ % 6 == 0) {
            refresh();
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

private:
    void refresh() {
        auto tasks = manager_->snapshot();
        if (tasks.size() != cells_.size()) {
            list_->clearViews();
            cells_.clear();
            for (auto& t : tasks) {
                auto* cell = new DownloadCell(t, manager_);
                list_->addView(cell);
                cells_.push_back(cell);
            }
        } else {
            for (auto* cell : cells_) {
                cell->updateStatus();
            }
        }
    }

    goonies::SimpleDownloadManager* manager_;
    brls::ScrollingFrame* scroll_;
    brls::Box* list_;
    std::vector<DownloadCell*> cells_;
    int frames_ = 0;
};

} // namespace goonies::ui
