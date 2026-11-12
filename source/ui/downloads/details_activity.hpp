#pragma once

#include <string>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "ui/common/progress_bar.hpp"
#include "ui/common/speed_graph.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"

namespace goonies::ui {

class DetailCardBox : public brls::Box {
public:
    DetailCardBox() : brls::Box(brls::Axis::COLUMN) {
        setCornerRadius(14.0f);
        setPadding(18, 24, 18, 24);
        setMarginBottom(16);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 14.0f);
        nvgFillColor(vg, nvgRGBA(22, 25, 34, 235));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 14.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 12));
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }
};

class DetailsActivity : public brls::Activity {
public:
    DetailsActivity(std::string taskId, DownloadManager* manager)
        : taskId_(std::move(taskId)), manager_(manager) {
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(20, 36, 24, 36);
        content->setAlignItems(brls::AlignItems::STRETCH);

        // 1. TOP CARD: 100% FULL-WIDTH PROGRESS COMMAND HUD
        auto* hudCard = new DetailCardBox();
        hudCard->setMarginBottom(16);
        
        auto* hudTop = new brls::Box(brls::Axis::ROW);
        hudTop->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        hudTop->setAlignItems(brls::AlignItems::CENTER);
        hudTop->setMarginBottom(12);

        titleLabel_ = new brls::Label();
        titleLabel_->setFontSize(24);
        titleLabel_->setTextColor(nvgRGB(255, 255, 255));
        titleLabel_->setSingleLine(true);
        titleLabel_->setWidth(850);
        hudTop->addView(titleLabel_);

        statusPill_ = new brls::Box(brls::Axis::ROW);
        statusPill_->setAlignItems(brls::AlignItems::CENTER);
        statusPill_->setPadding(4, 14, 4, 14);
        statusPill_->setCornerRadius(8.0f);
        statusPill_->setBackgroundColor(nvgRGBA(0, 210, 247, 40));

        status_ = new brls::Label();
        status_->setFontSize(13);
        status_->setTextColor(nvgRGB(0, 210, 247));
        statusPill_->addView(status_);
        hudTop->addView(statusPill_);

        hudCard->addView(hudTop);

        // Stats Row above progress bar: Big % and details
        auto* statsRow = new brls::Box(brls::Axis::ROW);
        statsRow->setAlignItems(brls::AlignItems::CENTER);
        statsRow->setMarginBottom(8);

        bigPercentLabel_ = new brls::Label();
        bigPercentLabel_->setFontSize(36);
        bigPercentLabel_->setTextColor(theme::neonCyan());
        bigPercentLabel_->setSingleLine(true);
        bigPercentLabel_->setAutoAnimate(false);
        bigPercentLabel_->setWidth(brls::View::AUTO);
        bigPercentLabel_->setShrink(0.0f);
        bigPercentLabel_->setMarginRight(20);
        statsRow->addView(bigPercentLabel_);

        progress_ = new brls::Label();
        progress_->setFontSize(16);
        progress_->setTextColor(nvgRGB(255, 255, 255));
        progress_->setSingleLine(true);
        progress_->setAutoAnimate(false);
        progress_->setWidth(brls::View::AUTO);
        progress_->setShrink(0.0f);
        progress_->setMarginRight(24);
        statsRow->addView(progress_);

        eta_ = new brls::Label();
        eta_->setFontSize(14);
        eta_->setTextColor(theme::textTertiary());
        eta_->setSingleLine(true);
        eta_->setAutoAnimate(false);
        eta_->setGrow(1.0f);
        statsRow->addView(eta_);

        hudCard->addView(statsRow);

        // 100% FULL-WIDTH PROGRESS BAR
        progressBar_ = new ProgressBar();
        progressBar_->setHeight(16);
        progressBar_->setCornerRadius(8.0f);
        progressBar_->setMarginBottom(12);
        hudCard->addView(progressBar_);

        // Subline: Package info
        auto* pkgRow = new brls::Box(brls::Axis::ROW);
        pkgRow->setAlignItems(brls::AlignItems::CENTER);
        pkgRow->setMarginBottom(12);

        package_ = new brls::Label();
        package_->setFontSize(13);
        package_->setTextColor(theme::neonCyan());
        package_->setMarginRight(12);
        pkgRow->addView(package_);

        currentPackage_ = new brls::Label();
        currentPackage_->setFontSize(13);
        currentPackage_->setTextColor(theme::textSecondary());
        currentPackage_->setSingleLine(true);
        currentPackage_->setAutoAnimate(false);
        pkgRow->addView(currentPackage_);

        hudCard->addView(pkgRow);

        // Action buttons inside the top HUD
        auto* actions = new brls::Box(brls::Axis::ROW);
        actions->setAlignItems(brls::AlignItems::CENTER);
        pauseButton_ = addActionButton(actions, "Pausar",
                                       &brls::BUTTONSTYLE_PRIMARY);
        verifyButton_ = addActionButton(actions, "Verificar",
                                        &brls::BUTTONSTYLE_DEFAULT);
        removeButton_ = addActionButton(actions, "Eliminar",
                                        &brls::BUTTONSTYLE_DEFAULT);
        hudCard->addView(actions);

        pauseButton_->registerClickAction([this](brls::View*) {
            onPauseResume();
            return true;
        });
        verifyButton_->registerClickAction([this](brls::View*) {
            manager_->verify(taskId_);
            refresh();
            return true;
        });
        removeButton_->registerClickAction([this](brls::View*) {
            openRemoveDialog();
            return true;
        });

        content->addView(hudCard);

        // 2. BOTTOM SECTION: TELEMETRY & NETWORK/STORAGE (UNDERNEATH)
        auto* bottomRow = new brls::Box(brls::Axis::ROW);
        bottomRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

        // Left Underneath: Speed Monitor Card
        auto* speedCard = addCard(bottomRow, t("MONITOR DE VELOCIDAD", "SPEED MONITOR", "MONITOR DE VELOCIDADE"));
        speedCard->setWidth(580);
        speedCard->setMarginRight(16);

        auto* speedLegend = new brls::Box(brls::Axis::ROW);
        speedLegend->setAlignItems(brls::AlignItems::CENTER);
        speedLegend->setMarginBottom(8);
        downloadSpeed_ = addSpeedLegend(speedLegend, theme::neonCyan(), nullptr);
        installSpeed_ = addSpeedLegend(speedLegend, theme::neonGreen(),
                                       &installSpeedItem_);
        speedCard->addView(speedLegend);
        speedGraph_ = new SpeedGraphView();
        speedCard->addView(speedGraph_);

        // Right Underneath: Network & Storage Card
        auto* networkCard = addCard(bottomRow, t("RED & ALMACENAMIENTO", "NETWORK & STORAGE", "REDE & ARMAZENAMENTO"));
        networkCard->setWidth(580);

        peers_ = addLine(networkCard, 14);
        pieces_ = addLine(networkCard, 14);

        path_ = addLine(networkCard, 13);
        path_->setTextColor(theme::textTertiary());
        path_->setMarginTop(8);

        error_ = addLine(networkCard, 13);
        error_->setTextColor(theme::error());

        content->addView(bottomRow);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1);
        scroll->setContentView(content);
        frame_ = new brls::AppletFrame(scroll);
    }

    brls::View* createContentView() override {
        return frame_;
    }

    void onContentAvailable() override {
        refresh();
        timer_.setCallback([this] { refresh(); });
        timer_.start(500);
        brls::Application::giveFocus(pauseButton_);
    }

    ~DetailsActivity() override {
        timer_.stop();
    }

private:
    static brls::Label* addLine(brls::Box* box, float size) {
        auto* label = new brls::Label();
        label->setWidth(brls::View::AUTO);
        label->setFontSize(size);
        label->setMarginBottom(6);
        box->addView(label);
        return label;
    }

    static brls::Box* addCard(brls::Box* parent, const std::string& title) {
        auto* card = new DetailCardBox();
        auto* heading = new brls::Label();
        heading->setFontSize(13);
        heading->setTextColor(nvgRGB(0, 210, 247));
        heading->setMarginBottom(12);
        heading->setText(title);
        card->addView(heading);
        parent->addView(card);
        return card;
    }

    static brls::Label* addSpeedLegend(brls::Box* row, NVGcolor color,
                                       brls::Box** itemOut) {
        auto* item = new brls::Box(brls::Axis::ROW);
        item->setAlignItems(brls::AlignItems::CENTER);
        item->setMarginRight(28);

        auto* dot = new brls::Box();
        dot->setWidth(10);
        dot->setHeight(10);
        dot->setCornerRadius(5);
        dot->setBackgroundColor(color);
        dot->setMarginRight(8);
        item->addView(dot);

        auto* label = new brls::Label();
        label->setFontSize(theme::kFontBody);
        label->setSingleLine(true);
        label->setAutoAnimate(false);
        item->addView(label);
        row->addView(item);
        if (itemOut)
            *itemOut = item;
        return label;
    }

    static brls::Button* addActionButton(brls::Box* row, const std::string& text,
                                         const brls::ButtonStyle* style) {
        auto* button = new brls::Button();
        button->setStyle(style);
        button->setFontSize(theme::kFontSmall);
        button->setHeight(48);
        button->setCornerRadius(10.0f);
        button->setGrow(1);
        button->setMarginRight(12);
        button->setText(text);
        row->addView(button);
        return button;
    }

    const DownloadTask* currentTask() {
        cache_ = manager_->snapshot();
        for (const auto& task : cache_)
            if (task.id == taskId_)
                return &task;
        return nullptr;
    }

    void onPauseResume() {
        const DownloadTask* task = currentTask();
        if (!task)
            return;
        if (task->status == DownloadStatus::Paused ||
            task->status == DownloadStatus::Error) {
            manager_->resume(taskId_);
            refresh();
        } else {
            bool hasOtherQueued = false;
            for (const auto& t : manager_->snapshot()) {
                if (t.id != taskId_ && t.status == DownloadStatus::Queued) {
                    hasOtherQueued = true;
                    break;
                }
            }
            if (hasOtherQueued) {
                auto* dialog = new brls::Dialog(t(
                    "¿Qué deseas hacer con la cola de descargas?",
                    "What would you like to do with the download queue?",
                    "O que deseja fazer com a fila de downloads?"));
                dialog->addButton(t("Saltar a la siguiente", "Skip to next", "Pular para a próxima"), [this] {
                    std::string err;
                    manager_->skipActiveTask(err);
                    refresh();
                });
                dialog->addButton(t("Pausar descarga", "Pause download", "Pausar download"), [this] {
                    manager_->pause(taskId_);
                    refresh();
                });
                dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), [] {});
                dialog->open();
            } else {
                manager_->pause(taskId_);
                refresh();
            }
        }
    }

    void refresh() {
        const DownloadTask* task = currentTask();
        if (!task) {
            brls::Application::popActivity();
            return;
        }
        frame_->setTitle(t("Detalle de Descarga", "Download Details", "Detalhes do Download"));
        titleLabel_->setText(task->name);
        std::string st = goonies::statusName(task->status);
        if (task->status == DownloadStatus::Queued && task->slowSkipped) {
            st = t("Pospuesta (lenta)", "Postponed (slow)", "Posposta (lenta)");
        }
        status_->setText(st);

        NVGcolor col = statusColor(*task);
        status_->setTextColor(col);
        statusPill_->setBackgroundColor(nvgRGBA(col.r * 255, col.g * 255, col.b * 255, 45));

        bool installing = task->status == DownloadStatus::Installing ||
                          task->status == DownloadStatus::Committing;
        float progress = installing ? installProgressOf(*task)
                                    : progressOf(*task);
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        progressBar_->setProgress(progress);

        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%.1f%%", progress * 100.0f);
        bigPercentLabel_->setText(pctBuf);
        bigPercentLabel_->setTextColor(col);

        if (task->status == DownloadStatus::Installing && task->isPort) {
            progress_->setText(std::to_string(task->packagesInstalled) + " / " +
                               std::to_string(task->packageCount) + " " + t("archivos", "files", "arquivos"));
        } else {
            progress_->setText(formatBytes(task->completedBytes) + " / " +
                               formatBytes(task->totalBytes));
        }

        std::string eta;
        if (task->status == DownloadStatus::Downloading &&
            task->totalBytes > task->completedBytes)
            eta = formatEta(task->totalBytes - task->completedBytes,
                            task->speedBytesPerSecond);
        eta_->setText(eta.empty() ? "" : std::string(t("Tiempo estimado (ETA): ", "Estimated time (ETA): ", "Tempo estimado (ETA): ")) + eta);

        if (task->mode == TransferMode::StreamInstall && task->packageCount) {
            const bool hasCurrent = !task->currentPackage.empty() &&
                                    task->packagesInstalled < task->packageCount;
            if (hasCurrent) {
                package_->setText(
                    std::string(t("Paquete ", "Package ", "Pacote ")) + std::to_string(task->packagesInstalled + 1) +
                    t(" de ", " of ", " de ") + std::to_string(task->packageCount));
            } else {
                package_->setText(
                    std::string(t("Paquetes instalados: ", "Installed packages: ", "Pacotes instalados: ")) +
                    std::to_string(task->packagesInstalled) + t(" de ", " of ", " de ") +
                    std::to_string(task->packageCount));
            }
            currentPackage_->setText(task->currentPackage);
        } else {
            package_->setText("");
            currentPackage_->setText("");
        }

        recordSpeedSample(*task);
        downloadSpeed_->setText(
            std::string(t("Descarga: ", "Download: ", "Download: ")) + formatSpeed(task->speedBytesPerSecond));
        if (task->mode == TransferMode::StreamInstall) {
            installSpeedItem_->setVisibility(brls::Visibility::VISIBLE);
            installSpeed_->setText(
                std::string(t("Instalación: ", "Install: ", "Instalação: ")) + formatSpeed(installSpeedSmoothed_));
        } else {
            installSpeedItem_->setVisibility(brls::Visibility::GONE);
        }
        peers_->setText(std::string(t("Conexiones (Peers): ", "Peers: ", "Conexões: ")) + std::to_string(task->peers) +
                        "   DHT: " + std::to_string(task->dhtGood) + t(" activos", " active", " ativos"));
        pieces_->setText(std::string(t("Bloques verificados: ", "Verified pieces: ", "Blocos verificados: ")) + std::to_string(task->piecesDone) + " / " +
                         std::to_string(task->piecesTotal));
        path_->setText(std::string(t("Destino: ", "Target: ", "Destino: ")) + task->dataPath);
        if (task->status == DownloadStatus::Checking) {
            error_->setText(task->error);
            error_->setTextColor(theme::accent());
        } else {
            error_->setText(task->error.empty() ? "" : std::string(t("Error: ", "Error: ", "Erro: ")) + task->error);
            error_->setTextColor(theme::error());
        }

        updateButtons(*task);
    }

    void updateButtons(const DownloadTask& task) {
        bool paused = task.status == DownloadStatus::Paused ||
                      task.status == DownloadStatus::Error;
        bool active = task.status == DownloadStatus::Queued ||
                      task.status == DownloadStatus::Checking ||
                      task.status == DownloadStatus::Downloading ||
                      task.status == DownloadStatus::Installing ||
                      task.status == DownloadStatus::Committing ||
                      task.status == DownloadStatus::Verifying;
        pauseButton_->setText(paused ? t("Reanudar", "Resume", "Retomar") : t("Pausar", "Pause", "Pausar"));
        setButtonAvailable(pauseButton_, paused || active);

        bool canVerify = task.status == DownloadStatus::Paused ||
                         task.status == DownloadStatus::Error ||
                         task.status == DownloadStatus::Completed ||
                         task.status == DownloadStatus::Installed;
        setButtonAvailable(verifyButton_, canVerify);
        setButtonAvailable(removeButton_,
                           task.status != DownloadStatus::Removing);
    }

    static void setButtonAvailable(brls::Button* button, bool available) {
        button->setState(available ? brls::ButtonState::ENABLED
                                   : brls::ButtonState::DISABLED);
        button->setAlpha(available ? 1.0f : 0.32f);
    }

    static void appendSpeedSample(std::vector<uint64_t>& samples,
                                  uint64_t value) {
        constexpr size_t kMaxSpeedSamples = 60;
        if (samples.size() == kMaxSpeedSamples)
            samples.erase(samples.begin());
        samples.push_back(value);
    }

    void recordSpeedSample(const DownloadTask& task) {
        uint64_t now = now_ms();
        appendSpeedSample(downloadSpeedSamples_, task.speedBytesPerSecond);

        if (task.mode == TransferMode::StreamInstall) {
            uint64_t installSpeed = 0;
            if (hasInstallSample_ && now > lastInstallSampleMs_ &&
                task.installedBytes >= lastInstallBytes_) {
                uint64_t elapsed = now - lastInstallSampleMs_;
                installSpeed =
                    (task.installedBytes - lastInstallBytes_) * 1000 / elapsed;
            }
            lastInstallBytes_ = task.installedBytes;
            lastInstallSampleMs_ = now;
            hasInstallSample_ = true;
            installSpeedSmoothed_ = emaUpdate(installSpeedSmoothed_,
                                              installSpeed);
            appendSpeedSample(installSpeedSamples_, installSpeedSmoothed_);
        } else {
            hasInstallSample_ = false;
            lastInstallBytes_ = 0;
            lastInstallSampleMs_ = now;
            installSpeedSmoothed_ = 0;
            installSpeedSamples_.clear();
        }

        speedGraph_->setSamples(downloadSpeedSamples_, installSpeedSamples_);
    }

    void openRemoveDialog() {
        auto* dialog = new brls::Dialog(
            t("¿Deseas eliminar esta descarga de la cola?", "Delete this download from queue?", "Deseja excluir este download da fila?"));
        dialog->addButton(t("Conservar datos descargados", "Keep downloaded data", "Manter dados baixados"), [this] {
            std::string error;
            if (!manager_->remove(taskId_, false, error))
                brls::Application::notify(error);
            else
                brls::Application::popActivity();
        });
        dialog->addButton(t("Eliminar todos los datos", "Delete all data", "Excluir todos os dados"), [this] {
            std::string error;
            if (!manager_->remove(taskId_, true, error))
                brls::Application::notify(error);
            else
                brls::Application::popActivity();
        });
        dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), [] {});
        dialog->open();
    }

    std::string taskId_;
    DownloadManager* manager_;
    brls::AppletFrame* frame_;
    brls::Label* titleLabel_ = nullptr;
    brls::Box* statusPill_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::Label* bigPercentLabel_ = nullptr;
    brls::Button* pauseButton_;
    brls::Button* verifyButton_;
    brls::Button* removeButton_;
    ProgressBar* progressBar_;
    brls::Label* progress_;
    brls::Label* package_;
    brls::Label* currentPackage_;
    brls::Label* eta_;
    brls::Label* downloadSpeed_;
    brls::Label* installSpeed_;
    brls::Box* installSpeedItem_;
    SpeedGraphView* speedGraph_;
    brls::Label* peers_;
    brls::Label* pieces_;
    brls::Label* path_;
    brls::Label* error_;
    brls::RepeatingTimer timer_;
    std::vector<DownloadTask> cache_;
    std::vector<uint64_t> downloadSpeedSamples_;
    std::vector<uint64_t> installSpeedSamples_;
    uint64_t installSpeedSmoothed_ = 0;
    uint64_t lastInstallBytes_ = 0;
    uint64_t lastInstallSampleMs_ = 0;
    bool hasInstallSample_ = false;
};

}  // namespace goonies::ui
