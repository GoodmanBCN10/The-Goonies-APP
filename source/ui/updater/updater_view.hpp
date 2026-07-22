#pragma once

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <string>
#include <vector>
#include <curl/curl.h>

namespace goonies::ui {

class UpdaterView : public brls::Box {
public:
    UpdaterView();
    ~UpdaterView();

    static brls::View* create();

private:
    brls::Label* status_label;
    brls::Button* update_button;
    brls::Box* checkboxes_box;
    brls::ScrollingFrame* scroll_frame;
    brls::Box* centerBox;
    
    brls::Box* progress_bar_bg;
    brls::Rectangle* progress_bar_fill;
    
    std::vector<brls::BooleanCell*> folder_toggles;
    int last_progress_percent = -1;
    
    std::string download_url;
    bool is_fetching;
    bool is_updating;

    void FetchLatestVersion();
    void PerformUpdate();
    
    static int ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
};

} // namespace goonies::ui
