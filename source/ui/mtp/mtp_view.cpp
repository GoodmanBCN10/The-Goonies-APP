#include "mtp_view.hpp"
#include "ui/common/ui_helpers.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <fmt/core.h>
#include <borealis/views/bottom_bar.hpp>
#include <fmt/core.h>

namespace goonies::ui {

static Installer::Core* g_mtpInstallerCore = nullptr;

// A custom box to draw the semi-transparent rounded background like in the original app
class PanelBox : public brls::Box {
public:
    PanelBox() : brls::Box(brls::Axis::COLUMN) {}

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        // Draw rounded background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 15.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 25)); // Semi-transparent white
        nvgFill(vg);

        // Draw children
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }
};

MTPView::MTPView() : brls::Box(brls::Axis::COLUMN) {
    this->setFocusable(true);
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    centerBox->setAlignItems(brls::AlignItems::STRETCH);
    centerBox->setPadding(40, 80, 40, 80);
    centerBox->setGrow(1.0f);

    // Header Title
    brls::Label* title = new brls::Label();
    title->setText(t("Instalar por MTP", "MTP Installer"));
    title->setFontSize(45);
    title->setTextColor(brls::Application::getTheme().getColor("brls/accent")); // Goonies Yellow
    title->setMarginBottom(20);
    centerBox->addView(title);

    // Active Install Box
    activeInstallBox_ = new PanelBox();
    activeInstallBox_->setPadding(20, 20, 20, 20);
    activeInstallBox_->setMarginBottom(20);
    
    brls::Box* activeTopRow = new brls::Box(brls::Axis::ROW);
    activeTopRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    activeTopRow->setMarginBottom(10);
    
    activeFilenameLabel_ = new brls::Label();
    activeFilenameLabel_->setText(t("Esperando conexión o arrastra juegos...", "Waiting for connection or drag games..."));
    activeFilenameLabel_->setFontSize(22);
    activeFilenameLabel_->setTextColor(nvgRGB(255, 255, 255));
    activeTopRow->addView(activeFilenameLabel_);
    
    speedLabel_ = new brls::Label();
    speedLabel_->setText("0.0 MB/s");
    speedLabel_->setFontSize(22);
    speedLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent")); // Yellow
    activeTopRow->addView(speedLabel_);
    
    activeInstallBox_->addView(activeTopRow);
    
    progressBar_ = new pipensx::ui::ProgressBar();
    progressBar_->setHeight(15);
    progressBar_->setProgress(0.0f);
    progressBar_->setMarginBottom(10);
    activeInstallBox_->addView(progressBar_);
    
    progressTextLabel_ = new brls::Label();
    progressTextLabel_->setText("0.00 MB / 0.00 MB (0.0%)");
    progressTextLabel_->setFontSize(18);
    progressTextLabel_->setTextColor(nvgRGB(200, 200, 200));
    activeInstallBox_->addView(progressTextLabel_);
    
    centerBox->addView(activeInstallBox_);

    // History Box
    historyBox_ = new PanelBox();
    historyBox_->setPadding(20, 20, 20, 20);
    historyBox_->setGrow(1.0f); // Fill remaining space
    
    historyTitleLabel_ = new brls::Label();
    historyTitleLabel_->setText(t("Archivos recibidos en esta sesión (0)", "Files received in this session (0)"));
    historyTitleLabel_->setFontSize(22);
    historyTitleLabel_->setTextColor(nvgRGB(255, 255, 255));
    historyTitleLabel_->setMarginBottom(20);
    historyBox_->addView(historyTitleLabel_);
    
    brls::ScrollingFrame* scrollFrame = new brls::ScrollingFrame();
    scrollFrame->setWidthPercentage(100.0f);
    scrollFrame->setGrow(1.0f);
    
    historyList_ = new brls::Box(brls::Axis::COLUMN);
    historyList_->setWidthPercentage(100.0f);
    
    scrollFrame->setContentView(historyList_);
    historyBox_->addView(scrollFrame);
    
    centerBox->addView(historyBox_);

    this->addView(centerBox);
    this->addView(new brls::BottomBar());

    // Init Installer Core
    if (!g_mtpInstallerCore) {
        g_mtpInstallerCore = new Installer::Core();
    }

    lastTime_ = std::chrono::steady_clock::now();

    // Init MTP
    MTP::Init();
#ifdef __SWITCH__
    appletSetMediaPlaybackState(true);
#endif
    
    MTP::InitInstallMode(
        [this](const char* filename, size_t size) -> bool {
            if (g_mtpInstallerCore) {
                std::string fname = filename;
                brls::sync([this, fname, size]() {
                    currentFilename_ = fname;
                    currentTotalSize_ = size;
                    lastBytesWritten_ = 0;
                    lastTime_ = std::chrono::steady_clock::now();
                    
                    // Create dynamic row and insert at the top of historyList_ without clearing
                    brls::Box* row = new brls::Box(brls::Axis::ROW);
                    row->setFocusable(true);
                    row->setWidthPercentage(100.0f);
                    row->setHeight(30.0f);
                    row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
                    row->setMarginBottom(5);
                    
                    brls::Label* nameL = new brls::Label();
                    nameL->setText(fname);
                    nameL->setFontSize(18);
                    nameL->setTextColor(nvgRGB(255, 255, 255));
                    
                    brls::Box* rightBox = new brls::Box(brls::Axis::ROW);
                    rightBox->setJustifyContent(brls::JustifyContent::FLEX_END);
                    
                    brls::Label* sizeL = new brls::Label();
                    sizeL->setText(formatSize(size));
                    sizeL->setFontSize(18);
                    sizeL->setTextColor(nvgRGB(200, 200, 200));
                    sizeL->setMarginRight(30);
                    
                    activeHistoryStatusLabel_ = new brls::Label();
                    activeHistoryStatusLabel_->setText(t("Instalando...", "Installing..."));
                    activeHistoryStatusLabel_->setFontSize(18);
                    activeHistoryStatusLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent")); // Yellow
                    
                    rightBox->addView(sizeL);
                    rightBox->addView(activeHistoryStatusLabel_);
                    
                    row->addView(nameL);
                    row->addView(rightBox);
                    
                    // Insert at position 0
                    historyList_->addView(row, 0);
                    
                    sessionFileCount_++;
                    historyTitleLabel_->setText(fmt::format("{} ({})", t("Archivos recibidos en esta sesión", "Files received in this session"), sessionFileCount_));
                });
                
                return g_mtpInstallerCore->StartInstallation(filename);
            }
            return false;
        },
        [this](const void* data, size_t size) -> bool {
            if (g_mtpInstallerCore) return g_mtpInstallerCore->WriteData(data, size);
            return false;
        },
        [this]() {
            if (g_mtpInstallerCore) {
                g_mtpInstallerCore->FinishInstallation();
                brls::sync([this]() {
                    if (activeHistoryStatusLabel_) {
                        if (g_mtpInstallerCore->HasError()) {
                            activeHistoryStatusLabel_->setText(t("Error", "Error"));
                            activeHistoryStatusLabel_->setTextColor(nvgRGB(255, 50, 50));
                        } else {
                            activeHistoryStatusLabel_->setText(t("Completado", "Completed"));
                            activeHistoryStatusLabel_->setTextColor(nvgRGB(50, 255, 50));
                        }
                        activeHistoryStatusLabel_ = nullptr;
                    }
                });
            }
        }
    );

    this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [this](brls::View* view) {
        if (g_mtpInstallerCore && !g_mtpInstallerCore->IsFinished() && !g_mtpInstallerCore->HasError()) {
            g_mtpInstallerCore->AbortInstallation();
        }
        brls::Application::popActivity();
        return true;
    });

    runLoopSubscription_ = brls::Application::getRunLoopEvent()->subscribe([this]() {
        this->updateStatus();
    });
}

MTPView::~MTPView() {
    brls::Application::getRunLoopEvent()->unsubscribe(runLoopSubscription_);
    MTP::DisableInstallMode();
#ifdef __SWITCH__
    appletSetMediaPlaybackState(false);
#endif
    if (g_mtpInstallerCore) {
        delete g_mtpInstallerCore;
        g_mtpInstallerCore = nullptr;
    }
}

brls::View* MTPView::create() {
    return new MTPView();
}



std::string MTPView::formatSize(u64 size) {
    if (size < 1024) return fmt::format("{} B", size);
    if (size < 1024 * 1024) return fmt::format("{:.2f} KB", size / 1024.0);
    if (size < 1024 * 1024 * 1024) return fmt::format("{:.2f} MB", size / (1024.0 * 1024.0));
    return fmt::format("{:.2f} GB", size / (1024.0 * 1024.0 * 1024.0));
}

void MTPView::updateStatus() {
    if (!g_mtpInstallerCore) return;

    if (g_mtpInstallerCore->HasError()) {
        activeFilenameLabel_->setText(fmt::format("Error de instalación (0x{:X})", g_mtpInstallerCore->GetErrorCode()));
        progressBar_->setProgress(1.0f);
        speedLabel_->setText("");
    } else if (g_mtpInstallerCore->IsFinished() && !currentFilename_.empty()) {
        activeFilenameLabel_->setText("Esperando más juegos...");
        progressBar_->setProgress(0.0f);
        speedLabel_->setText("");
        progressTextLabel_->setText("");
        currentFilename_ = "";
        currentTotalSize_ = 0;
    } else if (!currentFilename_.empty()) {
        u64 written = g_mtpInstallerCore->GetBytesWritten();
        activeFilenameLabel_->setText(fmt::format("Instalando: {}", currentFilename_));
        
        u64 diff = written - lastBytesWritten_;
        auto now = std::chrono::steady_clock::now();
        auto msPassed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime_).count();
        
        if (msPassed >= 1000) {
            currentSpeed_ = ((double)diff / 1024.0 / 1024.0) / (msPassed / 1000.0);
            lastBytesWritten_ = written;
            lastTime_ = now;
        }

        speedLabel_->setText(fmt::format("{:.1f} MB/s", currentSpeed_));
        
        if (currentTotalSize_ > 0) {
            float pct = (float)written / (float)currentTotalSize_;
            progressBar_->setProgress(pct);
            progressTextLabel_->setText(fmt::format("{} / {} ({:.1f}%)", formatSize(written), formatSize(currentTotalSize_), pct * 100.0f));
        } else {
            progressBar_->setProgress(0.5f);
            progressTextLabel_->setText(fmt::format("{} instalados", formatSize(written)));
        }
    }
}

void MTPView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

MTPExplorerView::MTPExplorerView() : brls::Box(brls::Axis::COLUMN) {
    this->setFocusable(true);
    this->setAlignItems(brls::AlignItems::STRETCH);

    MTP::Init(true); // Mount ONLY Unit 2: SD Card Explorer
#ifdef __SWITCH__
    appletSetMediaPlaybackState(true);
#endif

    brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setJustifyContent(brls::JustifyContent::CENTER);
    centerBox->setAlignItems(brls::AlignItems::CENTER);
    centerBox->setGrow(1.0f);

    titleLabel_ = new brls::Label();
    titleLabel_->setText(t("EXPLORAR MICROSD", "EXPLORE MICROSD"));
    titleLabel_->setFontSize(54);
    titleLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent")); // Goonies Yellow
    titleLabel_->setMarginBottom(40);
    centerBox->addView(titleLabel_);

    statusLabel_ = new brls::Label();
    lastConnected_ = MTP::IsConnected();
    statusLabel_->setText(lastConnected_ ? t("Mira las carpetas en tu PC", "Look at the folders on your PC") : t("Conecta el cable USB a tu PC", "Connect the USB cable to your PC"));
    statusLabel_->setFontSize(36);
    statusLabel_->setTextColor(lastConnected_ ? brls::Application::getTheme().getColor("brls/accent") : nvgRGB(255, 255, 255));
    statusLabel_->setMarginBottom(20);
    centerBox->addView(statusLabel_);

    this->addView(centerBox);
    this->addView(new brls::BottomBar());

    this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::SOUND_BACK);
}

MTPExplorerView::~MTPExplorerView() {
    MTP::Exit();
#ifdef __SWITCH__
    appletSetMediaPlaybackState(false);
#endif
}

void MTPExplorerView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    if (statusLabel_) {
        bool connected = MTP::IsConnected();
        if (connected != lastConnected_) {
            lastConnected_ = connected;
            statusLabel_->setText(connected ? t("Mira las carpetas en tu PC", "Look at the folders on your PC") : t("Conecta el cable USB a tu PC", "Connect the USB cable to your PC"));
            statusLabel_->setTextColor(connected ? brls::Application::getTheme().getColor("brls/accent") : nvgRGB(255, 255, 255));
        }
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

} // namespace goonies::ui
