#include "frontend/menus/goonies_main_menu.hpp"
#include "frontend/menus/main_menu.hpp"
#include "frontend/menus/settings_menu.hpp"
#include "app.hpp"
#include "frontend/nvg_util.hpp"
#include "defines.hpp"
#include <math.h>

#include "nro.hpp"
#include <switch.h>

namespace GooniesInstaller::frontend::menu::main {

GooniesMainMenu::GooniesMainMenu() {
    this->SetPos(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetAction(Button::START, Action{"Salir", App::Exit});

    auto nro_icon = nro_get_icon(App::GetExePath());
    if (!nro_icon.empty()) {
        m_logo_img = nvgCreateImageMem(App::GetVg(), NVG_IMAGE_GENERATE_MIPMAPS, nro_icon.data(), nro_icon.size());
    }
}

GooniesMainMenu::~GooniesMainMenu() {
    if (m_logo_img != 0) {
        nvgDeleteImage(App::GetVg(), m_logo_img);
    }
}

void GooniesMainMenu::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);

    if (controller->GotDown(Button::UP)) {
        m_selected_index = (m_selected_index - 1 + 5) % 5;
        App::PlaySoundEffect(audio::SoundEffect::Focus);
    } else if (controller->GotDown(Button::DOWN)) {
        m_selected_index = (m_selected_index + 1) % 5;
        App::PlaySoundEffect(audio::SoundEffect::Focus);
    }

    if (controller->GotDown(Button::A)) {
        App::PlaySoundEffect(audio::SoundEffect::Focus);
        switch (m_selected_index) {
            case 0: 
                App::GetApp()->m_center_menu.Set("MTP");
                App::Push<frontend::menu::main::MainMenu>();
                break;
            case 1: 
                App::GetApp()->m_center_menu.Set("Games");
                App::Push<frontend::menu::main::MainMenu>();
                break;
            case 2: 
                App::GetApp()->m_center_menu.Set("Saves");
                App::Push<frontend::menu::main::MainMenu>();
                break;
            case 3: 
                App::GetApp()->m_center_menu.Set("FileBrowser");
                App::Push<frontend::menu::main::MainMenu>();
                break;
            case 4:
                App::Push<frontend::menu::SettingsMenu>();
                break;
        }
    }
}

void GooniesMainMenu::DrawLogo(NVGcontext* vg, float cx, float cy, float t) {
    // Top-left layout
    float s = 140.0f;
    float startX = 50.0f; // Left padding
    float ly = 40.0f;     // Top padding
    
    // Draw Logo Image if loaded
    if (m_logo_img != 0) {
        NVGpaint imgPaint = nvgImagePattern(vg, startX, ly, s, s, 0, m_logo_img, 1.0f);
        nvgBeginPath(vg); nvgRoundedRect(vg, startX, ly, s, s, 15.0f);
        nvgFillPaint(vg, imgPaint); nvgFill(vg);
    } else {
        // Fallback placeholder
        nvgBeginPath(vg); nvgRoundedRect(vg, startX, ly, s, s, 15.0f);
        nvgFillColor(vg, nvgRGBA(50, 60, 80, 255)); nvgFill(vg);
        gfx::drawTextArgs(vg, startX + s/2.0f, ly + s/2.0f, 40.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 255), "NRO");
    }
    
    // Glow effect behind logo
    nvgBeginPath(vg); nvgRoundedRect(vg, startX, ly, s, s, 15.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 160, 0, 150 + sinf(t * 4.0f) * 55.0f)); 
    nvgStrokeWidth(vg, 2.5f); nvgStroke(vg);
    
    // Title (Bigger and Bold)
    float ty = ly + s/2.0f - 16.0f;
    float tx = startX + s + 35.0f;
    NVGcolor tcol = nvgRGBA(255, 180, 0, 255);
    
    // Draw 4 times for bold effect
    gfx::drawTextArgs(vg, tx, ty, 56.0f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, tcol, "THE GOONIES OS");
    gfx::drawTextArgs(vg, tx + 1.0f, ty, 56.0f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, tcol, "THE GOONIES OS");
    gfx::drawTextArgs(vg, tx, ty + 1.0f, 56.0f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, tcol, "THE GOONIES OS");
    gfx::drawTextArgs(vg, tx + 1.0f, ty + 1.0f, 56.0f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, tcol, "THE GOONIES OS");
    
    // Subtitle
    bool is_en = (App::GetLanguage() == 1);
    const char* sub = is_en ? "Smart microSD Installer for Nintendo Switch" : "Instalador inteligente para Nintendo Switch";
    gfx::drawTextArgs(vg, startX + s + 35.0f, ly + s/2.0f + 34.0f, 24.0f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, nvgRGBA(160, 160, 160, 255), sub);
}

void GooniesMainMenu::DrawPastilla(NVGcontext* vg, float x, float y, float w, float h,
    const char* icon, const char* title, const char* desc, bool selected, Theme* theme, float t)
{
    float popW = selected ? w + 10.0f : w;
    float popH = selected ? h + 6.0f : h;
    float popX = selected ? x - 5.0f : x;
    float popY = selected ? y - 3.0f : y;
    float radius = 12.0f; // Sharper corners like the desktop app

    // Main button background
    NVGcolor bgCol = nvgRGBA(20, 20, 20, 255);
    nvgBeginPath(vg); nvgRoundedRect(vg, popX, popY, popW, popH, radius);
    nvgFillColor(vg, bgCol); nvgFill(vg);

    // Border
    if (selected) {
        nvgBeginPath(vg); nvgRoundedRect(vg, popX, popY, popW, popH, radius);
        nvgStrokeColor(vg, nvgRGBA(255, 165, 0, 255)); // Bright orange
        nvgStrokeWidth(vg, 2.0f); nvgStroke(vg);
    } else {
        nvgBeginPath(vg); nvgRoundedRect(vg, popX, popY, popW, popH, radius);
        nvgStrokeColor(vg, nvgRGBA(60, 60, 60, 255)); // Thin gray
        nvgStrokeWidth(vg, 1.0f); nvgStroke(vg);
    }

    // Left Icon
    float iconY = popY + popH / 2.0f;
    NVGcolor iconCol = selected ? nvgRGBA(255, 165, 0, 255) : nvgRGBA(150, 150, 150, 255);
    gfx::drawTextArgs(vg, popX + 40.0f, iconY, 32.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, iconCol, "%s", icon);

    // Textos
    NVGcolor titleCol = selected ? nvgRGBA(255, 165, 0, 255) : nvgRGBA(220, 220, 220, 255);
    NVGcolor descCol  = selected ? nvgRGBA(180, 180, 180, 255) : nvgRGBA(120, 120, 120, 255);

    float textX = popX + 80.0f;
    
    gfx::drawTextArgs(vg, textX, popY + h*0.4f, selected ? 26.0f : 24.0f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, titleCol, "%s", title);
    gfx::drawTextArgs(vg, textX, popY + h*0.75f, selected ? 17.0f : 16.0f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, descCol, "%s", desc);

    if (selected) {
        float arrowX = popX + popW - 25.0f + sinf(t * 8.0f) * 4.0f;
        gfx::drawTextArgs(vg, arrowX, popY+popH/2, 24.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE,
            nvgRGBA(255, 165, 0, 255), ">");
    }
}

void GooniesMainMenu::Draw(NVGcontext* vg, Theme* theme) {
    static float t = 0.0f;
    t += 0.016f;

    // Fondo elegante oscuro con gradiente
    NVGpaint bg = nvgLinearGradient(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
        nvgRGBA(12,15,25,255), nvgRGBA(5,8,15,255));
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    nvgFillPaint(vg, bg); nvgFill(vg);

    // Luces de fondo dinámicas
    NVGpaint light1 = nvgRadialGradient(vg, SCREEN_WIDTH*0.2f, SCREEN_HEIGHT*0.3f, 100.0f, 600.0f, nvgRGBA(255, 100, 30, 20), nvgRGBA(0,0,0,0));
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    nvgFillPaint(vg, light1); nvgFill(vg);

    NVGpaint light2 = nvgRadialGradient(vg, SCREEN_WIDTH*0.8f, SCREEN_HEIGHT*0.7f, 100.0f, 500.0f, nvgRGBA(50, 100, 255, 15), nvgRGBA(0,0,0,0));
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    nvgFillPaint(vg, light2); nvgFill(vg);

    DrawLogo(vg, SCREEN_WIDTH/2, 120, t);

    // --- Top Right Elements ---
    float trX = SCREEN_WIDTH - 40.0f;
    float trY = 40.0f;
    
    // Language
    long lang_idx = App::GetLanguage();
    const char* lang_str = (lang_idx == 1) ? "English" : "Español";
    
    // Draw the language label
    gfx::drawTextArgs(vg, trX - 10.0f, trY + 15.0f, 22.0f, NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE, nvgRGBA(220, 220, 220, 255), "Lang: %s", lang_str);
    
    // Version Pill
    float pillW = 80.0f;
    float pillH = 30.0f;
    float pillX = trX - pillW;
    float pillY = trY + 40.0f;
    
    nvgBeginPath(vg); nvgRoundedRect(vg, pillX, pillY, pillW, pillH, 15.0f);
    nvgFillColor(vg, nvgRGBA(30, 30, 35, 255)); nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(60, 60, 60, 255)); nvgStrokeWidth(vg, 1.5f); nvgStroke(vg);
    
    // Green dot
    nvgBeginPath(vg); nvgCircle(vg, pillX + 15.0f, pillY + pillH/2.0f, 4.0f);
    nvgFillColor(vg, nvgRGBA(0, 255, 100, 255)); nvgFill(vg);
    
    // Version text
    gfx::drawTextArgs(vg, pillX + 28.0f, pillY + pillH/2.0f, 18.0f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 255), "%s", APP_VERSION);
    // --------------------------

    float boxW = 540.0f;
    float boxH = 80.0f;
    float startX = (SCREEN_WIDTH - boxW) / 2.0f;
    float startY = 195.0f;
    float gapY = 10.0f;

    bool is_en = (App::GetLanguage() == 1);

    struct Opt { const char* icon; const char* title; const char* desc; };
    Opt options[5] = {
        { "USB", is_en ? "Install via MTP" : "Instalar por MTP", is_en ? "Connect USB cable to PC" : "Conectar cable USB al PC" }, 
        { "++", is_en ? "Installed Games" : "Juegos Instalados", is_en ? "Manage and delete games" : "Gestionar y borrar juegos" }, 
        { "[S]", is_en ? "Saves" : "Partidas Guardadas", is_en ? "Manage game saves" : "Gestionar partidas guardadas" },
        { "SD", is_en ? "Browse microSD" : "Explorar microSD", is_en ? "File browser (read-only)" : "Navegador de archivos (solo lectura)" },
        { "O", is_en ? "Settings" : "Ajustes", is_en ? "Configuration and system" : "Configuracion y sistema" } 
    };

    for (int i=0; i<5; ++i) {
        float py = startY + i*(boxH + gapY);
        bool sel = (m_selected_index == i);
        DrawPastilla(vg, startX, py, boxW, boxH,
            options[i].icon, options[i].title, options[i].desc, sel, theme, t);
    }
    
    // Credits
    const char* cred = is_en ? "Developed for the Switch ES community - The Goonies OS by GoodmanBCN" 
                             : "Desarrollado para la comunidad Switch ES - The Goonies OS por GoodmanBCN";
    gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT - 30.0f, 18.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(150, 150, 150, 255), cred);
}

} // namespace GooniesInstaller::frontend::menu::main
