#pragma once

#include <borealis.hpp>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include "installer/installer_core.hpp"
#include "ui/common/progress_bar.hpp"

namespace goonies::ui {

struct LocalHistoryItem {
    std::string path;
    std::string name;
    u64 size = 0;
    int status = -1; // -1 pending, 0 installing, 1 success, 2 error
};

class LocalInstallView : public brls::Box {
public:
    explicit LocalInstallView(const std::vector<std::string>& queue);
    ~LocalInstallView() override;

private:
    void buildHistoryUI();
    void updateStatus();
    void installNext();
    void readThreadFunc();

    std::vector<LocalHistoryItem> queue_;
    int currentIndex_ = 0;

    std::atomic<bool> readThreadActive_{false};
    std::thread readThread_;

    brls::Label* titleLabel_;
    brls::Label* instructionsLabel_;
    brls::Box* progressContainer_;
    
    brls::Label* activeFilenameLabel_;
    pipensx::ui::ProgressBar* progressBar_;
    brls::Label* progressTextLabel_;
    brls::Label* speedLabel_;

    brls::Box* historyBox_;
    brls::ScrollingFrame* historyScroll_;
    brls::Box* historyList_;
    brls::Label* historyTitleLabel_;

    u64 currentTotalSize_ = 0;
    u64 lastBytesWritten_ = 0;
    std::chrono::steady_clock::time_point lastTime_;
    double currentSpeed_ = 0.0;
    
    brls::Event<>::Subscription runLoopSubscription_;
};

} // namespace goonies::ui
