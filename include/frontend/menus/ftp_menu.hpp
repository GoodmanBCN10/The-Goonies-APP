#pragma once

#include "frontend/menus/menu_base.hpp"
#include "frontend/menus/install_stream_menu_base.hpp"

namespace GooniesInstaller::frontend::menu::ftp {

struct Menu final : stream::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "FTP"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnDisableInstallMode() override;

private:
    const char* m_user{};
    const char* m_pass{};
    unsigned m_port{};
    bool m_anon{};
    bool m_was_ftp_enabled{};
};

} // namespace GooniesInstaller::frontend::menu::ftp
