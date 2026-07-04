#pragma once

#include "frontend/menus/install_stream_menu_base.hpp"

namespace GooniesInstaller::frontend::menu::mtp {

struct Menu final : stream::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "MTP"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnDisableInstallMode() override;

private:
    bool m_was_mtp_enabled{};
    int m_scroll_index{};
};

} // namespace GooniesInstaller::frontend::menu::mtp
