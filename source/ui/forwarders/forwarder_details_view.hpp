#pragma once
#include <borealis.hpp>
#include "app/homebrew_service.hpp"

namespace pipensx::ui {

class ForwarderDetailsView : public brls::Box {
public:
    ForwarderDetailsView(const HomebrewTitle& title);

private:
    HomebrewTitle title_;
    
    brls::Image* icon_;
    brls::Label* nameLabel_;
    brls::Label* authorLabel_;
    
    brls::Button* launchButton_;
    brls::Button* forwarderButton_;
    
    void UpdateButtons();
    void Launch();
    void CreateForwarder();
};

} // namespace pipensx::ui
