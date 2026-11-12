#pragma once

#include <memory>
#include <string>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "ui/common/progress_bar.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"

namespace goonies::ui {

class DownloadCell : public brls::Box {
public:
    DownloadCell() {
        setFocusable(true);
        setAxis(brls::Axis::COLUMN);
        setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        setPadding(12, 20, 12, 20);
        setHeight(72);
        setWidthPercentage(100.0f);
        setMarginBottom(10);
        setCornerRadius(14.0f);

        // Top Row: Title on Left, Status Pill on Right
        auto* topRow = new brls::Box(brls::Axis::ROW);
        topRow->setWidthPercentage(100.0f);
        topRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        topRow->setAlignItems(brls::AlignItems::CENTER);

        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setFontSize(19);
        title_->setTextColor(nvgRGB(245, 245, 250));
        title_->setGrow(1.0f);
        title_->setMarginRight(15);
        topRow->addView(title_);

        // Status Pill Box
        statusPill_ = new brls::Box(brls::Axis::ROW);
        statusPill_->setAlignItems(brls::AlignItems::CENTER);
        statusPill_->setJustifyContent(brls::JustifyContent::CENTER);
        statusPill_->setPadding(4, 12, 4, 12);
        statusPill_->setCornerRadius(8.0f);
        statusPill_->setBackgroundColor(nvgRGBA(0, 210, 247, 40));

        status_ = new brls::Label();
        status_->setSingleLine(true);
        status_->setFontSize(13);
        status_->setTextColor(nvgRGB(0, 210, 247));
        statusPill_->addView(status_);
        topRow->addView(statusPill_);

        addView(topRow);

        // Bottom Row: Meta details + mini progress percentage
        auto* bottomRow = new brls::Box(brls::Axis::ROW);
        bottomRow->setWidthPercentage(100.0f);
        bottomRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        bottomRow->setAlignItems(brls::AlignItems::CENTER);

        meta_ = new brls::Label();
        meta_->setSingleLine(true);
        meta_->setFontSize(14);
        meta_->setTextColor(nvgRGB(165, 170, 185));
        bottomRow->addView(meta_);

        percentLabel_ = new brls::Label();
        percentLabel_->setSingleLine(true);
        percentLabel_->setFontSize(14);
        percentLabel_->setTextColor(nvgRGB(0, 210, 247));
        bottomRow->addView(percentLabel_);

        addView(bottomRow);

        // Integrated Mini Progress Track
        miniProgressBar_ = new ProgressBar();
        miniProgressBar_->setHeight(4);
        miniProgressBar_->setCornerRadius(2.0f);
        miniProgressBar_->setProgress(0.0f);
        miniProgressBar_->setMarginTop(3);
        addView(miniProgressBar_);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 14.0f);
        if (isFocused()) {
            nvgFillColor(vg, nvgRGBA(36, 42, 58, 255));
            nvgFill(vg);

            // Modern glowing focus stroke
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, 14.0f);
            nvgStrokeColor(vg, nvgRGBA(0, 210, 247, 220));
            nvgStrokeWidth(vg, 2.0f);
            nvgStroke(vg);
        } else {
            nvgFillColor(vg, nvgRGBA(24, 27, 36, 235));
            nvgFill(vg);

            // Subtle border
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, 14.0f);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 12));
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        }

        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

    void setTask(const DownloadTask& task, GameMetadataService* service) {
        title_->setText(task.name);
        std::string stText = taskStatusText(task);
        status_->setText(stText);

        NVGcolor col = statusColor(task);
        status_->setTextColor(col);
        statusPill_->setBackgroundColor(nvgRGBA(col.r * 255, col.g * 255, col.b * 255, 45));

        bool installing = task.status == DownloadStatus::Installing || task.status == DownloadStatus::Committing;
        float prog = installing ? installProgressOf(task) : progressOf(task);
        if (prog < 0.0f) prog = 0.0f;
        if (prog > 1.0f) prog = 1.0f;
        miniProgressBar_->setProgress(prog);

        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%.1f%%", prog * 100.0f);
        percentLabel_->setText(pctBuf);
        percentLabel_->setTextColor(col);

        std::string meta = formatBytes(task.completedBytes) + " / " + formatBytes(task.totalBytes);
        if (installing) {
            meta = "Instalando: Paquete " + std::to_string(task.packagesInstalled + 1) + " de " + std::to_string(task.packageCount);
        } else if (task.status == DownloadStatus::Installed) {
            meta = "Instalado con éxito · " + std::to_string(task.packagesInstalled) + " paquete(s)";
            percentLabel_->setText("100%");
        } else if (task.status == DownloadStatus::Completed) {
            meta = "Descarga completada · " + formatBytes(task.totalBytes);
            percentLabel_->setText("100%");
        } else if (task.status == DownloadStatus::Downloading) {
            meta += "  ·  " + formatSpeed(task.speedBytesPerSecond);
            if (task.totalBytes > task.completedBytes && task.speedBytesPerSecond > 0) {
                meta += "  ·  ETA: " + formatEta(task.totalBytes - task.completedBytes, task.speedBytesPerSecond);
            }
        } else if (task.status == DownloadStatus::Queued) {
            meta = task.slowSkipped
                ? t("Pospuesta: descarga lenta (pulsa X para priorizar)", "Postponed: slow (press X to prioritize)", "Posposta: download lento (pressione X)")
                : t("En espera en la cola... (X: Descargar a continuación)", "Waiting in queue... (X: Download next)", "Aguardando na fila... (X: Baixar a seguir)");
        } else if (task.status == DownloadStatus::Paused) {
            meta = "Descarga pausada";
        }
        meta_->setText(meta);
    }

private:
    brls::Label* title_;
    brls::Box* statusPill_;
    brls::Label* status_;
    brls::Label* meta_;
    brls::Label* percentLabel_;
    ProgressBar* miniProgressBar_;
};

}  // namespace goonies::ui
