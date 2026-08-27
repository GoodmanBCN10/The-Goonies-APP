#include <curl/curl.h>
#include <borealis.hpp>
#include <SDL2/SDL_mixer.h>
#include <atomic>
std::atomic<bool> g_appExiting = false;
#include <switch.h>
#include <string>
#include <set>
#include <map>

#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/simple_download_manager.hpp"
#include "app/game_metadata_service.hpp"

#include "ui/theme.hpp"
#include "ui/main_frame.hpp"
#include "ui/ports/ports_grid_view.hpp"
#include "ui/ports/downloads_view.hpp"
#include "ui/settings_view.hpp"

int main(int argc, char* argv[]) {
    // Applet mode check
    AppletType at = appletGetAppletType();
    if (at == AppletType_LibraryApplet || at == AppletType_OverlayApplet) {
        consoleInit(NULL);
        printf("\n====================================================\n");
        printf(" THE GOONIES PORTS REQUIERE ACCESO TOTAL A LA MEMORIA\n");
        printf(" THE GOONIES PORTS REQUIRES FULL MEMORY ACCESS\n");
        printf("====================================================\n\n");
        printf(" Por favor, abre cualquier juego manteniendo pulsado 'R'\n");
        printf(" para abrir el Homebrew Menu en modo Acceso Total.\n\n");
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

    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
    NWindow* win = nwindowGetDefault();
    if (win) {
        nwindowSetDimensions(win, 1280, 720);
    }

    socketInitializeDefault();
    romfsInit();
    plInitialize(PlServiceType_User);
    nifmInitialize(NifmServiceType_User);
    psmInitialize();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    nxlinkStdio();


    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("The Goonies Ports");
    
    // Forzar tema oscuro siempre (estilo profesional)
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

    pipensx::ui::theme::registerColors();
    
    // Flat theme
    brls::Style style = brls::Application::getStyle();
    style.addMetric("brls/button/corner_radius", 0.0f);
    style.addMetric("brls/sidebar/padding_left", 24.0f);
    
    pipensx::AppSettings* settings = new pipensx::AppSettings("sdmc:/switch/TheGooniesPorts/settings.json");
    std::string loadErr;
    settings->load(loadErr);
    
    auto* download_manager = new goonies::SimpleDownloadManager(settings);
    auto* metadata_service = new pipensx::GameMetadataService("sdmc:/switch/TheGooniesPorts");

    auto* catalog_service = new pipensx::CatalogService("sdmc:/switch/TheGooniesPorts");
    std::string err;
    std::vector<pipensx::CatalogEntry> latest;
    // Intentar siempre descargar la ultima version de Github al iniciar
    if (catalog_service->fetchLatest(latest, err)) {
        catalog_service->adopt(std::move(latest));
    } else if (!catalog_service->load(err)) {
        brls::Logger::error("No se pudo cargar el catalogo: %s", err.c_str());
        if (!catalog_service->loadFile("romfs:/repo.json", "Local", err)) {
            brls::Logger::error("No repo.json fallback: %s", err.c_str());
        }
    }

    pipensx::ui::installSidebarStyle();
    
    
    bool mixerInit = false;
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        brls::Logger::error("Mix_OpenAudio failed");
    } else {
        mixerInit = true;
    }
    Mix_Music *bgmMusic = nullptr;
    if (mixerInit) {
        bgmMusic = Mix_LoadMUS("romfs:/bgm.mp3");
        if (bgmMusic && settings->get().enableBackgroundMusic) {
            Mix_PlayMusic(bgmMusic, -1);
        }
    }
    bool wasBgmEnabled = settings->get().enableBackgroundMusic;

    pipensx::ui::MainFrame* rootFrame = new pipensx::ui::MainFrame();

    
    std::map<std::string, int> categoryCounts;
    for (const auto& entry : catalog_service->entries()) {
        if (!entry.category.empty()) {
            categoryCounts[entry.category]++;
        }
    }

    int descargas_idx = 2 + categoryCounts.size();
    int ajustes_idx = descargas_idx + 1;

    auto makeTab = [=](const std::string& filter) -> brls::View* {
        auto* view = new goonies::ui::PortsGridView(catalog_service, download_manager, metadata_service, filter);
        view->registerAction(settings->get().language == 2 ? "Exit" : "Salir", brls::BUTTON_START, [](brls::View*) { brls::Application::quit(); return true; });
        view->registerAction(settings->get().language == 2 ? "Downloads" : "Descargas", brls::BUTTON_Y, [=](brls::View*) { rootFrame->focusTab(descargas_idx); return true; });
        view->registerAction(settings->get().language == 2 ? "Settings" : "Ajustes", brls::BUTTON_BACK, [=](brls::View*) { rootFrame->focusTab(ajustes_idx); return true; });
        return view;
    };

    int totalCount = catalog_service->entries().size();
    int novedadesCount = (totalCount < 6) ? totalCount : 6;
    
    rootFrame->addNavTab((settings->get().language == 2 ? "New (" : "Novedades (") + std::to_string(novedadesCount) + ")", pipensx::ui::NavIconType::Catalog, [=]() { return makeTab("Novedades"); });
    rootFrame->addNavTab((settings->get().language == 2 ? "All (" : "Todos (") + std::to_string(totalCount) + ")", pipensx::ui::NavIconType::Catalog, [=]() { return makeTab(""); });

    for (const auto& pair : categoryCounts) {
        std::string catName = pair.first;
        rootFrame->addNavTab(pair.first + " (" + std::to_string(pair.second) + ")", pipensx::ui::NavIconType::Catalog, [=]() { return makeTab(catName); });
    }
    
    rootFrame->addNavTab(settings->get().language == 2 ? "Downloads" : "Descargas", pipensx::ui::NavIconType::Downloads, [=]() -> brls::View* {
        auto* dlView = new goonies::ui::DownloadsView(download_manager);
        dlView->registerAction(settings->get().language == 2 ? "Clear Queue" : "Limpiar Cola", brls::BUTTON_LB, [download_manager, settings](brls::View*) {
            brls::Dialog* d = new brls::Dialog(settings->get().language == 2 ? "Clear finished downloads?" : "¿Limpiar descargas terminadas?");
            d->addButton(settings->get().language == 2 ? "Yes" : "Si", [download_manager]() {
                download_manager->clearCompleted();
            });
            d->addButton(settings->get().language == 2 ? "No" : "No", []{});
            d->open();
            return true;
        });
        return dlView;
    });

    rootFrame->addNavTab(settings->get().language == 2 ? "Settings" : "Ajustes", pipensx::ui::NavIconType::Settings, [=]() -> brls::View* {
        auto* view = new goonies::ui::SettingsView(settings);
        view->registerAction(settings->get().language == 2 ? "Exit" : "Salir", brls::BUTTON_START, [](brls::View*) { brls::Application::quit(); return true; });
        view->registerAction(settings->get().language == 2 ? "Downloads" : "Descargas", brls::BUTTON_Y, [=](brls::View*) { rootFrame->focusTab(descargas_idx); return true; });
        return view;
    });

    // Register actions on the Sidebar itself so they appear when the sidebar is focused
    brls::View* sidebar = rootFrame->getView("brls/tab_frame/sidebar");
    if (sidebar) {
        sidebar->registerAction(settings->get().language == 2 ? "Exit" : "Salir", brls::BUTTON_START, [](brls::View*) { brls::Application::quit(); return true; });
        sidebar->registerAction(settings->get().language == 2 ? "Downloads" : "Descargas", brls::BUTTON_Y, [=](brls::View*) { rootFrame->focusTab(descargas_idx); return true; });
        sidebar->registerAction(settings->get().language == 2 ? "Settings" : "Ajustes", brls::BUTTON_BACK, [=](brls::View*) { rootFrame->focusTab(ajustes_idx); return true; });
    }

    auto* applet = new brls::AppletFrame(rootFrame);
    applet->setTitle("The Goonies Ports");
    applet->setIcon("romfs:/icon_explorer.png");
    
    // Disable AppletFrame's default B button back logic so it never shows up
    applet->setActionAvailable(brls::BUTTON_B, false);

    brls::Application::pushActivity(new brls::Activity(applet));

    if (settings->get().language == 0) {
        brls::Dialog* langDialog = new brls::Dialog("Selecciona tu idioma / Select your language");
        langDialog->addButton("Castellano", [settings, langDialog]() {
            pipensx::AppSettingsData data = settings->get();
            data.language = 1;
            std::string e;
            settings->update(data, e);
            langDialog->dismiss();
        });
        langDialog->addButton("English", [settings, langDialog]() {
            pipensx::AppSettingsData data = settings->get();
            data.language = 2;
            std::string e;
            settings->update(data, e);
            brls::Application::quit();
        });
        langDialog->setCancelable(false);
        langDialog->open();
    }
    
    while (brls::Application::mainLoop()) {
        if (mixerInit && bgmMusic) {
            bool isBgmEnabled = settings->get().enableBackgroundMusic;
            if (isBgmEnabled != wasBgmEnabled) {
                if (isBgmEnabled) {
                    if (Mix_PlayingMusic() == 0) {
                        Mix_PlayMusic(bgmMusic, -1);
                    } else {
                        Mix_ResumeMusic();
                    }
                } else {
                    Mix_PauseMusic();
                }
                wasBgmEnabled = isBgmEnabled;
            }
        }
    }

    if (bgmMusic) {
        Mix_FreeMusic(bgmMusic);
        bgmMusic = nullptr;
    }
    if (mixerInit) {
        Mix_CloseAudio();
    }


    delete download_manager;
    delete catalog_service;
    delete metadata_service;

    curl_global_cleanup();
    psmExit();
    nifmExit();
    plExit();
    romfsExit();
    socketExit();

    return EXIT_SUCCESS;
}
