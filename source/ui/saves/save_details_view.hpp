#pragma once

#include <string>
#include <vector>
#include <borealis.hpp>

#include "app/installed_title_service.hpp"
#include "app/game_metadata_service.hpp"

namespace pipensx::ui {

class SaveDetailsView : public brls::Box {
public:
    SaveDetailsView(const InstalledTitle& title, GameMetadataService* metadata);

private:
    void UpdateButtons();

    InstalledTitle title_;
    GameMetadataService* metadata_;
    
    brls::Button* backupButton_ = nullptr;
    brls::Button* restoreButton_ = nullptr;
    brls::Button* deleteButton_ = nullptr;
    brls::Label* statusLabel_ = nullptr;
};

} // namespace pipensx::ui
