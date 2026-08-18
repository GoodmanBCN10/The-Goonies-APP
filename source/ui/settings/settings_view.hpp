#pragma once

#include <sys/statvfs.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#endif

#include <filesystem>

#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/installed_title_service.hpp"
#include "app/update_service.hpp"
#include "ui/common/message_cells.hpp"
#include "ui/common/ui_helpers.hpp"
#include "app/cleanup_helper.hpp"
#include "linkuser.hpp"
#include "ui/mtp/mtp_view.hpp"
#include "ui/settings/rd_auth_dialog.hpp"

namespace pipensx::ui {

class SettingsView : public brls::Box {
public:
    SettingsView(AppSettings* settings, DownloadManager* manager,
                 CatalogService* catalog, GameMetadataService* metadata,
                 InstalledTitleService* installed, UpdateService* updater = nullptr)
        : brls::Box(brls::Axis::COLUMN), settings_(settings), manager_(manager),
          catalog_(catalog), metadata_(metadata), installed_(installed), updater_(updater),
          alive_(std::make_shared<std::atomic<bool>>(true)) {
          
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(20, 34, 20, 34);

        addSection(content, t("Consola", "Console", "Console"));
        
        auto* langToggle = new brls::SelectorCell();
        int currentLangIdx = 0;
        if (brls::Platform::APP_LOCALE_DEFAULT == brls::LOCALE_EN_US) currentLangIdx = 1;
        else if (brls::Platform::APP_LOCALE_DEFAULT == brls::LOCALE_PT_BR) currentLangIdx = 2;
        
        langToggle->init(t("Idioma / Language", "Language / Idioma", "Idioma / Language"), {"Español", "English", "Português"},
            currentLangIdx,
            [settings](int selected) {
                if (selected == 1) brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
                else if (selected == 2) brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_PT_BR;
                else brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ES;
                
                auto vals = settings->get();
                vals.language = selected + 1; // 1=ES, 2=EN, 3=PT
                std::string err;
                settings->update(vals, err);
                brls::Application::notify(t("Reinicia la app para aplicar el idioma.", "Restart the app to apply language.", "Reinicie o aplicativo para aplicar o idioma."));
            });
        langToggle->title->setFontSize(20);
        langToggle->detail->setFontSize(20);
        langToggle->detail->setFontSize(20);
        content->addView(langToggle);

        auto* bgmToggle = new brls::BooleanCell();
        bgmToggle->init(t("Música de fondo", "Background music", "Música de fundo"), settings_->get().enableBackgroundMusic, [settings](bool value) {
            auto vals = settings->get();
            vals.enableBackgroundMusic = value;
            std::string err;
            settings->update(vals, err);
            // The main loop will pick up this change automatically
        });
        bgmToggle->title->setFontSize(20);
        content->addView(bgmToggle);

        // System Firmware Version
        SetSysFirmwareVersion fw;
        std::string fwStr = "Unknown";
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
            char fw_buf[128];
            snprintf(fw_buf, sizeof(fw_buf), "Firmware: %d.%d.%d", fw.major, fw.minor, fw.micro);
            fwStr = fw_buf;
        }
        auto* fwLabel = new brls::Label();
        fwLabel->setText(fwStr);
        fwLabel->setFontSize(20);
        fwLabel->setMarginTop(12);
        fwLabel->setMarginBottom(8);
        content->addView(fwLabel);

        // App Version
        auto* appVerLabel = new brls::Label();
        appVerLabel->setText(std::string(t("Versión de la App: ", "App Version: ", "Versão do aplicativo:")) + PIPENSX_VERSION);
        appVerLabel->setFontSize(20);
        appVerLabel->setMarginTop(8);
        appVerLabel->setMarginBottom(12);
        content->addView(appVerLabel);

        addSection(content, t("Almacenamiento (microSD)", "Storage (microSD)", "Armazenamento (microSD)"));

        std::error_code ec;
        auto spaceInfo = std::filesystem::space("sdmc:/", ec);
        std::string sdStr = t("No se pudo leer la SD", "Could not read SD", "Não foi possível ler SD");
        if (!ec) {
            double totalGB = static_cast<double>(spaceInfo.capacity) / (1024.0 * 1024.0 * 1024.0);
            double freeGB = static_cast<double>(spaceInfo.available) / (1024.0 * 1024.0 * 1024.0);
            char sd_buf[256];
            snprintf(sd_buf, sizeof(sd_buf), t("Capacidad: %.2f GB / Libre: %.2f GB", "Capacity: %.2f GB / Free: %.2f GB", "Capacidade: %,2f GB / Gratuito: %,2f GB"), totalGB, freeGB);
            sdStr = sd_buf;
        }
        
        auto* sdLabel = new brls::Label();
        sdLabel->setText(sdStr);
        sdLabel->setFontSize(20);
        sdLabel->setMarginBottom(12);
        content->addView(sdLabel);

        addSection(content, t("Mantenimiento", "Maintenance", "Manutenção"));

        content->addView(actionCell(t("Limpiar archivos huérfanos", "Clean orphaned files", "Limpe arquivos órfãos"), "",
            [] { 
                try {
                    u64 freedBytes = 0;
                    int deletedTickets = 0;
                    std::string errorOut;
                    
                    if (CleanupHelper::cleanSystem(errorOut, freedBytes, deletedTickets)) {
                        double sizeMB = freedBytes / (1024.0 * 1024.0);
                        char msg[256];
                        snprintf(msg, sizeof(msg), t("Limpieza completada! Eliminados %d tickets huérfanos y liberados %.2f MB.", "Cleanup completed! Deleted %d orphan tickets and freed %.2f MB.", "Limpeza concluída! %d tickets órfãos removidos e %,2f MB liberados."), deletedTickets, sizeMB);
                        brls::Application::notify(msg);
                    } else {
                        brls::Application::notify("Error: " + errorOut);
                    }
                } catch (...) {
                    brls::Application::notify(t("Error al limpiar archivos.", "Error cleaning files.", "Erro ao limpar arquivos."));
                }
            }));

        updateAction_ = actionCell(t("Buscar actualizaciones", "Check for updates", "Verificar atualizações"),
            t("Comprueba si hay una nueva versión de la app.", "Check if there is a new app version.", "Verifique se há uma nova versão do aplicativo."),
            [this] { checkForUpdateNow(); });
        content->addView(updateAction_);


        addSection(content, t("Conectividad", "Connectivity", "Conectividade"));

        // Explorar microSD Cell
        content->addView(actionCell(t("Explorar microSD (Conexión MTP)", "Explore microSD (MTP Connection)", "Explorar microSD (conexão MTP)"),
            t("Inicia la conexión MTP para transferir archivos desde tu PC.", "Starts MTP connection to transfer files from your PC.", "Inicie a conexão MTP para transferir arquivos do seu PC."),
            [] {
                brls::Application::pushActivity(new brls::Activity(new goonies::ui::MTPExplorerView()));
            }));

        auto* providerToggle = new brls::SelectorCell();
        providerToggle->init(t("Motor de Descarga", "Download Engine", "Mecanismo de download"), {"P2P Torrent", "Real-Debrid (Alta Velocidad)"},
            settings_->get().downloadProvider,
            [this](int selected) {
                auto vals = settings_->get();
                vals.downloadProvider = selected;
                std::string err;
                settings_->update(vals, err);
                
                if (manager_) {
                    manager_->setDefaultProvider(selected);
                }
                
                if (selected == 1) {
                    brls::Application::notify(t("Has activado Real-Debrid.", "Real-Debrid activated.", "Você ativou o Real-Debrid."));
                }
            });
        providerToggle->title->setFontSize(20);
        providerToggle->detail->setFontSize(20);
        content->addView(providerToggle);

        // Real-Debrid Auth Cell
        RealDebridProvider tempProvider;
        bool isLinked = tempProvider.IsAuthenticated();
        
        std::string rdTitle = isLinked ? t("Cerrar sesión en Real-Debrid", "Log out of Real-Debrid", "Sair do Real-Debrid") : t("Vincular cuenta de Real-Debrid", "Link Real-Debrid account", "Vincular conta Real-Debrid");
        std::string rdDetail = isLinked ? t("Tu cuenta ya está vinculada. Toca para cerrar sesión.", "Your account is linked. Tap to log out.", "Sua conta agora está vinculada. Toque para sair.") : t("Inicia sesión en Real-Debrid para descargas de alta velocidad.", "Log in to Real-Debrid for high-speed downloads.", "Faça login no Real-Debrid para downloads de alta velocidade.");
        
        content->addView(actionCell(rdTitle, rdDetail,
            [isLinked] {
                if (isLinked) {
                    RealDebridProvider p;
                    p.Logout();
                    brls::Application::notify(t("Sesión de Real-Debrid cerrada.", "Real-Debrid session closed.", "Sessão Real-Debrid encerrada."));
                    brls::Application::popActivity(); // Pop to force refresh
                } else {
                    ShowRealDebridAuthDialog();
                }
            }));

        auto* usbToggle = new brls::SelectorCell();
        usbToggle->init(t("Modo USB", "USB Mode", "Modo USB"), {"USB 2.0 (Estable)", "USB 3.0 (Rápido)"},
            settings_->get().enableUsb30 ? 1 : 0,
            [this](int selected) {
                auto vals = settings_->get();
                vals.enableUsb30 = (selected == 1);
                std::string err;
                settings_->update(vals, err);
                
#ifdef __SWITCH__
                setsysSetUsb30EnableFlag(vals.enableUsb30);
#endif
                
                if (selected == 1) {
                    brls::Application::notify(t("USB 3.0 activado. Puede causar interferencias con mandos Bluetooth.", "USB 3.0 activated. May cause Bluetooth controller interference.", "USB 3.0 habilitado. Pode causar interferência nos controles do Bluetooth."));
                } else {
                    brls::Application::notify(t("USB 2.0 activado (Estable).", "USB 2.0 activated (Stable).", "USB 2.0 ativado (estável)."));
                }
            });
        usbToggle->title->setFontSize(20);
        usbToggle->detail->setFontSize(20);
        content->addView(usbToggle);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1);
        scroll->setContentView(content);
        addView(scroll);
        
        addView(new brls::BottomBar());
        
        // Add bottom bar action
        this->registerAction(t("Volver", "Back", "Retornar"), brls::BUTTON_B, [](brls::View*) { 
            brls::Application::popActivity();
            return true; 
        });
    }

    ~SettingsView() override {
        alive_->store(false);
    }

private:
    static void addSection(brls::Box* content, const std::string& text) {
        auto* title = new brls::Label();
        title->setText(text);
        title->setFontSize(20);
        title->setMarginTop(14);
        title->setMarginBottom(8);
        content->addView(title);
    }

    static brls::DetailCell* actionCell(const std::string& title,
                                        const std::string& detail,
                                        std::function<void()> callback) {
        auto* cell = new brls::DetailCell();
        cell->setText(title);
        cell->setDetailText(detail);
        cell->title->setFontSize(20);
        cell->detail->setFontSize(20);
        cell->registerClickAction(
            [callback = std::move(callback)](brls::View*) {
                callback();
                return true;
            });
        return cell;
    }

    bool persist(const AppSettingsData& values, const char* tag) {
        std::string error;
        if (settings_->update(values, error)) {
            manager_->setDefaultProvider(values.downloadProvider);
            return true;
        }
        diagnostic_error("settings", tag, "error=%s", error.c_str());
        brls::Application::notify(error);
        return false;
    }

    void recordRefreshTime(bool catalog, bool metadata) {
        AppSettingsData values = settings_->get();
        const uint64_t now = now_ms();
        if (catalog)
            values.lastCatalogRefreshMs = now;
        if (metadata)
            values.lastMetadataRefreshMs = now;
        persist(values, catalog ? "catalog_refresh_time"
                                : "metadata_refresh_time");
    }

    void refreshCatalogNow() {
        if (refreshInFlight_)
            return;
        refreshInFlight_ = true;
        brls::Application::notify("Updating catalog from Langegen...");
        auto alive = alive_;
        CatalogService* catalog = catalog_;
        brls::async([this, alive, catalog] {
            std::vector<CatalogEntry> entries;
            std::string error;
            bool ok = catalog->fetchLatest(entries, error);
            brls::sync([this, alive, ok, entries = std::move(entries),
                        error = std::move(error)]() mutable {
                if (!alive->load())
                    return;
                refreshInFlight_ = false;
                if (!ok) {
                    diagnostic_error("catalog", "settings_refresh", "error=%s",
                                     error.c_str());
                    brls::Application::notify(error);
                    return;
                }
                catalog_->adopt(std::move(entries));
                recordRefreshTime(true, false);
                brls::Application::notify(
                    "Catalog updated: " +
                    std::to_string(catalog_->entries().size()) + " entries.");
            });
        });
    }

    void refreshMetadataNow() {
        if (refreshInFlight_ || !metadata_)
            return;
        refreshInFlight_ = true;
        brls::Application::notify("Updating artwork metadata...");
        auto alive = alive_;
        GameMetadataService* metadata = metadata_;
        brls::async([this, alive, metadata] {
            MetadataSnapshot snapshot;
            std::string error;
            bool ok = metadata->fetchLatest(snapshot, error);
            brls::sync([this, alive, ok, snapshot = std::move(snapshot),
                        error = std::move(error)]() mutable {
                if (!alive->load())
                    return;
                refreshInFlight_ = false;
                if (!ok) {
                    diagnostic_error("metadata", "settings_refresh",
                                     "error=%s", error.c_str());
                    brls::Application::notify(error);
                    return;
                }
                metadata_->adopt(std::move(snapshot));
                metadata_->dropMemoryImageCache();
                recordRefreshTime(false, true);
                brls::Application::notify(
                    "Artwork metadata updated: " +
                    std::to_string(metadata_->size()) + " matches.");
            });
        });
    }

    void checkForUpdateNow() {
        if (updateInFlight_ || !updater_)
            return;
        updateInFlight_ = true;
        updateAction_->setDetailText("Checking...");
        auto alive = alive_;
        UpdateService* updater = updater_;
        updater->checkAsync([this, alive](UpdateCheckResult result) {
            brls::sync([this, alive, result = std::move(result)]() mutable {
                if (!alive->load())
                    return;
                updateInFlight_ = false;
                if (!result.ok) {
                    updateAction_->setDetailText("Check failed");
                    diagnostic_error("update", "check", "error=%s",
                                     result.error.c_str());
                    brls::Application::notify(result.error);
                    return;
                }
                if (!result.updateAvailable) {
                    updateAction_->setDetailText("Up to date");
                    brls::Application::notify("The Goonies APP is up to date.");
                    return;
                }
                updateAction_->setDetailText("Version " + result.release.version);
                confirmInstallUpdate(std::move(result.release));
            });
        });
    }

    void confirmInstallUpdate(ReleaseInfo release) {
        auto* dialog = new brls::Dialog(
            "pipensx " + release.version + " is available. Install it now?");
        dialog->addButton("Install and restart", [this, release = std::move(release)] {
            installUpdate(release);
        });
        dialog->addButton("Later", [] {});
        dialog->open();
    }

    void installUpdate(const ReleaseInfo& release) {
        if (updateInFlight_ || !updater_)
            return;
        updateInFlight_ = true;
        updateAction_->setDetailText("Downloading...");
        auto alive = alive_;
        UpdateService* updater = updater_;
        auto lastPercent = std::make_shared<std::atomic<int>>(-1);
        updater->onInstallProgress(
            [this, alive, lastPercent](uint64_t received, uint64_t total) {
                const int percent = static_cast<int>((received * 100) / total);
                if (lastPercent->exchange(percent) == percent)
                    return;
                brls::sync([this, alive, percent] {
                    if (!alive->load())
                        return;
                    updateAction_->setDetailText(
                        "Downloading... " + std::to_string(percent) + "%");
                });
            });
        updater->installAsync(release, [this, alive](bool installed,
                                                       std::string error) {
            brls::sync([this, alive, installed, error = std::move(error)] {
                if (!alive->load())
                    return;
                updateInFlight_ = false;
                if (!installed) {
                    updateAction_->setDetailText("Install failed");
                    diagnostic_error("update", "install", "error=%s",
                                     error.c_str());
                    brls::Application::notify(error);
                    return;
                }
                updateAction_->setDetailText("Restart required");
#ifdef __SWITCH__
                if (!envHasNextLoad()) {
                    brls::Application::notify(
                        "Update downloaded, but this loader cannot restart it.");
                    return;
                }
                const std::string temp = updater_->stagedPath();
                const std::string target = updater_->targetPath();
                // hbloader REQUIRES argv to start with the executable path!
                const std::string arguments = "\"" + temp + "\" \"--finish-update\" \"" + target + "\"";
                const Result result = envSetNextLoad(temp.c_str(),
                                                     arguments.c_str());
                if (R_FAILED(result)) {
                    diagnostic_error("update", "restart", "result=0x%08x",
                                     result);
                    brls::Application::notify(
                        "Update downloaded, but restart setup failed.");
                    return;
                }
#endif
                brls::Application::notify(
                    "Update downloaded. pipensx will restart to install it.");
                brls::Application::quit();
            });
        });
    }

    void applyValues() {
        const AppSettingsData& values = settings_->get();
        catalogFilter_->setSelection(
            values.catalogFilter == CatalogFilter::Games ? 1 : 0, true);
        refreshCatalog_->setOn(values.refreshCatalogOnLaunch, false);
        streamSelection_->setSelection(
            values.streamSelection == StreamSelection::PackagesOnly ? 1 : 0,
            true);
        installLocation_->setSelection(
            values.installLocation == InstallLocation::SystemMemory ? 1 : 0,
            true);
        manager_->setInstallTarget(installTargetFor(values.installLocation));
        showCompleted_->setOn(values.showCompletedDownloads, false);
        extendedTelemetry_->setOn(values.extendedTelemetry, false);
        checkForUpdates_->setOn(values.checkForUpdatesOnLaunch, false);
        telemetry_set_enabled(values.extendedTelemetry ? 1 : 0);
    }

    void captureSnapshot() {
        size_t active = 0;
        size_t errors = 0;
        for (const DownloadTask& task : manager_->snapshot()) {
            if (task.status == DownloadStatus::Error)
                ++errors;
            else if (task.status != DownloadStatus::Completed &&
                     task.status != DownloadStatus::Installed &&
                     task.status != DownloadStatus::Paused)
                ++active;
        }
        uint64_t freeBytes = 0;
        struct statvfs storage {};
        if (statvfs("sdmc:/", &storage) == 0)
            freeBytes = static_cast<uint64_t>(storage.f_bavail) *
                        static_cast<uint64_t>(storage.f_frsize);
        uint32_t hos = hosversionGet();
        diagnostic_snapshot("system", "manual",
            "version=%s hos=%u.%u.%u operation_mode=%d telemetry=%d "
            "catalog=%zu metadata=%zu installed=%zu active=%zu errors=%zu "
            "sd_free_bytes=%llu",
            PIPENSX_VERSION, HOSVER_MAJOR(hos), HOSVER_MINOR(hos),
            HOSVER_MICRO(hos), static_cast<int>(appletGetOperationMode()),
            telemetry_enabled(), catalog_->entries().size(), metadata_->size(),
            installed_->titles().size(), active, errors,
            static_cast<unsigned long long>(freeBytes));
        brls::Application::notify("Diagnostic snapshot written to pipensx.log.");
    }

    void confirmClearLog() {
        auto* dialog = new brls::Dialog("Clear pipensx.log now?");
        dialog->addButton("Clear log", [] {
            if (!clearApplicationLog())
                brls::Application::notify("Unable to clear pipensx.log.");
            else
                brls::Application::notify("Log cleared.");
        });
        dialog->addButton("Cancel", [] {});
        dialog->open();
    }

    void confirmClearArtwork() {
        auto* dialog = new brls::Dialog("Clear downloaded artwork cache?");
        dialog->addButton("Clear artwork", [this] {
            std::string error;
            if (!metadata_->clearImageCache(error)) {
                diagnostic_error("metadata", "clear_cache", "error=%s",
                                 error.c_str());
                brls::Application::notify(error);
            } else {
                brls::Application::notify("Artwork cache cleared.");
            }
        });
        dialog->addButton("Cancel", [] {});
        dialog->open();
    }

    void confirmReset() {
        auto* dialog = new brls::Dialog("Reset all pipensx settings?");
        dialog->addButton("Reset settings", [this] {
            std::string error;
            if (!settings_->reset(error)) {
                diagnostic_error("settings", "reset", "error=%s",
                                 error.c_str());
                brls::Application::notify(error);
                return;
            }
            applyValues();
            brls::Application::notify("Settings restored to defaults.");
        });
        dialog->addButton("Cancel", [] {});
        dialog->open();
    }

    AppSettings* settings_;
    DownloadManager* manager_;
    CatalogService* catalog_;
    GameMetadataService* metadata_;
    InstalledTitleService* installed_;
    UpdateService* updater_;
    std::shared_ptr<std::atomic<bool>> alive_;
    brls::SelectorCell* catalogFilter_ = nullptr;
    brls::BooleanCell* refreshCatalog_ = nullptr;
    brls::BooleanCell* checkForUpdates_ = nullptr;
    brls::DetailCell* updateAction_ = nullptr;
    brls::SelectorCell* streamSelection_ = nullptr;
    brls::SelectorCell* installLocation_ = nullptr;
    brls::BooleanCell* showCompleted_ = nullptr;
    brls::BooleanCell* extendedTelemetry_ = nullptr;
    bool refreshInFlight_ = false;
    bool updateInFlight_ = false;
};

}  // namespace pipensx::ui
