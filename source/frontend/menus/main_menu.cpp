#include "frontend/menus/main_menu.hpp"

#include "frontend/sidebar.hpp"
#include "frontend/popup_list.hpp"
#include "frontend/option_box.hpp"
#include "frontend/progress_box.hpp"
#include "frontend/error_box.hpp"

#include "frontend/menus/homebrew.hpp"
#include "frontend/menus/filebrowser.hpp"
#include "frontend/menus/irs_menu.hpp"
#include "frontend/menus/themezer.hpp"
#include "frontend/menus/ghdl.hpp"
#include "frontend/menus/usb_menu.hpp"
#include "frontend/menus/ftp_menu.hpp"
#include "frontend/menus/mtp_menu.hpp"
#include "frontend/menus/gc_menu.hpp"
#include "frontend/menus/game_menu.hpp"
#include "frontend/menus/save_menu.hpp"
#include "frontend/menus/appstore.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"

#include <cstring>
#include <yyjson.h>
#include <iomanip>

namespace GooniesInstaller::frontend::menu::main {
namespace {

constexpr const char* GITHUB_URL{"https://api.github.com/repos/ITotalJustice/GooniesInstaller/releases/latest"};
constexpr fs::FsPath CACHE_PATH{"/switch/GooniesInstaller/cache/GooniesInstaller_latest.json"};

// paths where GooniesInstaller can be installed, used when updating
constexpr const fs::FsPath GOONIES_PATHS[]{
    "/hbmenu.nro",
    "/switch/GooniesInstaller.nro",
    "/switch/GooniesInstaller/GooniesInstaller.nro",
};

template<typename T>
auto MiscMenuFuncGenerator(u32 flags) {
    return std::make_unique<T>(flags);
}

const MiscMenuEntry MISC_MENU_ENTRIES[] = {
    { .name = "Games", .title = "Games", .func = MiscMenuFuncGenerator<frontend::menu::game::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "View all installed games. "
        "In this menu you can launch, backup, create savedata and much more." },

    { .name = "FileBrowser", .title = "FileBrowser", .func = MiscMenuFuncGenerator<frontend::menu::filebrowser::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "Browse files on you SD Card. "
        "You can move, copy, delete, extract zip, create zip, upload and much more.\n\n"
        "A connected USB/HDD can be opened by mounting it in the advanced options." },

    { .name = "Saves", .title = "Saves", .func = MiscMenuFuncGenerator<frontend::menu::save::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "Manage your save games." },

#ifdef ENABLE_LIBHAZE
    { .name = "MTP", .title = "MTP Install", .func = MiscMenuFuncGenerator<frontend::menu::mtp::Menu>, .flag = MiscMenuFlag_Install, .info =
        "Install apps via MTP." },
#endif // ENABLE_LIBHAZE
};


auto InstallUpdate(ProgressBox* pbox, const std::string url, const std::string version) -> Result {
    static fs::FsPath zip_out{"/switch/GooniesInstaller/cache/update.zip"};

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    // 1. download the zip
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer(i18n::Reorder("Downloading ", version));
        log_write("starting download: %s\n", url.c_str());

        const auto result = curl::Api().ToFile(
            curl::Url{url},
            curl::Path{zip_out},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        R_UNLESS(result.success, Result_MainFailedToDownloadUpdate);
    }

    ON_SCOPE_EXIT(fs.DeleteFile(zip_out));

    // 2. extract the zip
    if (!pbox->ShouldExit()) {
        const auto exe_path = App::GetExePath();
        bool found_exe{};

        R_TRY(thread::TransferUnzipAll(pbox, zip_out, &fs, "/", [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
            if (std::strstr(path, "GooniesInstaller.nro")) {
                path = exe_path;
                found_exe = true;
            }
            return true;
        }));

        // check if we have GooniesInstaller installed in other locations and update them.
        if (found_exe) {
            for (auto& path : GOONIES_PATHS) {
                log_write("[UPD] checking path: %s\n", path.s);
                // skip if we already updated this path.
                if (exe_path == path) {
                    log_write("[UPD] skipped as already updated\n");
                    continue;
                }

                // check that this is really GooniesInstaller.
                log_write("[UPD] checking nacp\n");
                NacpStruct nacp;
                if (R_SUCCEEDED(nro_get_nacp(path, nacp)) && !std::strcmp(nacp.lang[0].name, "GooniesInstaller")) {
                    log_write("[UPD] found, updating\n");
                    pbox->NewTransfer(path);
                    R_TRY(pbox->CopyFile(&fs, exe_path, path));
                }
            }
        }
    }

    log_write("finished update :)\n");
    R_SUCCEED();
}

auto CreateCenterMenu(std::string& name_out) -> std::unique_ptr<MenuBase> {
    const auto name = App::GetApp()->m_center_menu.Get();

    for (auto& e : GetMenuMenuEntries()) {
        if (e.name == name) {
            name_out = name;
            return e.func(MenuFlag_Tab);
        }
    }

    name_out = "FileBrowser";
    return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
}

auto CreateLeftSideMenu(std::string_view center_name, std::string& name_out) -> std::unique_ptr<MenuBase> {
    const auto name = App::GetApp()->m_left_menu.Get();

    // handle if the user tries to mount the same menu twice.
    if (name == center_name) {
        if (center_name != "FileBrowser") {
            return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
        } else {
#ifdef ENABLE_LIBHAZE
            return std::make_unique<frontend::menu::mtp::Menu>(MenuFlag_Tab);
#else
            return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
#endif
        }
    }

    for (auto& e : GetMenuMenuEntries()) {
        if (e.name == name) {
            name_out = name;
            return e.func(MenuFlag_Tab);
        }
    }

    name_out = "FileBrowser";
    return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
}

// todo: handle center / left menu being the same.
auto CreateRightSideMenu(std::string_view left_name) -> std::unique_ptr<MenuBase> {
    const auto name = App::GetApp()->m_right_menu.Get();

    // handle if the user tries to mount the same menu twice.
    if (name == left_name) {
        if (left_name != "FileBrowser") {
            return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
        } else {
#ifdef ENABLE_LIBHAZE
            return std::make_unique<frontend::menu::mtp::Menu>(MenuFlag_Tab);
#else
            return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
#endif
        }
    }

    for (auto& e : GetMenuMenuEntries()) {
        if (e.name == name) {
            return e.func(MenuFlag_Tab);
        }
    }

    return std::make_unique<frontend::menu::filebrowser::Menu>(MenuFlag_Tab);
}

} // namespace

auto GetMenuMenuEntries() -> std::span<const MiscMenuEntry> {
    return MISC_MENU_ENTRIES;
}

MainMenu::MainMenu() {

    this->SetActions(
        std::make_pair(Button::B, Action{"Atras", [this](){
            SetPop();
        }})
    );

    std::string center_name;
    m_centre_menu = CreateCenterMenu(center_name);
    m_current_menu = m_centre_menu.get();

    for (auto [button, action] : m_actions) {
        m_current_menu->SetAction(button, action);
    }
}

MainMenu::~MainMenu() {

}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    m_current_menu->Update(controller, touch);
    if (m_current_menu->ShouldPop()) {
        this->SetPop();
    }
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    m_current_menu->Draw(vg, theme);
}

void MainMenu::OnFocusGained() {
    Widget::OnFocusGained();
    m_current_menu->OnFocusGained();
}

void MainMenu::OnFocusLost() {
    Widget::OnFocusLost();
    m_current_menu->OnFocusLost();
}

void MainMenu::OnLRPress(MenuBase* menu, Button b) {
    m_current_menu->OnFocusLost();
    if (m_current_menu == m_centre_menu.get()) {
        m_current_menu = menu;
        RemoveAction(b);
    } else {
        m_current_menu = m_centre_menu.get();
    }

    AddOnLRPress();
    m_current_menu->OnFocusGained();

    for (auto [button, action] : m_actions) {
        m_current_menu->SetAction(button, action);
    }
}

void MainMenu::AddOnLRPress() {
    if (m_current_menu != m_left_menu.get()) {
        const auto label = m_current_menu == m_centre_menu.get() ? m_left_menu->GetShortTitle() : m_centre_menu->GetShortTitle();
        SetAction(Button::L, Action{i18n::get(label), [this]{
            OnLRPress(m_left_menu.get(), Button::L);
        }});
    }

    if (m_current_menu != m_right_menu.get()) {
        const auto label = m_current_menu == m_centre_menu.get() ? m_right_menu->GetShortTitle() : m_centre_menu->GetShortTitle();
        SetAction(Button::R, Action{i18n::get(label), [this]{
            OnLRPress(m_right_menu.get(), Button::R);
        }});
    }
}

} // namespace GooniesInstaller::frontend::menu::main
