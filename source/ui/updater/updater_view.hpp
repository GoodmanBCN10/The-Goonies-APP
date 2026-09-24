#pragma once

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
extern "C" {
#include <ipcext/amssu.h>
}
#include <string>
#include <vector>
#include <curl/curl.h>

namespace goonies::ui {

enum class UpdateMode {
    CFW,
    Firmware
};

class UpdaterView : public brls::Box {
public:
    UpdaterView(UpdateMode mode = UpdateMode::CFW);
    ~UpdaterView();

    static brls::View* create(UpdateMode mode = UpdateMode::CFW);

private:
    brls::Label* status_label;
    brls::Button* update_button;
    brls::Box* centerBox;
    
    brls::Box* progress_bar_bg;
    brls::Rectangle* progress_bar_fill;
    
    int last_progress_percent = -1;
    
    UpdateMode current_mode;
    std::string download_url;
    bool is_fetching;
    bool is_updating;

    void FetchLatestVersion();
    void PerformUpdate();
    
    static int ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
};

} // namespace goonies::ui
