#pragma once

#include <borealis.hpp>
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/installed_title_service.hpp"
#include "app/app_settings.hpp"
#include "app/homebrew_service.hpp"

namespace goonies::ui {

class MainMenu : public brls::Box {
public:
    MainMenu(pipensx::DownloadManager* manager, pipensx::CatalogService* catalog,
             pipensx::GameMetadataService* metadata,
             pipensx::InstalledTitleService* installed, pipensx::AppSettings* settings,
             pipensx::HomebrewService* homebrew);

private:
    pipensx::DownloadManager* manager_;
    pipensx::CatalogService* catalog_;
    pipensx::GameMetadataService* metadata_;
    pipensx::InstalledTitleService* installed_;
    pipensx::AppSettings* settings_;
    pipensx::HomebrewService* homebrew_;

    brls::Button* createMenuButton(const std::string& title, std::function<void()> onClick);
};

} // namespace goonies::ui
