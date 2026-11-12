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
#include "ui/common/dashboard_card.hpp"
#include "app/install_space.hpp"
#include <iomanip>
#include <sstream>

// Forward declaration from main.cpp
namespace goonies {
    class SDInstallView;
    class USBView;
}

namespace {

#ifndef THEGOONIES_VERSION
#define THEGOONIES_VERSION "v3.0.2"
#endif

// Modern interactive row button for submenus with glowing focus borders
class ModernMenuRow : public brls::Box {
public:
    ModernMenuRow(const std::string& title, NVGcolor accentColor, std::function<void()> onClick)
        : brls::Box(brls::Axis::ROW), accentColor_(accentColor), onClick_(std::move(onClick)) {
        this->setFocusable(true);
        this->setCornerRadius(12.0f);
        this->setHighlightCornerRadius(12.0f);
        this->setHideHighlightBorder(true);
        this->setHideHighlightBackground(true);

        this->setWidth(680);
        this->setHeight(64);
        this->setMarginBottom(14);
        this->setPadding(0, 24, 0, 24);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

        titleLabel_ = new brls::Label();
        titleLabel_->setText(title);
        titleLabel_->setFontSize(20);
        titleLabel_->setTextColor(goonies::ui::theme::textPrimary());
        this->addView(titleLabel_);

        arrowLabel_ = new brls::Label();
        arrowLabel_->setText(">");
        arrowLabel_->setFontSize(20);
        arrowLabel_->setTextColor(nvgRGBA(255, 255, 255, 90));
        this->addView(arrowLabel_);

        this->registerClickAction([this](brls::View* view) {
            if (onClick_) onClick_();
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        bool focused = this->isFocused();
        float r = 12.0f;

        if (focused) {
            NVGpaint glow = nvgBoxGradient(vg, x, y, width, height, r, 10.0f,
                                           nvgRGBAf(accentColor_.r, accentColor_.g, accentColor_.b, 0.40f),
                                           nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, x - 6, y - 6, width + 12, height + 12);
            nvgFillPaint(vg, glow);
            nvgFill(vg);
        }

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, r);
        nvgFillColor(vg, focused ? goonies::ui::theme::cardBgFocus() : goonies::ui::theme::cardBg());
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, r);
        if (focused) {
            nvgStrokeWidth(vg, 2.0f);
            nvgStrokeColor(vg, accentColor_);
        } else {
            nvgStrokeWidth(vg, 1.0f);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 22));
        }
        nvgStroke(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 3.0f, y + 14.0f, 3.5f, height - 28.0f, 1.75f);
        nvgFillColor(vg, accentColor_);
        nvgFill(vg);

        if (titleLabel_) {
            titleLabel_->setTextColor(focused ? accentColor_ : goonies::ui::theme::textPrimary());
        }
        if (arrowLabel_) {
            arrowLabel_->setTextColor(focused ? accentColor_ : nvgRGBA(255, 255, 255, 90));
        }

        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

private:
    NVGcolor accentColor_;
    std::function<void()> onClick_;
    brls::Label* titleLabel_ = nullptr;
    brls::Label* arrowLabel_ = nullptr;
};

// Helper for top status pills (HUD)
inline brls::Box* createStatusPill(const std::string& text, NVGcolor textColor, NVGcolor bgTint) {
    brls::Box* pill = new brls::Box();
    pill->setPadding(7, 18, 7, 18);
    pill->setMarginLeft(12);
    pill->setCornerRadius(14.0f);
    pill->setBackgroundColor(bgTint);
    pill->setBorderThickness(1.5f);
    pill->setBorderColor(nvgRGBAf(textColor.r, textColor.g, textColor.b, 0.35f));
    
    brls::Label* lbl = new brls::Label();
    lbl->setText(text);
    lbl->setFontSize(16);
    lbl->setTextColor(textColor);
    pill->addView(lbl);
    return pill;
}

inline std::string getStoragePillText() {
    auto snapshot = goonies::queryStorageSpace("sdmc:/");
    if (snapshot.available && snapshot.totalBytes > 0) {
        double freeGb = static_cast<double>(snapshot.freeBytes) / (1024.0 * 1024.0 * 1024.0);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "SD: %.1f GB", freeGb);
        return std::string(buf);
    }
    return "SD: OK";
}

} // namespace

namespace goonies {
    class InstallMenuView : public brls::Box {
    public:
        InstallMenuView() : brls::Box(brls::Axis::COLUMN) {
            this->setBackgroundColor(nvgRGB(18, 19, 24));
            this->setBackgroundColor(nvgRGB(18, 19, 24));
    this->setAlignItems(brls::AlignItems::STRETCH);
            
            // Add 'B' to go back, so it shows in the bottom bar
            this->registerAction(t("Volver", "Back", "Retornar"), brls::BUTTON_B, [](brls::View* view) {
                brls::Application::popActivity();
                return true;
            });
            
            brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
            centerBox->setAlignItems(brls::AlignItems::CENTER);
            centerBox->setJustifyContent(brls::JustifyContent::CENTER);
            centerBox->setGrow(1.0f);

            brls::Label* titleLabel = new brls::Label();
            titleLabel->setText(t("Instalar Juegos", "Install Games", "Instalar jogos"));
            titleLabel->setFontSize(38);
            titleLabel->setMarginBottom(32);
            titleLabel->setTextColor(goonies::ui::theme::neonGreen());
            centerBox->addView(titleLabel);

            brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
            buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
            buttonsBox->setWidth(680);

            buttonsBox->addView(new ModernMenuRow(
                t("Instalar juegos por MTP", "Install games via MTP", "Instale jogos via MTP"),
                goonies::ui::theme::neonGreen(),
                []() {
                    brls::Application::pushActivity(new brls::Activity(new goonies::ui::MTPView()));
                }
            ));

            buttonsBox->addView(new ModernMenuRow(
                t("Instalar juegos desde microSD", "Install games from microSD", "Instale jogos do microSD"),
                goonies::ui::theme::neonGreen(),
                []() {
                    brls::Application::pushActivity(new brls::Activity(new goonies::ui::InstallBrowserView("sdmc:/")));
                }
            ));

            buttonsBox->addView(new ModernMenuRow(
                t("Instalar juegos desde pendrive o disco externo", "Install games from USB drive", "Instale jogos do pendrive ou disco externo"),
                goonies::ui::theme::neonGreen(),
                [this]() {
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
                            brls::Application::notify(t("No se encontraron dispositivos USB.", "No USB devices found.", "Nenhum dispositivo USB encontrado."));
                        }
                    } else {
                        brls::Application::notify(t("No hay pendrive conectado.", "No USB drive connected.", "Não há pendrive conectado."));
                    }
                }
            ));

            centerBox->addView(buttonsBox);
            this->addView(centerBox);
            this->addView(new brls::BottomBar());
        }
        static brls::View* create() { return new InstallMenuView(); }
    };

    class ManageMenuView : public brls::Box {
    public:
        ManageMenuView(goonies::InstalledTitleService* installed, goonies::DownloadManager* manager, goonies::GameMetadataService* metadata)
            : brls::Box(brls::Axis::COLUMN) {
            this->setBackgroundColor(nvgRGB(18, 19, 24));
            this->setBackgroundColor(nvgRGB(18, 19, 24));
    this->setAlignItems(brls::AlignItems::STRETCH);
            
            this->registerAction(t("Volver", "Back", "Retornar"), brls::BUTTON_B, [](brls::View* view) {
                brls::Application::popActivity();
                return true;
            });
            
            brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
            centerBox->setAlignItems(brls::AlignItems::CENTER);
            centerBox->setJustifyContent(brls::JustifyContent::CENTER);
            centerBox->setGrow(1.0f);

            brls::Label* titleLabel = new brls::Label();
            titleLabel->setText(t("Gestión de Consola", "Console Management", "Gerenciamento de console"));
            titleLabel->setFontSize(38);
            titleLabel->setMarginBottom(32);
            titleLabel->setTextColor(goonies::ui::theme::neonPurple());
            centerBox->addView(titleLabel);

            brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
            buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
            buttonsBox->setWidth(680);

            buttonsBox->addView(new ModernMenuRow(
                t("Juegos Instalados", "Installed Games", "Jogos instalados"),
                goonies::ui::theme::neonPurple(),
                [installed, manager, metadata]() {
                    brls::Application::pushActivity(new brls::Activity(
                        new goonies::ui::InstalledView(installed, manager, metadata, nullptr, nullptr)));
                }
            ));

            buttonsBox->addView(new ModernMenuRow(
                t("Partidas Guardadas", "Save Data", "Jogos salvos"),
                goonies::ui::theme::neonPurple(),
                [installed, metadata]() {
                    brls::Application::pushActivity(new brls::Activity(
                        new goonies::ui::SavesView(installed, metadata)));
                }
            ));

            centerBox->addView(buttonsBox);
            this->addView(centerBox);
            this->addView(new brls::BottomBar());
        }
    };

    class LinkGooniesMenuView : public brls::Box {
    public:
        LinkGooniesMenuView() : brls::Box(brls::Axis::COLUMN) {
            this->setBackgroundColor(nvgRGB(18, 19, 24));
            this->setBackgroundColor(nvgRGB(18, 19, 24));
    this->setAlignItems(brls::AlignItems::STRETCH);
            
            this->registerAction(t("Volver", "Back", "Retornar"), brls::BUTTON_B, [](brls::View* view) {
                brls::Application::popActivity();
                return true;
            });
            
            brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
            centerBox->setAlignItems(brls::AlignItems::CENTER);
            centerBox->setJustifyContent(brls::JustifyContent::CENTER);
            centerBox->setGrow(1.0f);

            brls::Label* titleLabel = new brls::Label();
            titleLabel->setText("LinkGoonies");
            titleLabel->setFontSize(38);
            titleLabel->setMarginBottom(32);
            titleLabel->setTextColor(goonies::ui::theme::neonOrange());
            centerBox->addView(titleLabel);

            brls::Box* buttonsBox = new brls::Box(brls::Axis::COLUMN);
            buttonsBox->setAlignItems(brls::AlignItems::STRETCH);
            buttonsBox->setWidth(680);

            // 1. Vincular
            buttonsBox->addView(new ModernMenuRow(
                t("Vincular cuentas offline", "Link accounts offline", "Vincular contas off-line"),
                goonies::ui::theme::neonOrange(),
                []() {
                    auto* dialog = new brls::Dialog(t("¿Vincular todas las cuentas offline?\nEsto creará una vinculación falsa. Si tenías una cuenta NNID vinculada, se sobrescribirá.", "Link all accounts offline?\nThis will create a dummy link. If you had a linked NNID account, it will be overwritten.", "Vincular todas as contas off-line?\nIsso criará um link falso. Se você tivesse uma conta NNID vinculada, ela será substituída."));
                    dialog->addButton(t("Vincular", "Link", "Link"), [] {
                        brls::Application::notify(t("Vinculando cuentas...", "Linking accounts...", "Vinculando contas..."));
                        std::string err = goonies::linkuser::linkAccounts();
                        if (err.empty()) {
                            auto* rebootDialog = new brls::Dialog(t("¡Cuentas vinculadas con éxito! Para aplicar los cambios y evitar que la consola crashee, es obligatorio reiniciar.", "Accounts linked successfully! To apply changes and prevent a system crash, you must reboot.", "Contas vinculadas com sucesso! Para aplicar as alterações e evitar que o console trave, é necessária uma reinicialização."));
                            rebootDialog->addButton(t("Reiniciar", "Reboot", "Reinício"), [] {
                                goonies::linkuser::g_shouldReboot = true;
                                brls::Application::quit();
                            });
                            rebootDialog->open();
                        } else {
                            auto* errorDialog = new brls::Dialog(t(
                                ("Error al vincular: " + err + "\n\nNOTA: El acceso a los perfiles requiere permisos del sistema. Debes iniciar esta aplicación desde el Álbum (Modo Applet) sin pulsar R para poder vincular.").c_str(),
                                ("Error linking: " + err + "\n\nNOTE: Profiles access requires system permissions. You must launch this app from the Album (Applet Mode) without holding R to link.").c_str()
                            ));
                            errorDialog->addButton(t("Aceptar", "OK", "Aceitar"), [] {});
                            errorDialog->open();
                        }
                    });
                    dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), [] {});
                    dialog->open();
                }
            ));

            // 2. Desvincular
            buttonsBox->addView(new ModernMenuRow(
                t("Desvincular cuentas", "Unlink accounts", "Desvincular contas"),
                goonies::ui::theme::neonOrange(),
                []() {
                    auto* dialog = new brls::Dialog(t("¿Desvincular todas las cuentas?\nSe eliminará la vinculación local y online de tus perfiles.", "Unlink all accounts?\nThis will remove local and online links from your profiles.", "Desvincular todas as contas?\nA vinculação local e on-line dos seus perfis será removida."));
                    dialog->addButton(t("Desvincular", "Unlink", "Desvincular"), [] {
                        brls::Application::notify(t("Desvinculando cuentas...", "Unlinking accounts...", "Desvinculando contas..."));
                        std::string err = goonies::linkuser::unlinkAccounts();
                        if (err.empty()) {
                            auto* rebootDialog = new brls::Dialog(t("¡Cuentas desvinculadas con éxito! Para aplicar los cambios y evitar que la consola crashee, es obligatorio reiniciar.", "Accounts unlinked successfully! To apply changes and prevent a system crash, you must reboot.", "Contas desvinculadas com sucesso! Para aplicar as alterações e evitar que o console trave, é necessária uma reinicialização."));
                            rebootDialog->addButton(t("Reiniciar", "Reboot", "Reinício"), [] {
                                goonies::linkuser::g_shouldReboot = true;
                                brls::Application::quit();
                            });
                            rebootDialog->open();
                        } else {
                            auto* errorDialog = new brls::Dialog(t(
                                ("Error al desvincular: " + err + "\n\nNOTA: El acceso a los perfiles requiere permisos del sistema. Debes iniciar esta aplicación desde el Álbum (Modo Applet) sin pulsar R para poder desvincular.").c_str(),
                                ("Error unlinking: " + err + "\n\nNOTE: Profiles access requires system permissions. You must launch this app from the Album (Applet Mode) without holding R to unlink.").c_str()
                            ));
                            errorDialog->addButton(t("Aceptar", "OK", "Aceitar"), [] {});
                            errorDialog->open();
                        }
                    });
                    dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), [] {});
                    dialog->open();
                }
            ));

            // 3. Restaurar copia
            buttonsBox->addView(new ModernMenuRow(
                t("Restaurar copia de seguridad", "Restore backup", "Restaurar cópia de segurança"),
                goonies::ui::theme::neonOrange(),
                []() {
                    std::vector<std::string> backups = goonies::linkuser::getBackups();
                    if (backups.empty()) {
                        auto* dialog = new brls::Dialog(t("No se encontraron copias de seguridad en la SD.", "No backups found on the SD card.", "Nenhum backup encontrado no SD."));
                        dialog->addButton(t("Aceptar", "OK", "Aceitar"), [] {});
                        dialog->open();
                        return;
                    }

                    std::string latest_backup = backups.back();
                    auto* dialog = new brls::Dialog(t(
                        ("¿Restaurar la copia de seguridad más reciente?\nArchivo: " + latest_backup + "\nEsto sobrescribirá tus perfiles actuales.").c_str(),
                        ("Restore the most recent backup?\nFile: " + latest_backup + "\nThis will overwrite your current profiles.").c_str()
                    ));
                    dialog->addButton(t("Restaurar", "Restore", "Restaurar"), [latest_backup] {
                        brls::Application::notify(t("Restaurando copia...", "Restoring backup...", "Restaurando cópia..."));
                        std::string err = goonies::linkuser::restoreBackup(latest_backup);
                        if (err.empty()) {
                            auto* rebootDialog = new brls::Dialog(t("¡Copia restaurada con éxito! Para aplicar los cambios y evitar que la consola crashee, es obligatorio reiniciar.", "Backup restored successfully! To apply changes and prevent a system crash, you must reboot.", "Cópia restaurada com sucesso! Para aplicar as alterações e evitar que o console trave, é necessária uma reinicialização."));
                            rebootDialog->addButton(t("Reiniciar", "Reboot", "Reinício"), [] {
                                goonies::linkuser::g_shouldReboot = true;
                                brls::Application::quit();
                            });
                            rebootDialog->open();
                        } else {
                            auto* errorDialog = new brls::Dialog(t(
                                ("Error al restaurar: " + err + "\n\nNOTA: El acceso a los perfiles requiere permisos del sistema. Debes iniciar esta aplicación desde el Álbum (Modo Applet) sin pulsar R para poder restaurar.").c_str(),
                                ("Error restoring: " + err + "\n\nNOTE: Profiles access requires system permissions. You must launch this app from the Album (Applet Mode) without holding R to restore.").c_str()
                            ));
                            errorDialog->addButton(t("Aceptar", "OK", "Aceitar"), [] {});
                            errorDialog->open();
                        }
                    });
                    dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), [] {});
                    dialog->open();
                }
            ));

            centerBox->addView(buttonsBox);
            this->addView(centerBox);
            this->addView(new brls::BottomBar());
        }
        static brls::View* create() { return new LinkGooniesMenuView(); }
    };
}

namespace goonies::ui {

MainMenu::MainMenu(goonies::DownloadManager* manager, goonies::CatalogService* catalog,
                   goonies::GameMetadataService* metadata,
                   goonies::InstalledTitleService* installed, goonies::AppSettings* settings,
                   goonies::HomebrewService* homebrew, goonies::UpdateService* updater)
    : brls::Box(brls::Axis::COLUMN), manager_(manager), catalog_(catalog),
      metadata_(metadata), installed_(installed), settings_(settings), homebrew_(homebrew), updater_(updater) {
    
    this->setBackgroundColor(nvgRGB(18, 19, 24));
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setAlignItems(brls::AlignItems::CENTER);
    centerBox->setJustifyContent(brls::JustifyContent::FLEX_START);
    centerBox->setGrow(1.0f);

    // --- TOP BAR / HUD HEADER ---
    brls::Box* headerBox = new brls::Box(brls::Axis::ROW);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerBox->setWidth(1184);
    headerBox->setMarginTop(10);
    headerBox->setMarginBottom(10);

    // Left brand: Logo + App Title + OLED Pill
    brls::Box* brandBox = new brls::Box(brls::Axis::ROW);
    brandBox->setAlignItems(brls::AlignItems::CENTER);

    brls::Image* logo = new brls::Image();
    logo->setImageFromFile("romfs:/logo.png");
    logo->setWidth(68);
    logo->setHeight(68);
    logo->setMarginRight(18);
    brandBox->addView(logo);

    brls::Box* titleTextBox = new brls::Box(brls::Axis::COLUMN);
    brls::Label* titleLabel = new brls::Label();
    titleLabel->setText("THE GOONIES APP");
    titleLabel->setFontSize(36);
    titleLabel->setTextColor(theme::gold());
    titleTextBox->addView(titleLabel);

    brls::Label* subtitleLabel = new brls::Label();
    subtitleLabel->setText(THEGOONIES_VERSION);
    subtitleLabel->setFontSize(15);
    subtitleLabel->setTextColor(theme::textSecondary());
    titleTextBox->addView(subtitleLabel);

    brandBox->addView(titleTextBox);
    headerBox->addView(brandBox);

    // Right HUD: SD Storage Pill + Provider Pill
    brls::Box* hudBox = new brls::Box(brls::Axis::ROW);
    hudBox->setAlignItems(brls::AlignItems::CENTER);

    // 1. SD Storage space pill
    std::string sdText = getStoragePillText();
    hudBox->addView(createStatusPill(sdText, theme::neonCyan(), nvgRGBA(0, 210, 247, 28)));

    // 2. Download provider pill
    bool isRd = (settings_ && settings_->get().downloadProvider == 1);
    std::string provText = isRd ? "RD VIP" : "TORRENT P2P";
    NVGcolor provColor = isRd ? theme::gold() : theme::neonPurple();
    hudBox->addView(createStatusPill(provText, provColor, nvgRGBAf(provColor.r, provColor.g, provColor.b, 0.20f)));

    headerBox->addView(hudBox);
    centerBox->addView(headerBox);

    // --- CARDS CONTAINER (2 rows of 3 modern cards) ---
    brls::Box* cardsContainer = new brls::Box(brls::Axis::COLUMN);
    cardsContainer->setAlignItems(brls::AlignItems::CENTER);

    // --- ROW 1 ---
    brls::Box* row1 = new brls::Box(brls::Axis::ROW);
    row1->setAlignItems(brls::AlignItems::CENTER);

    auto m_manager = manager_;
    auto m_metadata = metadata_;
    auto m_settings = settings_;
    auto m_catalog = catalog_;
    auto m_installed = installed_;

    // Card 1: Descarga de Juegos (Cyan)
    auto* card1 = new DashboardCard(
        t("Descarga de Juegos", "Download Games", "Baixar jogos"),
        t("Catálogo de títulos, filtros y cola de descargas", "Full catalog, filters and download queue", "Catálogo completo e downloads"),
        t("CATÁLOGO", "CATALOG", "CATÁLOGO"),
        theme::neonCyan(),
        [this, m_manager, m_catalog, m_metadata, m_installed, m_settings]() {
            brls::Application::pushActivity(new brls::Activity(new goonies::ui::CatalogView(
                m_manager, m_catalog, m_metadata, m_installed, m_settings, [m_manager, m_metadata, m_settings]() {
                    brls::Logger::info("User clicked Cola Descargas");
                    brls::Logger::info("Allocating MainView...");
                    auto* mainView = new goonies::ui::MainView(m_manager, m_metadata, m_settings);
                    brls::Logger::info("MainView allocated. Pushing Activity...");
                    brls::Application::pushActivity(new brls::Activity(mainView));
                    brls::Logger::info("Activity pushed successfully.");
                })));
        }
    );
    row1->addView(card1);

    brls::Box* r1_spacer1 = new brls::Box();
    r1_spacer1->setWidth(22);
    row1->addView(r1_spacer1);

    // Card 2: Instalar Juegos (Green)
    auto* card2 = new DashboardCard(
        t("Instalar Juegos", "Install Games", "Instalar jogos"),
        t("Instalar por USB, microSD o protocolo MTP", "Install via USB, microSD or MTP protocol", "Instalar via USB, microSD ou MTP"),
        t("INSTALADOR", "INSTALLER", "INSTALADOR"),
        theme::neonGreen(),
        []() {
            brls::Application::pushActivity(new brls::Activity(goonies::InstallMenuView::create()));
        }
    );
    row1->addView(card2);

    brls::Box* r1_spacer2 = new brls::Box();
    r1_spacer2->setWidth(22);
    row1->addView(r1_spacer2);

    // Card 3: Juegos Instalados (Purple)
    auto* card3 = new DashboardCard(
        t("Juegos Instalados", "Installed Games", "Jogos instalados"),
        t("Gestión de títulos instalados, partidas y DLCs", "Manage installed titles, save data and DLCs", "Gerenciar jogos instalados e saves"),
        t("CONSOLA", "CONSOLE", "CONSOLE"),
        theme::neonPurple(),
        [this, m_installed, m_manager, m_metadata, m_catalog, m_settings]() {
            brls::Application::pushActivity(new brls::Activity(
                new goonies::ui::InstalledView(m_installed, m_manager, m_metadata, m_catalog, m_settings)));
        }
    );
    row1->addView(card3);

    cardsContainer->addView(row1);

    // Spacer between rows
    brls::Box* rowSpacer = new brls::Box();
    rowSpacer->setHeight(14);
    cardsContainer->addView(rowSpacer);

    // --- ROW 2 ---
    brls::Box* row2 = new brls::Box(brls::Axis::ROW);
    row2->setAlignItems(brls::AlignItems::CENTER);

    // Card 4: LinkGoonies (Orange)
    auto* card4 = new DashboardCard(
        "LinkGoonies",
        t("Vincular y desvincular cuentas online y offline", "Link and unlink online and offline accounts", "Vincular e desvincular contas online"),
        t("CUENTAS", "ACCOUNTS", "CONTAS"),
        theme::neonOrange(),
        []() {
            brls::Application::pushActivity(new brls::Activity(goonies::LinkGooniesMenuView::create()));
        }
    );
    row2->addView(card4);

    brls::Box* r2_spacer1 = new brls::Box();
    r2_spacer1->setWidth(22);
    row2->addView(r2_spacer1);

    // Card 5: Crear acceso directo (Gold)
    auto m_homebrew = homebrew_;
    auto* card5 = new DashboardCard(
        t("Crear Accesos", "Create Forwarders", "Criar atalhos"),
        t("Crear forwarders directos en el menú Home", "Create forwarders on the Switch Home menu", "Criar atalhos directos no menu Home"),
        t("FORWARDER", "FORWARDER", "ATALHO"),
        theme::gold(),
        [this, m_homebrew]() {
            brls::Application::pushActivity(new brls::Activity(
                new goonies::ui::ForwardersView("sdmc:/switch/")));
        }
    );
    row2->addView(card5);

    brls::Box* r2_spacer2 = new brls::Box();
    r2_spacer2->setWidth(22);
    row2->addView(r2_spacer2);

    // Card 6: Ajustes & CFW (Pink)
    auto* card6 = new DashboardCard(
        t("Ajustes & CFW", "Settings & CFW", "Ajustes & CFW"),
        t("Opciones generales, Real-Debrid y actualizar CFW", "General options, Real-Debrid and CFW update", "Opções gerais e atualizar CFW"),
        t("SISTEMA", "SYSTEM", "SISTEMA"),
        theme::neonPink(),
        [this]() {
            brls::Application::pushActivity(new brls::Activity(
                new goonies::ui::SettingsView(settings_, manager_, catalog_, metadata_, installed_, updater_)));
        }
    );
    row2->addView(card6);

    // Explicit custom navigation routes for a flawless 2x3 grid
    card1->setCustomNavigationRoute(brls::FocusDirection::RIGHT, card2);
    card1->setCustomNavigationRoute(brls::FocusDirection::DOWN, card4);

    card2->setCustomNavigationRoute(brls::FocusDirection::LEFT, card1);
    card2->setCustomNavigationRoute(brls::FocusDirection::RIGHT, card3);
    card2->setCustomNavigationRoute(brls::FocusDirection::DOWN, card5);

    card3->setCustomNavigationRoute(brls::FocusDirection::LEFT, card2);
    card3->setCustomNavigationRoute(brls::FocusDirection::DOWN, card6);

    card4->setCustomNavigationRoute(brls::FocusDirection::UP, card1);
    card4->setCustomNavigationRoute(brls::FocusDirection::RIGHT, card5);

    card5->setCustomNavigationRoute(brls::FocusDirection::UP, card2);
    card5->setCustomNavigationRoute(brls::FocusDirection::LEFT, card4);
    card5->setCustomNavigationRoute(brls::FocusDirection::RIGHT, card6);

    card6->setCustomNavigationRoute(brls::FocusDirection::UP, card3);
    card6->setCustomNavigationRoute(brls::FocusDirection::LEFT, card5);

    cardsContainer->addView(row2);
    centerBox->addView(cardsContainer);

    // Spacer between cards and credits
    brls::Box* creditsTopSpacer = new brls::Box();
    creditsTopSpacer->setGrow(1.0f);
    centerBox->addView(creditsTopSpacer);

    brls::Label* creditsLabel = new brls::Label();
    creditsLabel->setText(t("Desarrollado para la comunidad Switch ES - The Goonies OS por GoodmanBCN", "Developed for the Switch ES community - The Goonies OS by GoodmanBCN", "Desenvolvido para a comunidade Switch ES - The Goonies OS por GoodmanBCN"));
    creditsLabel->setFontSize(17);
    creditsLabel->setTextColor(theme::textSecondary());
    creditsLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    creditsLabel->setMarginBottom(8);
    centerBox->addView(creditsLabel);

    this->addView(centerBox);
    this->addView(new brls::BottomBar());

    // Register Actions
    this->registerAction(t("Ajustes", "Settings", "Configurações"), brls::BUTTON_BACK, [this](brls::View* view) {
        brls::Application::pushActivity(new brls::Activity(
            new goonies::ui::SettingsView(settings_, manager_, catalog_, metadata_, installed_, updater_)));
        return true;
    }, false); // false = show in hint bar
    
    this->registerAction(t("Salir", "Exit", "Sair"), brls::BUTTON_START, [this](brls::View* view) {
        if (manager_ && manager_->hasActiveTransfer()) {
            brls::Dialog* dialog = new brls::Dialog(t(
                "Hay descargas activas o en cola. Si sales ahora, se pausaran.\n\n¿Seguro que deseas salir?",
                "There are active or queued downloads. Exiting now will pause them.\n\nAre you sure you want to exit?",
                "Existem downloads ativos ou na fila. Sair agora ira pausa-los.\n\nTem certeza de que deseja sair?"));
            dialog->addButton(t("Salir", "Exit", "Sair"), []() {
                brls::Application::quit();
            });
            dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), []() {});
            dialog->open();
        } else {
            brls::Application::quit();
        }
        return true;
    }, false);

    // Auto-update check at startup
    if (updater_) {
        updater_->checkAsync([this](goonies::UpdateCheckResult result) {
            if (!result.ok) {
                // Log failure without popping up toast dialog during startup
                log_msg("[update] Startup check error: %s\n", result.error.c_str());
            } else if (result.updateAvailable) {
                brls::sync([this, release = result.release]() {
                    std::string esText = "Hay una nueva versión de The Goonies APP disponible (" + release.version + "). ¿Deseas descargarla e instalarla ahora?";
                    std::string enText = "A new version of The Goonies APP is available (" + release.version + "). Do you want to download and install it now?";
                    brls::Dialog* dialog = new brls::Dialog(
                        t(esText.c_str(), enText.c_str())
                    );
                    dialog->addButton(t("Actualizar", "Update", "Atualizar"), [this, release]() {
                        brls::Label* progressLabel = new brls::Label();
                        progressLabel->setText(t("Descargando...", "Downloading...", "Baixando..."));
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
                                        brls::Application::notify(t("Error al instalar: ", "Installation error: ", "Erro ao instalar:") + error);
                                        return;
                                    }
                                    
#ifdef __SWITCH__
                                    const std::string staged = updater_->stagedPath();
                                    const std::string target = updater_->targetPath();
                                    const std::string arguments = "\"--finish-update\" \"" + target + "\"";
                                    envSetNextLoad(staged.c_str(), arguments.c_str());
#endif
                                    brls::Application::notify(t("Actualización descargada. Reiniciando...", "Update downloaded. Restarting...", "Atualização baixada. Reinicializando..."));
                                    brls::Application::quit();
                                });
                            });
                        });
                        
                        downloadDialog->open();
                    });
                    dialog->addButton(t("Cancelar", "Cancel", "Cancelar"), []() {});
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
