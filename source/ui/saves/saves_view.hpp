#pragma once

#include <string>
#include <vector>
#include <borealis.hpp>

#include "app/installed_title_service.hpp"
#include "app/game_metadata_service.hpp"
#include "ui/installed/installed_view.hpp"
#include "ui/common/message_cells.hpp"

namespace pipensx::ui {

class SavesView : public brls::Box {
public:
    SavesView(InstalledTitleService* installedService, GameMetadataService* metadata);
    
private:
    InstalledTitleService* installedService_;
    GameMetadataService* metadata_;
    
    brls::RecyclerFrame* recycler_ = nullptr;
    InstalledDataSource* dataSource_ = nullptr;
};

} // namespace pipensx::ui
