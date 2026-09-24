#pragma once

#include <memory>
#include <string>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "ui/common/progress_bar.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"

namespace pipensx::ui {

class DownloadCell : public brls::Box {
public:
    DownloadCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(0, 20, 0, 20);
        setHeight(40);
        setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        setMarginBottom(5);

        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setFontSize(18);
        title_->setTextColor(nvgRGB(255, 255, 255));
        
        auto* right = new brls::Box(brls::Axis::ROW);
        right->setJustifyContent(brls::JustifyContent::FLEX_END);
        right->setAlignItems(brls::AlignItems::CENTER);

        meta_ = new brls::Label();
        meta_->setSingleLine(true);
        meta_->setFontSize(18);
        meta_->setTextColor(nvgRGB(200, 200, 200));
        meta_->setMarginRight(30);

        status_ = new brls::Label();
        status_->setSingleLine(true);
        status_->setFontSize(18);

        right->addView(meta_);
        right->addView(status_);

        addView(title_);
        addView(right);
    }

    void setTask(const DownloadTask& task, GameMetadataService* service) {
        title_->setText(task.name);
        status_->setText(taskStatusText(task));
        status_->setTextColor(statusColor(task.status));

        std::string meta = formatBytes(task.completedBytes) + " / " + formatBytes(task.totalBytes);
        if (task.status == DownloadStatus::Installing || task.status == DownloadStatus::Committing) {
            meta = "Pkg " + std::to_string(task.packagesInstalled + 1) + "/" + std::to_string(task.packageCount);
        } else if (task.status == DownloadStatus::Installed) {
            meta = std::to_string(task.packagesInstalled) + " pkg(s)";
        } else if (task.status == DownloadStatus::Downloading) {
            meta += "   " + formatSpeed(task.speedBytesPerSecond);
        }
        meta_->setText(meta);
    }

private:
    brls::Label* title_;
    brls::Label* status_;
    brls::Label* meta_;
};

}  // namespace pipensx::ui
