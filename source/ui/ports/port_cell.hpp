#pragma once

#include <borealis.hpp>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include "app/catalog_service.hpp"
#include "app/simple_download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "ui/common/async_image.hpp"

namespace goonies::ui {

class PortCell : public brls::Box {
public:
    PortCell(pipensx::CatalogEntry entry, goonies::SimpleDownloadManager* manager, pipensx::GameMetadataService* metadata_service) 
        : brls::Box(brls::Axis::COLUMN), entry_(std::move(entry)), manager_(manager), metadata_(metadata_service)
    {
        this->setFocusable(true);
        this->setPadding(20);
        this->setCornerRadius(10.0f);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setBackgroundColor(brls::Application::getTheme().getColor("brls/sidebar/background"));

        image_ = new pipensx::ui::AsyncRgbaImage();
        image_->setWidth(120);
        image_->setHeight(120);
        image_->setMarginBottom(15);
        image_->setCornerRadius(15.0f);
        image_->setScalingType(brls::ImageScalingType::FIT);
        
        if (!entry_.posterUrl.empty()) {
            pipensx::ui::loadImageInto(image_, metadata_, entry_.posterUrl);
        } else {
            image_->setImageFromRes("icon_explorer.png");
        }
        
        this->addView(image_);

        auto* titleLabel = new brls::Label();
        titleLabel->setText(entry_.title);
        titleLabel->setFontSize(22);
        titleLabel->setMarginBottom(5);
        titleLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        this->addView(titleLabel);

        auto* authorLabel = new brls::Label();
        authorLabel->setText(entry_.developer);
        authorLabel->setFontSize(16);
        authorLabel->setTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
        authorLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);

        this->addView(authorLabel);
        
        bool installed = false;
        struct stat st;
        std::string path1 = "sdmc:/switch/" + entry_.name;
        std::string path2 = "sdmc:/switch/" + entry_.title;
        
        std::string titleNoSpace = entry_.title;
        titleNoSpace.erase(std::remove_if(titleNoSpace.begin(), titleNoSpace.end(), ::isspace), titleNoSpace.end());
        std::string path3 = "sdmc:/switch/" + titleNoSpace;
        
        if (stat(path1.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { installed = true; }
        else if (stat(path2.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { installed = true; }
        else if (stat(path3.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { installed = true; }

        if (installed) {
            auto* badge = new brls::Label();
            badge->setText("✔ INSTALADO");
            badge->setFontSize(16);
            badge->setTextColor(brls::Application::getTheme()["brls/accent"]);
            badge->setMarginTop(10);
            badge->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            this->addView(badge);
        }
        
        std::string actionName = installed ? "Reinstalar" : "Instalar";
        this->registerAction(actionName, brls::BUTTON_A, [this](brls::View*) {

            this->showInstallDialog();
            return true;
        });
    }

private:
    void showInstallDialog() {
        auto* dialog = new brls::Dialog("¿Instalar " + entry_.title + "?");
        dialog->addButton("Instalar", [this]() {
            manager_->add(entry_.name, entry_.title, entry_.directDownloadUrl, entry_.developer, entry_.posterUrl);
            brls::Application::notify("Descarga añadida a la cola");
        });
        dialog->addButton("Cancelar", []{});
        dialog->open();
    }

    pipensx::CatalogEntry entry_;
    goonies::SimpleDownloadManager* manager_;
    pipensx::GameMetadataService* metadata_;
    pipensx::ui::AsyncRgbaImage* image_;
};

} // namespace goonies::ui
