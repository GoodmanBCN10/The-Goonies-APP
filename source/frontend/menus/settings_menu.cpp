#include "frontend/menus/settings_menu.hpp"
#include "app.hpp"
#include "frontend/nvg_util.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include <switch.h>
#include <math.h>

namespace GooniesInstaller::frontend::menu {

SettingsMenu::SettingsMenu() {
    this->SetPos(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetAction(Button::B, Action{"Atras", [this](){
        SetPop();
    }});

    // Get App Version
    m_app_version = APP_VERSION;

    // Get Firmware Version
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
            m_fw_version = fw.display_version;
        } else {
            m_fw_version = "Unknown";
        }
        setsysExit();
    } else {
        m_fw_version = "Unknown";
    }

    // Get SD Card capacity
    App::GetSdSize(&m_sd_free, &m_sd_total);
}

SettingsMenu::~SettingsMenu() {
}

void SettingsMenu::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);

    if (controller->GotDown(Button::UP)) {
        m_selected_index = (m_selected_index - 1 + 1) % 1; // Only language selector for now
        App::PlaySoundEffect(audio::SoundEffect::Focus);
    } else if (controller->GotDown(Button::DOWN)) {
        m_selected_index = (m_selected_index + 1) % 1;
        App::PlaySoundEffect(audio::SoundEffect::Focus);
    }

    if (controller->GotDown(Button::A)) {
        App::PlaySoundEffect(audio::SoundEffect::Focus);
        if (m_selected_index == 0) {
            // Toggle between English (1) and Spanish (6)
            long current = App::GetLanguage();
            if (current == 6) {
                App::SetLanguage(1); // English
            } else {
                App::SetLanguage(6); // Spanish
            }
        }
    }
}

void SettingsMenu::Draw(NVGcontext* vg, Theme* theme) {
    float cx = SCREEN_WIDTH / 2.0f;

    // Background
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, SCREEN_HEIGHT, nvgRGBA(20, 25, 40, 255), nvgRGBA(10, 15, 25, 255));
    nvgFillPaint(vg, bg); nvgFill(vg);

    // Title
    gfx::drawTextArgs(vg, cx, 80.0f, 40.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 255), "Ajustes");

    float startY = 200.0f;
    float rowH = 60.0f;

    // Box to contain settings
    nvgBeginPath(vg); nvgRoundedRect(vg, cx - 300, startY - 20, 600, 300, 15.0f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 100)); nvgFill(vg);

    auto drawRow = [&](int index, const char* label, const std::string& value, bool selectable) {
        float y = startY + index * rowH;
        if (selectable && m_selected_index == index) {
            nvgBeginPath(vg); nvgRoundedRect(vg, cx - 290, y - 10, 580, rowH - 10, 8.0f);
            nvgFillColor(vg, nvgRGBA(255, 160, 0, 150)); nvgFill(vg);
        }

        gfx::drawTextArgs(vg, cx - 270, y + 15, 24.0f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, nvgRGBA(200, 200, 200, 255), "%s", label);
        gfx::drawTextArgs(vg, cx + 270, y + 15, 24.0f, NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 255), "%s", value.c_str());
    };

    long current = App::GetLanguage();
    bool is_en = (current == 1);
    std::string lang_str = is_en ? "English" : "Español";
    drawRow(0, is_en ? "Language / Idioma" : "Idioma / Language", lang_str, true);
    
    char sd_str[64];
    std::snprintf(sd_str, sizeof(sd_str), "%.1f GB / %.1f GB", (float)(m_sd_total - m_sd_free) / (1024.f*1024.f*1024.f), (float)m_sd_total / (1024.f*1024.f*1024.f));
    drawRow(1, is_en ? "SD Capacity" : "Capacidad SD", sd_str, false);
    
    drawRow(2, "Firmware", m_fw_version, false);
    drawRow(3, is_en ? "Version" : "Versión", APP_VERSION, false);
    
    // Bottom instructions
    const char* instr = is_en ? "Press B to go back" : "Pulsa B para volver";
    gfx::drawTextArgs(vg, cx, SCREEN_HEIGHT - 30.0f, 18.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(200, 200, 200, 255), instr);
}

} // namespace GooniesInstaller::frontend::menu
