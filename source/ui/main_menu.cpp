#include "ui/main_menu.hpp"
#include <borealis/views/bottom_bar.hpp>
#include "ui/catalog/catalog_view.hpp"
#include "ui/installed/installed_view.hpp"
#include "app/app_settings.hpp"
#include "ui/downloads/downloads_view.hpp"
#include "ui/theme.hpp"
#include "ui/settings/settings_view.hpp"
#include "ui/mtp/mtp_view.hpp"
#include "ui/installed/install_browser.hpp"
#include "ui/saves/saves_view.hpp"
#include "ui/forwarders/forwarders_view.hpp"
#include "ui/updater/updater_view.hpp"
#include <usbhsfs.h>
#include "ui/common/ui_helpers.hpp"

// Forward declaration from main.cpp
namespace goonies {
    class SDInstallView;
    class USBView;
}

namespace goonies {
    class InstallMenuView : public brls::Box {
    public:
        InstallMenuView() : brls::Box(brls::Axis::COLUMN) {
            this->setAlignItems(brls::AlignItems::STRETCH);
            
            // Add 'B' to go back, so it shows in the bottom bar
            this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [](brls::View* view) {
                brls::Application::popActivity();
                return true;
            });
            
            brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
            centerBox->setAlignItems(brls::AlignItems::CENTER);
            centerBox->setJustifyContent(brls::JustifyContent::CENTER);
            centerBox->setGrow(1.0f);

            brls::Label* titleLabel = new brls::Label();
            titleLabel->setText(t("Instalar Juegos", "Install Games"));
            titleLabel->setFontSize(48);
            titleLabel->setMarginBottom(48);
            centerBox->addView(titleLabel);

            brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
            buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
            buttonsBox->setWidth(600);

            buttonsBox->addView(createMenuButton(t("Instalar juegos por MTP", "Install games via MTP"), []() {
                brls::Application::pushActivity(new brls::Activity(new goonies::ui::MTPView()));
            }));

            buttonsBox->addView(createMenuButton(t("Instalar juegos desde microSD", "Install games from microSD"), []() {
                brls::Application::pushActivity(new brls::Activity(new goonies::ui::InstallBrowserView("sdmc:/")));
            }));

            buttonsBox->addView(createMenuButton(t("Instalar juegos desde pendrive o disco externo", "Install games from USB drive"), [this]() {
                u32 count = usbHsFsGetMountedDeviceCount();
                if (count > 0) {
                    UsbHsFsDevice devices[5];
                    memset(devices, 0, sizeof(devices));
                    u32 listed = usbHsFsListMountedDevices(devices, 5);
                    if (listed > 0) {
                        std::string validPath = "";
                        for (u32 i = 0; i < listed; i++) {
                            std::string devName(devices[i].name, strnlen(devices[i].name, 32));
                            if (!devName.empty() && devName.back() == ':') {
                                devName.pop_back();
                            }
                            std::string testPath = devName + ":/";
                            DIR* dir = opendir(testPath.c_str());
                            if (dir) {
                                closedir(dir);
                                validPath = testPath;
                                break;
                            }
                        }
                        
                        // Fallback to first if none opened properly (to show empty folder instead of nothing)
                        if (validPath.empty()) {
                            std::string devName(devices[0].name, strnlen(devices[0].name, 32));
                            if (!devName.empty() && devName.back() == ':') {
                                devName.pop_back();
                            }
                            validPath = devName + ":/";
                        }

                        brls::Application::pushActivity(new brls::Activity(new goonies::ui::InstallBrowserView(validPath)));
                    } else {
                        brls::Application::notify(t("No se encontraron dispositivos USB.", "No USB devices found."));
                    }
                } else {
                    brls::Application::notify(t("No hay pendrive conectado.", "No USB drive connected."));
                }
            }));

            centerBox->addView(buttonsBox);
            
            this->addView(centerBox);
            this->addView(new brls::BottomBar());
        }
        static brls::View* create() { return new InstallMenuView(); }

    private:
        brls::Button* createMenuButton(const std::string& title, std::function<void()> onClick) {
            brls::Button* button = new brls::Button();
            button->setStyle(&brls::BUTTONSTYLE_BORDERED);
            button->setText(title);
            button->setHeight(64);
            button->setMarginBottom(16);
            button->setFontSize(22);
            button->registerClickAction([onClick](brls::View* view) {
                if (onClick) onClick();
                return true;
            });
            return button;
        }
    };

    class ManageMenuView : public brls::Box {
    public:
        ManageMenuView(pipensx::InstalledTitleService* installed, pipensx::DownloadManager* manager, pipensx::GameMetadataService* metadata)
            : brls::Box(brls::Axis::COLUMN) {
            this->setAlignItems(brls::AlignItems::STRETCH);
            
            this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [](brls::View* view) {
                brls::Application::popActivity();
                return true;
            });
            
            brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
            centerBox->setAlignItems(brls::AlignItems::CENTER);
            centerBox->setJustifyContent(brls::JustifyContent::CENTER);
            centerBox->setGrow(1.0f);

            brls::Label* titleLabel = new brls::Label();
            titleLabel->setText(t("Gestión de Consola", "Console Management"));
            titleLabel->setFontSize(48);
            titleLabel->setMarginBottom(48);
            centerBox->addView(titleLabel);

            brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
            buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
            buttonsBox->setWidth(600);

            buttonsBox->addView(createMenuButton(t("Juegos Instalados", "Installed Games"), [installed, manager, metadata]() {
                brls::Application::pushActivity(new brls::Activity(
                    new pipensx::ui::InstalledView(installed, manager, metadata)));
            }));

            buttonsBox->addView(createMenuButton(t("Partidas Guardadas", "Save Data"), [installed, metadata]() {
                brls::Application::pushActivity(new brls::Activity(
                    new pipensx::ui::SavesView(installed, metadata)));
            }));

            centerBox->addView(buttonsBox);
            
            this->addView(centerBox);
            this->addView(new brls::BottomBar());
        }

    private:
        brls::Button* createMenuButton(const std::string& title, std::function<void()> onClick) {
            brls::Button* button = new brls::Button();
            button->setStyle(&brls::BUTTONSTYLE_BORDERED);
            button->setText(title);
            button->setHeight(64);
            button->setMarginBottom(16);
            button->setFontSize(22);
            button->registerClickAction([onClick](brls::View* view) {
                if (onClick) onClick();
                return true;
            });
            return button;
        }
    };
}

namespace goonies::ui {

MainMenu::MainMenu(pipensx::DownloadManager* manager, pipensx::CatalogService* catalog,
                   pipensx::GameMetadataService* metadata,
                   pipensx::InstalledTitleService* installed, pipensx::AppSettings* settings,
                   pipensx::HomebrewService* homebrew)
    : brls::Box(brls::Axis::COLUMN), manager_(manager), catalog_(catalog),
      metadata_(metadata), installed_(installed), settings_(settings), homebrew_(homebrew) {
    
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setAlignItems(brls::AlignItems::CENTER);
    centerBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    centerBox->setGrow(1.0f);

    brls::Box* topSpacer = new brls::Box();
    topSpacer->setGrow(2.0f);
    centerBox->addView(topSpacer);

    // Header container
    brls::Box* headerBox = new brls::Box(brls::Axis::ROW);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setMarginBottom(30);

    // Image (Logo)
    brls::Image* logo = new brls::Image();
    logo->setImageFromFile("romfs:/logo.png");
    logo->setWidth(150);
    logo->setHeight(150);
    logo->setMarginRight(32);
    headerBox->addView(logo);

    // Title text box
    brls::Box* titleBox = new brls::Box(brls::Axis::COLUMN);
    
    brls::Label* titleLabel = new brls::Label();
    titleLabel->setText("THE GOONIES APP");
    titleLabel->setFontSize(64);
    titleLabel->setTextColor(pipensx::ui::theme::accent()); // Yellow/gold accent
    titleBox->addView(titleLabel);

    brls::Label* subtitleLabel = new brls::Label();
    subtitleLabel->setText(t("Instalador inteligente para Nintendo Switch", "Smart installer for Nintendo Switch"));
    subtitleLabel->setFontSize(28);
    subtitleLabel->setTextColor(nvgRGB(220, 220, 220));
    titleBox->addView(subtitleLabel);

    headerBox->addView(titleBox);
    centerBox->addView(headerBox);

    // Buttons Container
    brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
    buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
    buttonsBox->setWidth(800);

    // 1. Descarga de Juegos
    auto m_manager = manager_;
    auto m_metadata = metadata_;
    auto m_settings = settings_;
    auto m_catalog = catalog_;
    auto m_installed = installed_;
    buttonsBox->addView(createMenuButton(t("Descarga de Juegos", "Download Games"), [this, m_manager, m_catalog, m_metadata, m_installed, m_settings]() {
        brls::Application::pushActivity(new brls::Activity(new pipensx::ui::CatalogView(
            m_manager, m_catalog, m_metadata, m_installed, m_settings, [m_manager, m_metadata, m_settings]() {
                brls::Logger::info("User clicked Cola Descargas");
                brls::Logger::info("Allocating MainView...");
                auto* mainView = new pipensx::ui::MainView(m_manager, m_metadata, m_settings);
                brls::Logger::info("MainView allocated. Pushing Activity...");
                brls::Application::pushActivity(new brls::Activity(mainView));
                brls::Logger::info("Activity pushed successfully.");
            })));
    }));

    // 2. Instalar Juegos (Abre submenu)
    buttonsBox->addView(createMenuButton(t("Instalar Juegos", "Install Games"), []() {
        brls::Application::pushActivity(new brls::Activity(goonies::InstallMenuView::create()));
    }));

    // 3. Juegos Instalados / Partidas Guardadas
    buttonsBox->addView(createMenuButton(t("Juegos Instalados / Partidas Guardadas", "Installed Games / Save Data"), [this, m_installed, m_manager, m_metadata]() {
        brls::Application::pushActivity(new brls::Activity(
            new pipensx::ui::InstalledView(m_installed, m_manager, m_metadata)));
    }));

    // 4. Explorar microSD (Conexión MTP con Explorador de Windows - Unidad SD directa)
    buttonsBox->addView(createMenuButton(t("Explorar microSD", "Explore microSD"), []() {
        brls::Application::pushActivity(new brls::Activity(new goonies::ui::MTPExplorerView()));
    }));

    // 5. Crear acceso directo
    auto m_homebrew = homebrew_;
    buttonsBox->addView(createMenuButton(t("Crear acceso directo", "Create Forwarder"), [this, m_homebrew]() {
        brls::Application::pushActivity(new brls::Activity(
            new pipensx::ui::ForwardersView(m_homebrew)));
    }));

    centerBox->addView(buttonsBox);

    brls::Label* creditsLabel = new brls::Label();
    creditsLabel->setText(t("Desarrollado para la comunidad Switch ES - The Goonies OS por GoodmanBCN", "Developed for the Switch ES community - The Goonies OS by GoodmanBCN"));
    creditsLabel->setFontSize(18);
    creditsLabel->setMarginTop(24);
    creditsLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    centerBox->addView(creditsLabel);

    brls::Box* bottomSpacer = new brls::Box();
    bottomSpacer->setGrow(1.0f);
    centerBox->addView(bottomSpacer);

    this->addView(centerBox);
    this->addView(new brls::BottomBar());

    // Register Actions
    this->registerAction(t("Ajustes", "Settings"), brls::BUTTON_BACK, [this](brls::View* view) {
        brls::Application::pushActivity(new brls::Activity(
            new pipensx::ui::SettingsView(settings_, manager_, catalog_, metadata_, installed_)));
        return true;
    }, false); // false = show in hint bar
    
    this->registerAction(t("Salir", "Exit"), brls::BUTTON_START, [this](brls::View* view) {
        brls::Application::quit();
        return true;
    }, false);

    this->registerAction(t("Actualizar CFW", "Update CFW"), brls::BUTTON_Y, [this](brls::View* view) {
        brls::Application::pushActivity(new brls::Activity(new goonies::ui::UpdaterView()));
        return true;
    }, false);
}

brls::Button* MainMenu::createMenuButton(const std::string& title, std::function<void()> onClick) {
    brls::Button* button = new brls::Button();
    button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    button->setText(title);
    button->setHeight(72);
    button->setMarginBottom(16);
    button->setFontSize(24);
    
    button->registerClickAction([onClick](brls::View* view) {
        if (onClick) onClick();
        return true;
    });

    return button;
}

} // namespace goonies::ui
