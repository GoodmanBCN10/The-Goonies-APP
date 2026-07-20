#include <curl/curl.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <borealis.hpp>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include <mutex>
#include <usbhsfs.h>
extern "C" {
#include <ipcext/es.h>
    u32 __nx_applet_type = AppletType_None;
    u32 __nx_applet_ext_variant = 0;
}
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/installed_title_service.hpp"
#include "app/homebrew_service.hpp"

#include "ui/catalog/catalog_view.hpp"
#include "ui/main_menu.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"

using pipensx::AppSettings;
using pipensx::CatalogService;
using pipensx::DownloadManager;
using pipensx::GameMetadataService;
using pipensx::InstalledTitleService;
using pipensx::HomebrewService;
using namespace pipensx::ui;




int main(int argc, char* argv[]) {
    // A library applet must only terminate after qlaunch asks it to close.
    if (appletGetAppletType() != AppletType_Application && appletGetAppletType() != AppletType_SystemApplication) {
        consoleInit(NULL);
        printf("\nThe Goonies Installer requires full memory access.\n\n");
        printf("Please launch a game while holding 'R' to open the Homebrew Menu,\n");
        printf("or install a forwarder NSP.\n\n");
        printf("Press HOME to exit.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            svcSleepThread(100000000ULL);
        }
        consoleExit(NULL);
        return 0;
    }

    mkdir("sdmc:/switch", 0755);
    mkdir("sdmc:/switch/thegoonies", 0755);

    bool curlReady = false;
    bool ncmReady = false;
    bool nsReady = false;
    bool esReady = false;
    bool socketReady = false;

    std::ofstream logOut("sdmc:/switch/thegoonies/debug_log.txt", std::ios::out | std::ios::trunc);
    std::mutex logMutex;
    auto writeLog = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(logMutex);
        logOut << msg << std::endl;
        logOut.flush();
        brls::Logger::info("{}", msg);
    };

    writeLog("Starting app");

    usbHsFsInitialize(0);
    writeLog("usbHsFsInitialize OK");

    if (R_SUCCEEDED(romfsInit())) {
        writeLog("romfsInit OK");
    } else {
        writeLog("romfsInit FAILED");
    }

    if (R_SUCCEEDED(socketInitializeDefault())) {
        socketReady = true;
        writeLog("socketInitialize OK");
    } else {
        writeLog("socketInitialize FAILED");
    }

    std::FILE* borealisLogFile = std::fopen("sdmc:/switch/thegoonies/borealis_log.txt", "w");
    if (borealisLogFile) {
        setvbuf(borealisLogFile, NULL, _IONBF, 0);
        brls::Logger::setLogOutput(borealisLogFile);
    }

    // Init logger
    try {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        
        CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curlResult != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed");
        }
        curlReady = true;

        Result rc = ncmInitialize();
        if (R_FAILED(rc)) throw std::runtime_error("ncmInitialize failed");
        ncmReady = true;

        writeLog("Starting app");

        rc = nsInitialize();
        if (R_FAILED(rc)) throw std::runtime_error("nsInitialize failed");
        nsReady = true;
        writeLog("nsInitialize OK");

        rc = esInitialize();
        if (R_FAILED(rc)) throw std::runtime_error("esInitialize failed");
        esReady = true;
        writeLog("esInitialize OK");

        rc = accountInitialize(AccountServiceType_System);
        if (R_FAILED(rc)) {
            rc = accountInitialize(AccountServiceType_Application);
        }
        bool accountReady = R_SUCCEEDED(rc);
        if (accountReady) writeLog("accountInitialize OK");
        else writeLog("accountInitialize FAILED");

        // Init application
        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
        if (!brls::Application::init()) {
            throw std::runtime_error("Unable to init Borealis application");
        }
        writeLog("brls::Application::init OK");

        pipensx::ui::theme::registerColors();

        brls::Application::createWindow("The Goonies Installer");
        brls::Application::setGlobalQuit(true);
        brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        writeLog("createWindow OK");

        // Init Pipensx Services
        const char* BundledCatalogPath = "romfs:/catalog/switch_games.json";
        AppSettings settings("sdmc:/switch/thegoonies", BundledCatalogPath);
        std::string loadError;
        settings.load(loadError);
        writeLog("settings.load OK");
        
        DownloadManager download_manager("sdmc:/switch/thegoonies");
        CatalogService catalog_service("sdmc:/switch/thegoonies", BundledCatalogPath);
        GameMetadataService metadata_service("sdmc:/switch/thegoonies");
        InstalledTitleService installed_service("sdmc:/switch/thegoonies");
        HomebrewService homebrew_service;
        writeLog("Services constructed OK");

        // Push the activity before loading heavy services so we can pump the UI loop
        goonies::ui::MainMenu* rootFrame = new goonies::ui::MainMenu(&download_manager, &catalog_service, &metadata_service, &installed_service, &settings, &homebrew_service);
        brls::Application::pushActivity(new brls::Activity(rootFrame));
        writeLog("pushActivity OK");

        writeLog("Loading catalog_service...");
        std::string err;
        catalog_service.load(err);

        writeLog("Loading metadata_service...");
        metadata_service.load(err);

        writeLog("Refreshing installed_service...");
        installed_service.refresh(err);

        // Run the main loop
        int frameCount = 0;
        while (brls::Application::mainLoop()) {
            frameCount++;
            if (frameCount % 60 == 1) {
                writeLog("Main: pumping UI loop, frame " + std::to_string(frameCount));
            }
        }
        writeLog("Main loop EXITED. Application closing normally.");

    } catch (const std::exception& e) {
        brls::Logger::error("Fatal error: %s", e.what());
    } catch (...) {
        brls::Logger::error("Unknown fatal error");
    }

    // Cleanup
    usbHsFsExit();
    if (nsReady) nsExit();
    if (ncmReady) ncmExit();
    if (esReady) esExit();
    accountExit();
    if (curlReady) curl_global_cleanup();
    if (socketReady) socketExit();
    romfsExit();

    // Exit
    return EXIT_SUCCESS;
}
