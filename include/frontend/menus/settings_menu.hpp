#pragma once

#include "frontend/widget.hpp"
#include <string>

namespace GooniesInstaller::frontend::menu {

struct SettingsMenu : Widget {
    SettingsMenu();
    ~SettingsMenu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    std::string m_fw_version;
    std::string m_app_version;
    s64 m_sd_free{};
    s64 m_sd_total{};
    int m_selected_index{};
};

} // namespace GooniesInstaller::frontend::menu
