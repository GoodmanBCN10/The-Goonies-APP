#include "ui/installed/local_install_view.hpp"
#include "ui/common/ui_helpers.hpp"
#include <fmt/core.h>
#include <borealis/views/bottom_bar.hpp>
#include <fmt/core.h>

namespace goonies::ui {

static Installer::Core* g_localInstallerCore = nullptr;

LocalInstallView::LocalInstallView(const std::vector<std::string>& queue) : brls::Box(brls::Axis::COLUMN) {
    this->setFocusable(true);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(40, 60, 40, 60);

    for (const auto& p : queue) {
        LocalHistoryItem item;
        item.path = p;
        size_t slash = p.find_last_of('/');
        item.name = (slash != std::string::npos) ? p.substr(slash + 1) : p;
        item.status = -1;
        queue_.push_back(item);
    }

    // --- Premium Title and Instructions ---
    titleLabel_ = new brls::Label();
    titleLabel_->setText(t("Instalar por Cola Local", "Local Queue Installer"));
    titleLabel_->setFontSize(36);
    titleLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
    titleLabel_->setMarginBottom(10);
    this->addView(titleLabel_);

    instructionsLabel_ = new brls::Label();
    instructionsLabel_->setText(t("Por favor, no apagues la consola ni extraigas la unidad.", "Please do not turn off the console or remove the drive."));
    instructionsLabel_->setFontSize(20);
    instructionsLabel_->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel_->setMarginBottom(30);
    this->addView(instructionsLabel_);

    // --- Active Progress Box ---
    progressContainer_ = new brls::Box(brls::Axis::COLUMN);
    progressContainer_->setWidthPercentage(100.0f);
    progressContainer_->setPadding(20, 30, 20, 30);
    progressContainer_->setCornerRadius(10);
    progressContainer_->setBackgroundColor(nvgRGBA(50, 50, 55, 200));
    progressContainer_->setMarginBottom(20);

    brls::Box* activeTopRow = new brls::Box(brls::Axis::ROW);
    activeTopRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    activeTopRow->setMarginBottom(10);

    activeFilenameLabel_ = new brls::Label();
    activeFilenameLabel_->setText(t("Iniciando...", "Starting..."));
    activeFilenameLabel_->setFontSize(22);
    activeTopRow->addView(activeFilenameLabel_);

    speedLabel_ = new brls::Label();
    speedLabel_->setText("0.0 MB/s");
    speedLabel_->setFontSize(22);
    speedLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
    activeTopRow->addView(speedLabel_);

    progressContainer_->addView(activeTopRow);

    progressBar_ = new pipensx::ui::ProgressBar();
    progressBar_->setProgress(0.0f);
    progressContainer_->addView(progressBar_);

    progressTextLabel_ = new brls::Label();
    progressTextLabel_->setText("0.00 MB / 0.00 MB (0.0%)");
    progressTextLabel_->setFontSize(18);
    progressTextLabel_->setTextColor(nvgRGB(200, 200, 200));
    progressTextLabel_->setMarginTop(8);
    progressContainer_->addView(progressTextLabel_);

    this->addView(progressContainer_);

    // --- History Panel ---
    historyBox_ = new brls::Box(brls::Axis::COLUMN);
    historyBox_->setPadding(20, 20, 20, 20);
    historyBox_->setCornerRadius(10);
    historyBox_->setBackgroundColor(nvgRGBA(50, 50, 55, 100));
    historyBox_->setGrow(1.0f); // Fill remaining space

    historyTitleLabel_ = new brls::Label();
    historyTitleLabel_->setText(fmt::format("{} ({})", t("Cola de Instalación", "Installation Queue"), queue_.size()));
    historyTitleLabel_->setFontSize(22);
    historyTitleLabel_->setTextColor(nvgRGB(255, 255, 255));
    historyTitleLabel_->setMarginBottom(20);
    historyBox_->addView(historyTitleLabel_);

    historyScroll_ = new brls::ScrollingFrame();
    historyScroll_->setWidthPercentage(100.0f);
    historyScroll_->setGrow(1.0f);

    historyList_ = new brls::Box(brls::Axis::COLUMN);
    historyList_->setWidthPercentage(100.0f);

    historyScroll_->setContentView(historyList_);
    historyBox_->addView(historyScroll_);

    this->addView(historyBox_);

    // Setup actions
    this->registerAction(t("Cancelar/Volver", "Cancel/Back"), brls::BUTTON_B, [this](brls::View* view) {
        if (g_localInstallerCore && !g_localInstallerCore->IsFinished() && !g_localInstallerCore->HasError()) {
            brls::Application::notify(t("Cancelando instalación actual...", "Canceling current installation..."));
            g_localInstallerCore->AbortInstallation();
        } else {
            brls::Application::popActivity();
        }
        return true;
    });

    if (!g_localInstallerCore) {
        g_localInstallerCore = new Installer::Core();
    }

    lastTime_ = std::chrono::steady_clock::now();
    
    runLoopSubscription_ = brls::Application::getRunLoopEvent()->subscribe([this]() {
        this->updateStatus();
    });

    this->addView(new brls::BottomBar());

    buildHistoryUI();
    installNext();
}

LocalInstallView::~LocalInstallView() {
    brls::Application::getRunLoopEvent()->unsubscribe(runLoopSubscription_);
    readThreadActive_ = false;
    if (g_localInstallerCore) {
        g_localInstallerCore->AbortInstallation();
    }
    if (readThread_.joinable()) {
        readThread_.join();
    }
    if (g_localInstallerCore) {
        delete g_localInstallerCore;
        g_localInstallerCore = nullptr;
    }
}

void LocalInstallView::readThreadFunc() {
    brls::Logger::info("readThreadFunc started");
    std::string path = queue_[currentIndex_].path;
    brls::Logger::info("readThreadFunc opening file: {}", path);
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        brls::Logger::info("readThreadFunc fopen failed for: {}", path);
        if (g_localInstallerCore) g_localInstallerCore->AbortInstallation();
        readThreadActive_ = false;
        return;
    }
    
    brls::Logger::info("readThreadFunc fopen success");
    setvbuf(file, NULL, _IONBF, 0);
    fseek(file, 0, SEEK_END);
    currentTotalSize_ = ftell(file);
    fseek(file, 0, SEEK_SET);
    queue_[currentIndex_].size = currentTotalSize_;
    
    brls::Logger::info("readThreadFunc file size: {}", currentTotalSize_);
    
    const size_t chunkSize = 1024 * 1024 * 4; // 4MB
    char* buffer = new char[chunkSize];
    
    brls::Logger::info("readThreadFunc starting read loop");
    while (readThreadActive_ && currentIndex_ < (int)queue_.size()) {
        size_t bytesRead = fread(buffer, 1, chunkSize, file);
        if (bytesRead > 0) {
            if (!g_localInstallerCore->WriteData(buffer, bytesRead)) {
                brls::Logger::info("readThreadFunc WriteData returned false, breaking loop");
                break;
            }
        }
        
        if (bytesRead < chunkSize) {
            brls::Logger::info("readThreadFunc reached EOF");
            // EOF
            break;
        }
    }
    
    brls::Logger::info("readThreadFunc cleaning up");
    delete[] buffer;
    fclose(file);
    
    if (readThreadActive_) {
        brls::Logger::info("readThreadFunc calling FinishInstallation");
        g_localInstallerCore->FinishInstallation();
    }
    brls::Logger::info("readThreadFunc exiting");
}

void LocalInstallView::installNext() {
    brls::Logger::info("LocalInstallView::installNext called, currentIndex: {}", currentIndex_);
    if (currentIndex_ >= (int)queue_.size()) {
        brls::Logger::info("LocalInstallView::installNext finished queue");
        activeFilenameLabel_->setText(t("¡Proceso completado!", "Process completed!"));
        progressBar_->setProgress(1.0f);
        speedLabel_->setText("");
        progressTextLabel_->setText(t("Todas las instalaciones han terminado.", "All installations have finished."));
        buildHistoryUI(); // Ensure the last item shows as Completado
        return;
    }

    queue_[currentIndex_].status = 0; // Installing
    buildHistoryUI();

    std::string fn = queue_[currentIndex_].name;
    activeFilenameLabel_->setText(fmt::format("{}: {}", t("Instalando", "Installing"), fn));
    
    brls::Logger::info("LocalInstallView::installNext calling StartInstallation for {}", fn);
    if (g_localInstallerCore->StartInstallation(fn)) {
        brls::Logger::info("LocalInstallView::installNext StartInstallation returned true");
        readThreadActive_ = true;
        if (readThread_.joinable()) readThread_.join();
        brls::Logger::info("LocalInstallView::installNext starting readThreadFunc");
        readThread_ = std::thread(&LocalInstallView::readThreadFunc, this);
        brls::Logger::info("LocalInstallView::installNext readThreadFunc started");
    } else {
        brls::Logger::info("LocalInstallView::installNext StartInstallation returned false");
        queue_[currentIndex_].status = 2; // Error
        currentIndex_++;
        installNext();
    }
}

void LocalInstallView::buildHistoryUI() {
    historyTitleLabel_->setText(fmt::format("{} ({})", t("Cola de Instalación", "Installation Queue"), queue_.size()));
    historyList_->clearViews(true);
    
    for (const auto& item : queue_) {
        brls::Box* row = new brls::Box(brls::Axis::ROW);
        row->setWidthPercentage(100.0f);
        row->setHeight(30.0f);
        row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        row->setMarginBottom(5);
        
        brls::Label* nameL = new brls::Label();
        nameL->setText(item.name);
        nameL->setFontSize(20);
        nameL->setSingleLine(true);
        row->addView(nameL);
        
        brls::Box* rightBox = new brls::Box(brls::Axis::ROW);
        
        if (item.size > 0) {
            brls::Label* sizeL = new brls::Label();
            sizeL->setText(fmt::format("{:.2f} MB", (double)item.size / 1024.0 / 1024.0));
            sizeL->setFontSize(20);
            sizeL->setMarginRight(20);
            sizeL->setTextColor(nvgRGB(200, 200, 200));
            rightBox->addView(sizeL);
        }
        
        brls::Label* statusL = new brls::Label();
        if (item.status == 1) {
            statusL->setText(t("Completado", "Completed"));
            statusL->setTextColor(nvgRGB(50, 200, 50));
        } else if (item.status == 0) {
            statusL->setText(t("Instalando...", "Installing..."));
            statusL->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
        } else if (item.status == 2) {
            statusL->setText(t("Error", "Error"));
            statusL->setTextColor(nvgRGB(200, 50, 50));
        } else {
            statusL->setText(t("En cola", "Queued"));
            statusL->setTextColor(nvgRGB(150, 150, 150));
        }
        statusL->setFontSize(20);
        rightBox->addView(statusL);
        
        row->addView(rightBox);
        historyList_->addView(row);
    }
}

void LocalInstallView::updateStatus() {
    if (!g_localInstallerCore) return;
    if (currentIndex_ >= (int)queue_.size()) return;

    if (g_localInstallerCore->HasError()) {
        queue_[currentIndex_].status = 2;
        currentIndex_++;
        installNext();
        return;
    }

    if (g_localInstallerCore->IsFinished()) {
        queue_[currentIndex_].status = 1;
        currentIndex_++;
        installNext();
        return;
    }

    u64 written = g_localInstallerCore->GetBytesWritten();
    if (written > 0 && currentTotalSize_ > 0) {
        u64 diff = written - lastBytesWritten_;
        auto now = std::chrono::steady_clock::now();
        auto msPassed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime_).count();
        
        if (msPassed >= 1000) {
            currentSpeed_ = ((double)diff / 1024.0 / 1024.0) / (msPassed / 1000.0);
            lastBytesWritten_ = written;
            lastTime_ = now;
        }

        speedLabel_->setText(fmt::format("{:.1f} MB/s", currentSpeed_));
        
        float pct = (float)written / (float)currentTotalSize_;
        progressBar_->setProgress(pct);
        
        double wr_mb = (double)written / 1024.0 / 1024.0;
        double tot_mb = (double)currentTotalSize_ / 1024.0 / 1024.0;
        progressTextLabel_->setText(fmt::format("{:.2f} MB / {:.2f} MB ({:.1f}%)", wr_mb, tot_mb, pct * 100.0f));
    }
}

} // namespace goonies::ui
