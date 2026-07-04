#pragma once

#include "frontend/widget.hpp"

namespace GooniesInstaller { struct Theme; }

namespace GooniesInstaller::frontend::menu::main {

class GooniesMainMenu : public Widget {
public:
    GooniesMainMenu();
    ~GooniesMainMenu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    bool IsMenu() const override { return true; }

private:
    void DrawPastilla(NVGcontext* vg, float x, float y, float w, float h,
                      const char* icon, const char* title, const char* desc, bool selected, Theme* theme, float t);
    void DrawLogo(NVGcontext* vg, float cx, float cy, float t);

    int m_selected_index = 0;
    int m_logo_img = 0;
};

} // namespace GooniesInstaller::frontend::menu::main
