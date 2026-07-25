#include "ui/main_menu.hpp"
#include <borealis/views/bottom_bar.hpp>
#include "ui/catalog/catalog_view.hpp"
#include "linkuser.hpp"
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

    class LinkGooniesMenuView : public brls::Box {
    public:
        LinkGooniesMenuView() : brls::Box(brls::Axis::COLUMN) {
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
            titleLabel->setText("LinkGoonies");
            titleLabel->setFontSize(48);
            titleLabel->setMarginBottom(48);
            centerBox->addView(titleLabel);

            brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
            buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
            buttonsBox->setWidth(600);

            // 1. Vincular
            buttonsBox->addView(createMenuButton(t("Vincular cuentas offline", "Link accounts offline"), []() {
                auto* dialog = new brls::Dialog(t(
                    "¿Vincular todas las cuentas offline?\nEsto creará una vinculación falsa. Si tenías una cuenta NNID vinculada, se sobrescribirá.",
                    "Link all accounts offline?\nThis will create a dummy link. If you had a linked NNID account, it will be overwritten."
                ));
                dialog->addButton(t("Vincular", "Link"), [] {
                    brls::Application::notify(t("Vinculando cuentas...", "Linking accounts..."));
                    std::string err = pipensx::linkuser::linkAccounts();
                    if (err.empty()) {
                        auto* rebootDialog = new brls::Dialog(t(
                            "¡Cuentas vinculadas con éxito! Para aplicar los cambios y evitar que la consola crasheé, es obligatorio reiniciar.",
                            "Accounts linked successfully! To apply changes and prevent a system crash, you must reboot."
                        ));
                        rebootDialog->addButton(t("Reiniciar", "Reboot"), [] {
                            pipensx::linkuser::g_shouldReboot = true;
                            brls::Application::quit();
                        });
                        rebootDialog->open();
                    } else {
                        auto* errorDialog = new brls::Dialog(t(
                            ("Error al vincular: " + err + "\n\nNOTA: El acceso a los perfiles requiere permisos del sistema. Debes iniciar esta aplicación desde el Álbum (Modo Applet) sin pulsar R para poder vincular.").c_str(),
                            ("Error linking: " + err + "\n\nNOTE: Profiles access requires system permissions. You must launch this app from the Album (Applet Mode) without holding R to link.").c_str()
                        ));
                        errorDialog->addButton(t("Aceptar", "OK"), [] {});
                        errorDialog->open();
                    }
                });
                dialog->addButton(t("Cancelar", "Cancel"), [] {});
                dialog->open();
            }));

            // 2. Desvincular
            buttonsBox->addView(createMenuButton(t("Desvincular cuentas", "Unlink accounts"), []() {
                auto* dialog = new brls::Dialog(t(
                    "¿Desvincular todas las cuentas?\nSe eliminará la vinculación local y online de tus perfiles.",
                    "Unlink all accounts?\nThis will remove local and online links from your profiles."
                ));
                dialog->addButton(t("Desvincular", "Unlink"), [] {
                    brls::Application::notify(t("Desvinculando cuentas...", "Unlinking accounts..."));
                    std::string err = pipensx::linkuser::unlinkAccounts();
                    if (err.empty()) {
                        auto* rebootDialog = new brls::Dialog(t(
                            "¡Cuentas desvinculadas con éxito! Para aplicar los cambios y evitar que la consola crasheé, es obligatorio reiniciar.",
                            "Accounts unlinked successfully! To apply changes and prevent a system crash, you must reboot."
                        ));
                        rebootDialog->addButton(t("Reiniciar", "Reboot"), [] {
                            pipensx::linkuser::g_shouldReboot = true;
                            brls::Application::quit();
                        });
                        rebootDialog->open();
                    } else {
                        auto* errorDialog = new brls::Dialog(t(
                            ("Error al desvincular: " + err + "\n\nNOTA: El acceso a los perfiles requiere permisos del sistema. Debes iniciar esta aplicación desde el Álbum (Modo Applet) sin pulsar R para poder desvincular.").c_str(),
                            ("Error unlinking: " + err + "\n\nNOTE: Profiles access requires system permissions. You must launch this app from the Album (Applet Mode) without holding R to unlink.").c_str()
                        ));
                        errorDialog->addButton(t("Aceptar", "OK"), [] {});
                        errorDialog->open();
                    }
                });
                dialog->addButton(t("Cancelar", "Cancel"), [] {});
                dialog->open();
            }));

            // 3. Restaurar copia
            buttonsBox->addView(createMenuButton(t("Restaurar copia de seguridad", "Restore backup"), []() {
                std::vector<std::string> backups = pipensx::linkuser::getBackups();
                if (backups.empty()) {
                    auto* dialog = new brls::Dialog(t(
                        "No se encontraron copias de seguridad en la SD.",
                        "No backups found on the SD card."
                    ));
                    dialog->addButton(t("Aceptar", "OK"), [] {});
                    dialog->open();
                    return;
                }

                std::string latest_backup = backups.back();
                auto* dialog = new brls::Dialog(t(
                    ("¿Restaurar la copia de seguridad más reciente?\nArchivo: " + latest_backup + "\nEsto sobrescribirá tus perfiles actuales.").c_str(),
                    ("Restore the most recent backup?\nFile: " + latest_backup + "\nThis will overwrite your current profiles.").c_str()
                ));
                dialog->addButton(t("Restaurar", "Restore"), [latest_backup] {
                    brls::Application::notify(t("Restaurando copia...", "Restoring backup..."));
                    std::string err = pipensx::linkuser::restoreBackup(latest_backup);
                    if (err.empty()) {
                        auto* rebootDialog = new brls::Dialog(t(
                            "¡Copia restaurada con éxito! Para aplicar los cambios y evitar que la consola crasheé, es obligatorio reiniciar.",
                            "Backup restored successfully! To apply changes and prevent a system crash, you must reboot."
                        ));
                        rebootDialog->addButton(t("Reiniciar", "Reboot"), [] {
                            pipensx::linkuser::g_shouldReboot = true;
                            brls::Application::quit();
                        });
                        rebootDialog->open();
                    } else {
                        auto* errorDialog = new brls::Dialog(t(
                            ("Error al restaurar: " + err + "\n\nNOTA: El acceso a los perfiles requiere permisos del sistema. Debes iniciar esta aplicación desde el Álbum (Modo Applet) sin pulsar R para poder restaurar.").c_str(),
                            ("Error restoring: " + err + "\n\nNOTE: Profiles access requires system permissions. You must launch this app from the Album (Applet Mode) without holding R to restore.").c_str()
                        ));
                        errorDialog->addButton(t("Aceptar", "OK"), [] {});
                        errorDialog->open();
                    }
                });
                dialog->addButton(t("Cancelar", "Cancel"), [] {});
                dialog->open();
            }));

            centerBox->addView(buttonsBox);
            this->addView(centerBox);
            this->addView(new brls::BottomBar());
        }
        static brls::View* create() { return new LinkGooniesMenuView(); }

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
                   pipensx::HomebrewService* homebrew, pipensx::UpdateService* updater)
    : brls::Box(brls::Axis::COLUMN), manager_(manager), catalog_(catalog),
      metadata_(metadata), installed_(installed), settings_(settings), homebrew_(homebrew), updater_(updater) {
    
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setAlignItems(brls::AlignItems::CENTER);
    centerBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    centerBox->setGrow(1.0f);

    brls::Box* topSpacer = new brls::Box();
    topSpacer->setHeight(40);
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

    // 4. LinkGoonies - Vincular y desvincular cuentas online
    buttonsBox->addView(createMenuButton(t("LinkGoonies - Vincular y desvincular cuentas online", "LinkGoonies - Link and unlink online accounts"), []() {
        brls::Application::pushActivity(new brls::Activity(goonies::LinkGooniesMenuView::create()));
    }));

    // 5. Crear acceso directo
    auto m_homebrew = homebrew_;
    buttonsBox->addView(createMenuButton(t("Crear acceso directo", "Create Forwarder"), [this, m_homebrew]() {
        brls::Application::pushActivity(new brls::Activity(
            new pipensx::ui::ForwardersView("sdmc:/switch/")));
    }));

    centerBox->addView(buttonsBox);

    // Spacer between buttons and credits to push it down
    brls::Box* creditsTopSpacer = new brls::Box();
    creditsTopSpacer->setGrow(1.0f);
    centerBox->addView(creditsTopSpacer);

    brls::Label* creditsLabel = new brls::Label();
    creditsLabel->setText(t("Desarrollado para la comunidad Switch ES - The Goonies OS por GoodmanBCN", "Developed for the Switch ES community - The Goonies OS by GoodmanBCN"));
    creditsLabel->setFontSize(18);
    creditsLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    centerBox->addView(creditsLabel);

    // Spacer between credits and bottom bar to balance and center the text
    brls::Box* creditsBottomSpacer = new brls::Box();
    creditsBottomSpacer->setGrow(1.0f);
    centerBox->addView(creditsBottomSpacer);

    this->addView(centerBox);
    this->addView(new brls::BottomBar());

    // Register Actions
    this->registerAction(t("Ajustes", "Settings"), brls::BUTTON_BACK, [this](brls::View* view) {
        brls::Application::pushActivity(new brls::Activity(
            new pipensx::ui::SettingsView(settings_, manager_, catalog_, metadata_, installed_, updater_)));
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

    // Auto-update check at startup
    if (updater_) {
        updater_->checkAsync([this](pipensx::UpdateCheckResult result) {
            if (result.ok && result.updateAvailable) {
                brls::sync([this, release = result.release]() {
                    std::string esText = "Hay una nueva versión de The Goonies APP disponible (" + release.version + "). ¿Deseas descargarla e instalarla ahora?";
                    std::string enText = "A new version of The Goonies APP is available (" + release.version + "). Do you want to download and install it now?";
                    brls::Dialog* dialog = new brls::Dialog(
                        t(esText.c_str(), enText.c_str())
                    );
                    dialog->addButton(t("Actualizar", "Update"), [this, release]() {
                        brls::Label* progressLabel = new brls::Label();
                        progressLabel->setText(t("Descargando...", "Downloading..."));
                        progressLabel->setFontSize(22);
                        progressLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
                        progressLabel->setMarginBottom(20);
                        
                        brls::Box* pContent = new brls::Box(brls::Axis::COLUMN);
                        pContent->setAlignItems(brls::AlignItems::CENTER);
                        pContent->addView(progressLabel);
                        
                        brls::Dialog* downloadDialog = new brls::Dialog(pContent);
                        downloadDialog->setCancelable(false);
                        
                        updater_->onInstallProgress([progressLabel](uint64_t received, uint64_t total) {
                            const int percent = static_cast<int>((received * 100) / total);
                            brls::sync([progressLabel, percent] {
                                progressLabel->setText("Downloading... " + std::to_string(percent) + "%");
                            });
                        });
                        
                        updater_->installAsync(release, [this, downloadDialog](bool installed, std::string error) {
                            brls::sync([this, downloadDialog, installed, error = std::move(error)] {
                                downloadDialog->close([this, installed, error]() {
                                    if (!installed) {
                                        brls::Application::notify(t("Error al instalar: ", "Installation error: ") + error);
                                        return;
                                    }
                                    
#ifdef __SWITCH__
                                    const std::string helper = updater_->helperPath();
                                    const std::string arguments = helper + " --finish-update";
                                    const Result result = envSetNextLoad(helper.c_str(), arguments.c_str());
                                    if (R_FAILED(result)) {
                                        brls::Application::notify(t("Error al programar el reinicio.", "Failed to schedule restart."));
                                        return;
                                    }
#endif
                                    brls::Application::notify(t("Actualización descargada. Reiniciando...", "Update downloaded. Restarting..."));
                                    brls::Application::quit();
                                });
                            });
                        });
                        
                        downloadDialog->open();
                    });
                    dialog->addButton(t("Cancelar", "Cancel"), []() {});
                    dialog->open();
                });
            }
        });
    }
}

brls::Button* MainMenu::createMenuButton(const std::string& title, std::function<void()> onClick) {
    brls::Button* button = new brls::Button();
    button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    button->setText(title);
    button->setHeight(60);
    button->setMarginBottom(12);
    button->setFontSize(24);
    
    button->registerClickAction([onClick](brls::View* view) {
        if (onClick) onClick();
        return true;
    });

    return button;
}

} // namespace goonies::ui
