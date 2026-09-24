#pragma once

#include <borealis.hpp>
#include <string>
#include <atomic>

namespace pipensx::ui {

class CheatDownloaderView : public brls::Box {
public:
    CheatDownloaderView(const std::string& titleIdStr, std::function<void()> onComplete = nullptr);
    ~CheatDownloaderView() override;

    static brls::View* create(const std::string& titleIdStr, std::function<void()> onComplete = nullptr);

    brls::Label* statusLabel = nullptr;

private:
    void doDownload();
    void extractZip(const std::string& temp_zip);

    std::string m_titleIdStr;
    std::atomic<bool> isDownloading{false};
    std::atomic<bool> isExtracting{false};
    std::function<void()> onCompleteCb;
    std::shared_ptr<bool> isAlive;
};

} // namespace pipensx::ui
