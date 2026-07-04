#include "frontend/menus/custom_mtp_menu.hpp"
#include "app.hpp"
#include "frontend/nvg_util.hpp"

namespace GooniesInstaller::frontend::menu::mtp {

CustomMtpMenu::CustomMtpMenu() {
    this->SetPos(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetAction(Button::B, Action{"Atras", [this](){ SetPop(); }});
}

CustomMtpMenu::~CustomMtpMenu() {
}

void CustomMtpMenu::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
}

void CustomMtpMenu::Draw(NVGcontext* vg, Theme* theme) {
    NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, SCREEN_HEIGHT,
        nvgRGBA(8,10,18,255), nvgRGBA(15,20,38,255));
    nvgBeginPath(vg); nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    nvgFillPaint(vg, bg); nvgFill(vg);

    float cx = SCREEN_WIDTH / 2.0f;
    float cy = SCREEN_HEIGHT / 2.0f;

    gfx::drawTextArgs(vg, cx, cy - 20, 32.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(255,255,255,255), "Servidor MTP Activo");
    gfx::drawTextArgs(vg, cx, cy + 20, 20.0f, NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE, nvgRGBA(200,200,200,255), "Conecta el cable USB al PC");
}

} // namespace GooniesInstaller::frontend::menu::mtp
