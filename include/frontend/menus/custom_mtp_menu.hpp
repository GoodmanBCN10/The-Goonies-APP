#pragma once

#include "frontend/widget.hpp"

namespace GooniesInstaller::frontend::menu::mtp {

class CustomMtpMenu : public Widget {
public:
    CustomMtpMenu();
    ~CustomMtpMenu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
};

} // namespace GooniesInstaller::frontend::menu::mtp
