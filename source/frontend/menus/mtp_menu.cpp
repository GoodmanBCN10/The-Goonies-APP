#include "frontend/menus/mtp_menu.hpp"
#include "backend_usb/usbds.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "frontend/nvg_util.hpp"
#include "i18n.hpp"
#include "haze_helper.hpp"

namespace GooniesInstaller::frontend::menu::mtp {

Menu::Menu(u32 flags) : stream::Menu{"MTP Install"_i18n, flags} {
    m_was_mtp_enabled = libhaze::IsInit();
    if (!m_was_mtp_enabled) {
        log_write("[MTP] wasn't enabled, forcefully enabling\n");
        libhaze::Init();
    }

    libhaze::InitInstallMode(
        [this](const char* path){ return OnInstallStart(path); },
        [this](const void *buf, size_t size){ return OnInstallWrite(buf, size); },
        [this](){ return OnInstallClose(); }
    );

    // Disable options menu since it is not needed here
    RemoveAction(Button::X);
}

Menu::~Menu() {
    // signal for thread to exit and wait.
    libhaze::DisableInstallMode();

    if (!m_was_mtp_enabled) {
        log_write("[MTP] disabling on exit\n");
        libhaze::Exit();
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    stream::Menu::Update(controller, touch);

    if (controller->GotDown(Button::DOWN) || controller->GotHeld(Button::DOWN)) {
        m_scroll_index++;
    } else if (controller->GotDown(Button::UP) || controller->GotHeld(Button::UP)) {
        m_scroll_index--;
    }

    int max_scroll = static_cast<int>(m_history.size()) - 11;
    if (max_scroll < 0) max_scroll = 0;
    
    if (m_scroll_index < 0) m_scroll_index = 0;
    if (m_scroll_index > max_scroll) m_scroll_index = max_scroll;

    static TimeStamp poll_ts;
    if (poll_ts.GetSeconds() >= 1) {
        poll_ts.Update();

        UsbState state{UsbState_Detached};
        usbDsGetState(&state);

        UsbDeviceSpeed speed{(UsbDeviceSpeed)UsbDeviceSpeed_None};
        usbDsGetSpeed(&speed);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "State: %s | Speed: %s"_i18n.c_str(), i18n::get(GetUsbDsStateStr(state)).c_str(), i18n::get(GetUsbDsSpeedStr(speed)).c_str());
        SetSubHeading(buf);
    }
}

void Menu::OnDisableInstallMode() {
    libhaze::DisableInstallMode();
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, SCREEN_HEIGHT,
        nvgRGBA(8,10,18,255), nvgRGBA(15,20,38,255));
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    nvgFillPaint(vg, bg); nvgFill(vg);

    float cx = SCREEN_WIDTH / 2.0f;
    
    // TOP PILL (Fixed)
    float top_pill_x = 100.f;
    float top_pill_y = 40.f;
    float top_pill_w = SCREEN_WIDTH - 200.f;
    float top_pill_h = 130.f;

    UsbState state{UsbState_Detached};
    usbDsGetState(&state);

    if (m_state != stream::State::Progress) {
        NVGcolor orange = theme->GetColour(ThemeEntryID_TEXT_SELECTED);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, top_pill_x, top_pill_y, top_pill_w, top_pill_h, 15.f);
        nvgFillColor(vg, nvgRGBAf(orange.r, orange.g, orange.b, 0.15f));
        nvgFill(vg);

        nvgStrokeColor(vg, nvgRGBAf(orange.r, orange.g, orange.b, 0.40f));
        nvgStrokeWidth(vg, 2.f);
        nvgStroke(vg);

        if (state != UsbState_Configured) {
            gfx::drawTextArgs(vg, cx, top_pill_y + top_pill_h / 2.f, 32.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(255,100,100,255), "Conectar cable USB ahora");
        } else {
            gfx::drawTextArgs(vg, cx, top_pill_y + top_pill_h / 2.f, 32.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(200,200,200,255), "Esperando archivos...");
        }
    }

    // BOTTOM PILL (Fixed)
    float pill_x = 100.f;
    float pill_y = 200.f;
    float pill_w = SCREEN_WIDTH - 200.f;
    float pill_h = SCREEN_HEIGHT - pill_y - 100.f;

    NVGcolor orange_bot = theme->GetColour(ThemeEntryID_TEXT_SELECTED);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, pill_x, pill_y, pill_w, pill_h, 15.f);
    nvgFillColor(vg, nvgRGBAf(orange_bot.r, orange_bot.g, orange_bot.b, 0.15f));
    nvgFill(vg);

    nvgStrokeColor(vg, nvgRGBAf(orange_bot.r, orange_bot.g, orange_bot.b, 0.40f));
    nvgStrokeWidth(vg, 2.f);
    nvgStroke(vg);

    gfx::drawTextArgs(vg, cx, pill_y + 30.f, 24.f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 255), "Archivos recibidos");

    nvgBeginPath(vg);
    nvgMoveTo(vg, pill_x + 20, pill_y + 50);
    nvgLineTo(vg, pill_x + pill_w - 20, pill_y + 50);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 50));
    nvgStrokeWidth(vg, 2.f);
    nvgStroke(vg);

    if (m_history.empty()) {
        gfx::drawTextArgs(vg, cx, pill_y + pill_h / 2.f, 24.f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(150, 150, 150, 255), "No se han recibido archivos todavia");
    } else {
        float list_y = pill_y + 80.f;
        int max_items = 11;
        int start_idx = (int)m_history.size() - 1 - m_scroll_index;
        if (start_idx < 0) start_idx = 0;

        for (int i = start_idx; i >= 0; i--) {
            const auto& entry = m_history[i];
            std::string basename = entry.name;
            auto pos = basename.find_last_of('/');
            if (pos != std::string::npos) basename = basename.substr(pos + 1);

            char size_str[64] = "";
            if (entry.size > 1024LL * 1024LL * 1024LL) {
                std::snprintf(size_str, sizeof(size_str), " (%.1f GB)", (float)entry.size / (1024.f * 1024.f * 1024.f));
            } else if (entry.size > 0) {
                std::snprintf(size_str, sizeof(size_str), " (%.1f MB)", (float)entry.size / (1024.f * 1024.f));
            } else {
                std::snprintf(size_str, sizeof(size_str), " (0 MB)");
            }
            std::string full_name = basename + size_str;

            NVGcolor color = entry.success ? nvgRGBA(100, 255, 100, 255) : nvgRGBA(255, 100, 100, 255);
            gfx::drawTextArgs(vg, pill_x + 30.f, list_y, 20.f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, color, "%s", entry.success ? "[OK]" : "[ERROR]");
            gfx::drawTextArgs(vg, pill_x + 110.f, list_y, 20.f, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 255), "%s", full_name.c_str());
            list_y += 30.f;
            if (list_y > pill_y + pill_h - 20.f) break;
        }

        // Draw scrollbar if needed
        if (m_history.size() > max_items) {
            float scrollbar_h = pill_h - 100.f;
            float scroll_y = pill_y + 80.f - 10.f;
            float scroll_thumb_h = scrollbar_h * ((float)max_items / m_history.size());
            float scroll_thumb_y = scroll_y + (scrollbar_h - scroll_thumb_h) * ((float)m_scroll_index / (m_history.size() - max_items));

            nvgBeginPath(vg);
            nvgRoundedRect(vg, pill_x + pill_w - 20.f, scroll_y, 8.f, scrollbar_h, 4.f);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 20));
            nvgFill(vg);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, pill_x + pill_w - 20.f, scroll_thumb_y, 8.f, scroll_thumb_h, 4.f);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 80));
            nvgFill(vg);
        }
    }
}

} // namespace GooniesInstaller::frontend::menu::mtp
