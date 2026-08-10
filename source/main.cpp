#include <curl/curl.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <borealis.hpp>
#include <string>
#include <switch/runtime/devices/fs_dev.h>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include "app_state.hpp"
std::atomic<bool> g_appExiting{false};
#include <mutex>
#include <usbhsfs.h>
extern "C" {
#include <ipcext/es.h>
}
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "installer/installer_core.hpp"
#include "mtp/haze_helper.hpp"
#include "app/installed_title_service.hpp"
#include "app/homebrew_service.hpp"

#include "ui/catalog/catalog_view.hpp"
#include "linkuser.hpp"
#include "ui/main_menu.hpp"
#include "ui/common/ui_helpers.hpp"
#include "app/update_service.hpp"
#include "ui/theme.hpp"
#include <borealis/views/progress_spinner.hpp>

using pipensx::AppSettings;
using pipensx::CatalogService;
using pipensx::DownloadManager;
using pipensx::GameMetadataService;
using pipensx::InstalledTitleService;
using pipensx::HomebrewService;
using namespace pipensx::ui;

int main(int argc, char* argv[]) {
    std::string nroPath = "sdmc:/switch/thegoonies/TheGooniesInstaller.nro";
    if (argc > 0 && argv != nullptr) {
        if (argv[0] != nullptr && std::string(argv[0]).find("sdmc:/") == 0) {
            nroPath = argv[0];
        }
    }
    
    // Check if we are running as the temporary update staging file
    if (argc >= 3 && std::string(argv[1]) == "--finish-update") {
        std::string originalPath = argv[2];
        
        // Remove the original NRO
        unlink(originalPath.c_str());
        
        // Rename ourselves (.update) to the original path
        rename(nroPath.c_str(), originalPath.c_str());
        
        // Force the Switch OS FAT32 driver to commit changes to the SD card
        // This is CRITICAL. Without this, nx-hbloader reads stale filesystem cache,
        // causing the OS to panic (err:f) when loading the next NRO into memory.
        fsdevCommitDevice("sdmc");
        
        // Wait just in case the hardware needs a moment
        svcSleepThread(500000000ULL); // 0.5 seconds
        
        // Tell hbmenu to launch the newly replaced original NRO
        envSetNextLoad(originalPath.c_str(), originalPath.c_str());
        
        // Exit so the system starts the original app
        return 0;
    } else if (argc >= 2 && std::string(argv[0]) == "--finish-update") {
        // Fallback for older versions (e.g. v2.1.8) that passed incorrect argv[0]
        std::string originalPath = argv[1];
        std::string actualTempPath = originalPath + ".update";
        
        unlink(originalPath.c_str());
        rename(actualTempPath.c_str(), originalPath.c_str());
        
        fsdevCommitDevice("sdmc");
        svcSleepThread(500000000ULL); // 0.5 seconds
        
        envSetNextLoad(originalPath.c_str(), originalPath.c_str());
        
        return 0;
    }
    
    // If we just updated, clean up the temporary update file
    std::string tempUpdatePath = nroPath + ".update";
    if (access(tempUpdatePath.c_str(), F_OK) == 0) {
        unlink(tempUpdatePath.c_str());
    }
    
    pipensx::UpdateService updater(nroPath);
    updater.discardStaged();

    // Check if launched in Library Applet Mode (Album mode without Title Override)
    AppletType at = appletGetAppletType();
    if (at == AppletType_LibraryApplet || at == AppletType_OverlayApplet) {
        consoleInit(NULL);
        printf("\n====================================================\n");
        printf(" THE GOONIES APP REQUIERE ACCESO TOTAL A LA MEMORIA\n");
        printf(" THE GOONIES APP REQUIRES FULL MEMORY ACCESS\n");
        printf("====================================================\n\n");
        printf(" Por favor, abre cualquier juego manteniendo pulsado 'R'\n");
        printf(" para abrir el Homebrew Menu en modo Acceso Total.\n\n");
        printf(" Please launch any game while holding 'R' to open\n");
        printf(" the Homebrew Menu with full memory access.\n\n");
        printf(" Pulsa + o HOME para salir / Press + or HOME to exit.\n");
        printf("====================================================\n");
        consoleUpdate(NULL);
        
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & HidNpadButton_Plus) break;
            svcSleepThread(50000000ULL);
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
    bool setsysReady = false;

    std::ofstream logOut("sdmc:/switch/thegoonies/debug_log.txt", std::ios::out | std::ios::trunc);
    std::mutex logMutex;
    auto writeLog = [&](const std::string& msg) {
        std::lock_guard<std::mutex> lock(logMutex);
        logOut << msg << std::endl;
        logOut.flush();
        brls::Logger::info("{}", msg);
    };

    writeLog("Starting app");

    Result usbRc = usbHsFsInitialize(0);
    if (R_SUCCEEDED(usbRc)) {
        writeLog("usbHsFsInitialize OK");
    } else {
        writeLog("usbHsFsInitialize FAILED (non-fatal)");
    }

    if (R_SUCCEEDED(romfsInit())) {
        writeLog("romfsInit OK");
    } else {
        writeLog("romfsInit FAILED");
    }

    bool nvReady = false;
    Result nvRc = nvInitialize();
    if (R_SUCCEEDED(nvRc)) {
        nvReady = true;
        writeLog("nvInitialize OK");
    } else {
        writeLog("nvInitialize FAILED (non-fatal)");
    }

    std::FILE* borealisLogFile = std::fopen("sdmc:/switch/thegoonies/borealis_log.txt", "w");
    if (borealisLogFile) {
        setvbuf(borealisLogFile, NULL, _IONBF, 0);
        brls::Logger::setLogOutput(borealisLogFile);
    }

    try {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

        // 1. Initialize Settings and Language IMMEDIATELY
        const char* BundledCatalogPath = "romfs:/catalog/switch_games.json";
        AppSettings settings("sdmc:/switch/thegoonies/settings.json", BundledCatalogPath);
        std::string loadError;
        settings.load(loadError);
        writeLog("settings.load OK");
        
        if (settings.get().language == 1) {
            brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ES;
        } else if (settings.get().language == 2) {
            brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
        } else if (settings.get().language == 3) {
            brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_PT_BR;
        } else {
            brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ES; // Default to ES initially
        }
        
        // 2. Initialize Borealis Window IMMEDIATELY (Instant startup under 1 second)
        appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
        NWindow* win = nwindowGetDefault();
        if (win) {
            nwindowSetDimensions(win, 1280, 720);
        }

        if (!brls::Application::init()) {
            throw std::runtime_error("Unable to init Borealis application");
        }
        writeLog("brls::Application::init OK");

        pipensx::ui::theme::registerColors();

        brls::Application::createWindow("The Goonies APP");
        brls::Application::setGlobalQuit(true);
        brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        writeLog("createWindow OK");

        // 3. Initialize Sockets & System Services in background after window is visible
        if (R_SUCCEEDED(socketInitializeDefault())) {
            socketReady = true;
            writeLog("socketInitialize OK");
        } else {
            writeLog("socketInitialize FAILED (non-fatal)");
        }

        Result usbRc = usbHsFsInitialize(0);
        if (R_SUCCEEDED(usbRc)) {
            writeLog("usbHsFsInitialize OK");
        } else {
            writeLog("usbHsFsInitialize FAILED (non-fatal)");
        }

        CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curlResult == CURLE_OK) {
            curlReady = true;
        }

        Result rc = ncmInitialize();
        if (R_SUCCEEDED(rc)) {
            ncmReady = true;
            writeLog("ncmInitialize OK");
        } else {
            writeLog("ncmInitialize FAILED (non-fatal)");
        }

        rc = nsInitialize();
        if (R_SUCCEEDED(rc)) {
            nsReady = true;
            writeLog("nsInitialize OK");
        } else {
            writeLog("nsInitialize FAILED (non-fatal)");
        }

        rc = esInitialize();
        if (R_SUCCEEDED(rc)) {
            esReady = true;
            writeLog("esInitialize OK");
        } else {
            writeLog("esInitialize FAILED (non-fatal)");
        }

        rc = accountInitialize(AccountServiceType_System);
        if (R_FAILED(rc)) {
            rc = accountInitialize(AccountServiceType_Application);
        }
        bool accountReady = R_SUCCEEDED(rc);
        if (accountReady) writeLog("accountInitialize OK");

        rc = setsysInitialize();
        setsysReady = R_SUCCEEDED(rc);
        if (setsysReady) {
            writeLog("setsysInitialize OK");
            setsysSetUsb30EnableFlag(settings.get().enableUsb30);
            if (settings.get().enableUsb30) {
                writeLog("USB set to 3.0");
            } else {
                writeLog("USB forced to 2.0");
            }
        }
        
        DownloadManager* download_manager = new DownloadManager("sdmc:/switch/thegoonies");
        CatalogService* catalog_service = new CatalogService("sdmc:/switch/thegoonies", BundledCatalogPath);
        GameMetadataService* metadata_service = new GameMetadataService("sdmc:/switch/thegoonies");
        InstalledTitleService* installed_service = new InstalledTitleService("sdmc:/switch/thegoonies");
        HomebrewService* homebrew_service = new HomebrewService();
        writeLog("Services constructed OK");

        // Load installed games in the background thread to avoid blocking boot

        // Push a loading screen to prevent black screen
        brls::Box* loadingBox = new brls::Box(brls::Axis::COLUMN);
        loadingBox->setAlignItems(brls::AlignItems::CENTER);
        loadingBox->setJustifyContent(brls::JustifyContent::CENTER);
        
        brls::Label* loadingLabel = new brls::Label();
        loadingLabel->setText(t("Iniciando The Goonies APP...\nBuscando juegos instalados...", 
                                "Starting The Goonies APP...\nFinding installed games...", 
                                "Iniciando The Goonies APP...\nBuscando jogos instalados..."));
        loadingLabel->setFontSize(24);
        loadingLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        loadingLabel->setMarginBottom(40);
        
        loadingBox->addView(loadingLabel);
        
        brls::Application::pushActivity(new brls::Activity(loadingBox));
        writeLog("pushActivity LoadingScreen OK");

        std::thread initThread([&]() {
            std::string err;
            if (g_appExiting) return;
            installed_service->refresh(err);
            if (g_appExiting) return;

            brls::sync([&]() {
                if (g_appExiting) return;
                brls::Application::popActivity(); // Pop LoadingScreen
                
                goonies::ui::MainMenu* rootFrame = new goonies::ui::MainMenu(
                    download_manager, catalog_service, metadata_service, 
                    installed_service, &settings, homebrew_service, &updater);
                brls::Application::pushActivity(new brls::Activity(rootFrame));
                
                // Show language selection dialog on first run
                if (settings.get().language == 0) {
                    brls::Dialog* langDialog = new brls::Dialog("Selecciona tu idioma / Select your language");
                    langDialog->addButton("Español", [&]() {
                        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ES;
                        auto vals = settings.get();
                        vals.language = 1;
                        std::string updateErr;
                        settings.update(vals, updateErr);
                        brls::Application::notify("Idioma guardado: Español. Reinicia la app para aplicar.");
                    });
                    langDialog->addButton("English", [&]() {
                        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
                        auto vals = settings.get();
                        vals.language = 2;
                        std::string updateErr;
                        settings.update(vals, updateErr);
                        brls::Application::notify("Language saved: English. Restart app to apply.");
                    });
                    langDialog->addButton("Português", [&]() {
                        brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_PT_BR;
                        auto vals = settings.get();
                        vals.language = 3;
                        std::string updateErr;
                        settings.update(vals, updateErr);
                        brls::Application::notify("Idioma guardado: Português. Reinicie o aplicativo para aplicar.");
                    });
                    langDialog->open();
                }
            });

            // Load shop catalog and metadata in background after UI appears
            if (g_appExiting) return;
            catalog_service->load(err);
            if (g_appExiting) return;
            metadata_service->load(err);
        });

        brls::Application::getExitEvent()->subscribe([] {
            g_appExiting = true;
        });

        // Run the main loop
        int frameCount = 0;
        while (brls::Application::mainLoop()) {
            frameCount++;
            if (frameCount % 60 == 1) {
                writeLog("Main: pumping UI loop, frame " + std::to_string(frameCount));
            }
        }
        g_appExiting = true;
        writeLog("Main loop EXITED. Application closing normally.");
        
        download_manager->shutdown();

        if (initThread.joinable()) {
            initThread.join();
        }

        // Gracefully shutdown background threads before local services are destroyed
        // MTP::Exit();

    } catch (const std::exception& e) {
        std::string errMsg = std::string("Fatal error: ") + e.what();
        writeLog(errMsg);
        consoleInit(NULL);
        printf("\n====================================================\n");
        printf(" ERROR AL INICIAR / FATAL ERROR\n");
        printf("====================================================\n\n");
        printf(" Details: %s\n\n", e.what());
        printf(" Log guardado en / Log file saved at:\n");
        printf(" sdmc:/switch/thegoonies/debug_log.txt\n\n");
        printf(" Pulsa + o HOME para salir / Press + or HOME to exit.\n");
        printf("====================================================\n");
        consoleUpdate(NULL);
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & HidNpadButton_Plus) break;
            svcSleepThread(50000000ULL);
        }
        consoleExit(NULL);
    } catch (...) {
        writeLog("Unknown fatal error caught");
        consoleInit(NULL);
        printf("\n====================================================\n");
        printf(" ERROR DESCONOCIDO / UNKNOWN FATAL ERROR\n");
        printf("====================================================\n\n");
        printf(" Log guardado en / Log file saved at:\n");
        printf(" sdmc:/switch/thegoonies/debug_log.txt\n\n");
        printf(" Pulsa + o HOME para salir / Press + or HOME to exit.\n");
        printf("====================================================\n");
        consoleUpdate(NULL);
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & HidNpadButton_Plus) break;
            svcSleepThread(50000000ULL);
        }
        consoleExit(NULL);
    } 

    // Cleanup
    if (nvReady) nvExit();
    usbHsFsExit();
    if (nsReady) nsExit();
    if (ncmReady) ncmExit();
    if (esReady) esExit();
    accountExit();
    if (setsysReady) setsysExit();
    if (curlReady) curl_global_cleanup();
    if (socketReady) socketExit();
    romfsExit();

    // Exit
    if (pipensx::linkuser::g_shouldReboot) {
        pipensx::linkuser::rebootSystem();
    }
    return EXIT_SUCCESS;
}
