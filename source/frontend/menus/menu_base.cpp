#include "app.hpp"
#include "log.hpp"
#include "frontend/menus/menu_base.hpp"
#include "frontend/nvg_util.hpp"
#include "i18n.hpp"

namespace GooniesInstaller::frontend::menu {

auto MenuBase::GetPolledData(bool force_refresh) -> PolledData {
    static PolledData data{};
    static TimeStamp timestamp{};
    static bool has_init = false;

    if (!has_init) {
        has_init = true;
        force_refresh = true;
    }

    // update every second, do this in Draw because Update() isn't called if it
    // doesn't have focus.
    if (force_refresh || timestamp.GetSeconds() >= 1) {
        data.tm = {};
        data.type = {};
        data.status = {};
        data.strength = {};
        data.ip = {};
        // avoid divide by zero if getting the size fails, for whatever reason.
        data.sd_free = 1;
        data.sd_total = 1;
        data.emmc_free = 1;
        data.emmc_total = 1;

        const auto t = std::time(NULL);
        localtime_r(&t, &data.tm);
        nifmGetInternetConnectionStatus(&data.type, &data.strength, &data.status);
        nifmGetCurrentIpAddress(&data.ip);

        App::GetSdSize(&data.sd_free, &data.sd_total);
        App::GetEmmcSize(&data.emmc_free, &data.emmc_total);

        timestamp.Update();
    }

    return data;
}

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetAction(Button::START, Action{App::Exit});
}

MenuBase::~MenuBase() {
}

void MenuBase::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
}

void MenuBase::Draw(NVGcontext* vg, Theme* theme) {
    NVGcolor bg = theme->GetColour(ThemeEntryID_BACKGROUND);
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    nvgFillColor(vg, bg); nvgFill(vg);

    Widget::Draw(vg, theme);

    const float start_y = 70;
    float bounds[4];

    nvgFontSize(vg, 28);
    gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());

    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const auto title_sub_x = 80 + (bounds[2] - bounds[0]) + 10;

    gfx::drawTextArgs(vg, 80, start_y, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    m_scroll_title_sub_heading.Draw(vg, true, title_sub_x, start_y, text_w - title_sub_x, 16, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading.c_str());
    m_scroll_sub_heading.Draw(vg, true, 80, 675, text_w - 160, 18, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
}

} // namespace GooniesInstaller::frontend::menu
