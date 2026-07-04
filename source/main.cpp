#include <switch.h>
#include <memory>
#include "app.hpp"
#include "log.hpp"
#include "frontend/menus/goonies_main_menu.hpp"

int main(int argc, char** argv) {
    if (!argc || !argv) {
        return 1;
    }

    auto app = std::make_unique<GooniesInstaller::App>(argv[0]);
    app->Push<GooniesInstaller::frontend::menu::main::GooniesMainMenu>();
    app->Loop();
    return 0;
}

extern "C" {

void userAppInit(void) {
    GooniesInstaller::App::SetBoostMode(true);

    GooniesInstaller::App::SetBoostMode(true);

    const auto is_application = GooniesInstaller::App::IsApplication();

    Result rc;
    if (R_FAILED(rc = appletLockExit())) {}
        // diagAbortWithResult(rc);
    if (R_FAILED(rc = plInitialize(PlServiceType_User))) {}
        // diagAbortWithResult(rc);
    if (R_FAILED(rc = accountInitialize(is_application ? AccountServiceType_Application : AccountServiceType_System))) {}
        // diagAbortWithResult(rc);
    if (R_FAILED(rc = setInitialize())) {}
        // diagAbortWithResult(rc);
    if (R_FAILED(rc = hidsysInitialize())) {}
        // diagAbortWithResult(rc);
    if (R_FAILED(rc = ncmInitialize())) {}
        // diagAbortWithResult(rc);

    // it doesn't matter if this fails.
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
}

void userAppExit(void) {
    ncmExit();
    hidsysExit();
    setExit();
    accountExit();
    plExit();
    // NOTE (DMC): prevents exfat corruption.
    if (auto fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }

    GooniesInstaller::App::SetBoostMode(false);
    appletUnlockExit();
}

} // extern "C"
