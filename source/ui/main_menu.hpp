#pragma once

#include <borealis.hpp>
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/installed_title_service.hpp"
#include "app/app_settings.hpp"
#include "app/homebrew_service.hpp"
#include "app/update_service.hpp"

namespace goonies::ui {

class MainMenu : public brls::Box {
public:
    MainMenu(goonies::DownloadManager* manager, goonies::CatalogService* catalog,
             goonies::GameMetadataService* metadata,
             goonies::InstalledTitleService* installed, goonies::AppSettings* settings,
             goonies::HomebrewService* homebrew, goonies::UpdateService* updater);

private:
    goonies::DownloadManager* manager_;
    goonies::CatalogService* catalog_;
    goonies::GameMetadataService* metadata_;
    goonies::InstalledTitleService* installed_;
    goonies::AppSettings* settings_;
    goonies::HomebrewService* homebrew_;
    goonies::UpdateService* updater_;

    brls::Button* createMenuButton(const std::string& title, std::function<void()> onClick);
};

} // namespace goonies::ui
