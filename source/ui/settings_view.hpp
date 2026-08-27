#pragma once

#include <borealis.hpp>
#include "app/app_settings.hpp"

namespace goonies::ui {

class SettingsView : public brls::Box {
public:
    SettingsView(pipensx::AppSettings* settings) 
        : brls::Box(brls::Axis::COLUMN) 
    {
        this->setGrow(1.0f);
        
        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        this->addView(scroll);

        auto* container = new brls::Box(brls::Axis::COLUMN);
        container->setPadding(40, 80, 40, 80);
        scroll->setContentView(container);

        auto* title = new brls::Label();
        title->setText(settings->get().language == 2 ? "Settings" : "Ajustes");
        title->setFontSize(32);
        title->setMarginBottom(40);
        container->addView(title);
        
        // Version
        auto* versionBox = new brls::Box(brls::Axis::ROW);
        versionBox->setFocusable(true);
        versionBox->setPadding(20, 20, 20, 20);
        versionBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        
        auto* versionLabel = new brls::Label();
        versionLabel->setText(settings->get().language == 2 ? "App Version" : "Version de la App");
        versionLabel->setFontSize(24);
        
        auto* versionValue = new brls::Label();
        versionValue->setText(std::string(PIPENSX_VERSION) + " (The Goonies Ports)");
        versionValue->setFontSize(24);
        versionValue->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        
        versionBox->addView(versionLabel);
        versionBox->addView(versionValue);
        container->addView(versionBox);
        
        // Language
        auto* langBox = new brls::Box(brls::Axis::ROW);
        langBox->setFocusable(true);
        langBox->setPadding(20, 20, 20, 20);
        langBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        
        auto* langLabel = new brls::Label();
        langLabel->setText(settings->get().language == 2 ? "Language / Idioma" : "Idioma / Language");
        langLabel->setFontSize(24);
        
        auto* langValue = new brls::Label();
        langValue->setText(settings->get().language == 2 ? "English (EN)" : "Castellano (ES)");
        langValue->setFontSize(24);
        langValue->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        
        langBox->addView(langLabel);
        langBox->addView(langValue);
        
        langBox->registerClickAction([settings, langValue](brls::View* view) {
            brls::Dialog* langDialog = new brls::Dialog(settings->get().language == 2 ? "Select your language / Selecciona tu idioma" : "Selecciona tu idioma / Select your language");
            langDialog->addButton("Castellano (ES)", [settings, langValue, langDialog]() {
                pipensx::AppSettingsData data = settings->get();
                data.language = 1;
                std::string e;
                settings->update(data, e);
                langValue->setText("Castellano (ES)");
                brls::Application::quit(); // Reiniciar para aplicar
            });
            langDialog->addButton("English (EN)", [settings, langValue, langDialog]() {
                pipensx::AppSettingsData data = settings->get();
                data.language = 2;
                std::string e;
                settings->update(data, e);
                langValue->setText("English (EN)");
                brls::Application::quit(); // Reiniciar para aplicar
            });
            langDialog->addButton(settings->get().language == 2 ? "Cancel" : "Cancelar", [langDialog]() {
                langDialog->dismiss();
            });
            langDialog->open();
            return true;
        });
        
        container->addView(langBox);
        
        // BGM Toggle
        auto* bgmCell = new brls::BooleanCell();
        bgmCell->init(settings->get().language == 2 ? "Background Music" : "Musica de Fondo", settings->get().enableBackgroundMusic, [settings](bool state) {
            pipensx::AppSettingsData data = settings->get();
            data.enableBackgroundMusic = state;
            std::string err;
            settings->update(data, err);
        });
        container->addView(bgmCell);
    
        // MASK PARENT ACTIONS
        this->registerAction("", brls::BUTTON_BACK, [](brls::View*){return true;}, true);
    }
};

} // namespace goonies::ui
