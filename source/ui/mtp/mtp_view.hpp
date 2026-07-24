#pragma once

#include <borealis.hpp>
#include <vector>
#include <string>
#include <chrono>
#include "mtp/haze_helper.hpp"
#include "installer/installer_core.hpp"
#include "ui/common/progress_bar.hpp"

namespace goonies::ui {

class MTPView : public brls::Box {
public:
    MTPView();
    ~MTPView() override;

    static brls::View* create();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    brls::Label* instructionsLabel_;
    brls::Box* activeInstallBox_;
    brls::Label* activeFilenameLabel_;
    brls::Label* speedLabel_;
    pipensx::ui::ProgressBar* progressBar_;
    brls::Label* progressTextLabel_;

    brls::Box* historyBox_;
    brls::Label* historyTitleLabel_;
    brls::Box* historyList_;

    brls::Label* footerLabel_;

    u64 currentTotalSize_ = 0;
    std::string currentFilename_ = "";
    brls::Label* activeHistoryStatusLabel_ = nullptr;
    int sessionFileCount_ = 0;

    u32 lastUpdateTicks_ = 0;
    u64 lastBytesWritten_ = 0;
    std::chrono::steady_clock::time_point lastTime_;
    double currentSpeed_ = 0.0;

    void updateStatus();
    std::string formatSize(u64 size);

    brls::Event<>::Subscription runLoopSubscription_;
};

class MTPExplorerView : public brls::Box {
public:
    MTPExplorerView();
    ~MTPExplorerView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

private:
    brls::Label* titleLabel_ = nullptr;
    brls::Label* statusLabel_ = nullptr;
    bool lastConnected_ = false;
    brls::Event<>::Subscription runLoopSubscription_;
};

} // namespace goonies::ui
