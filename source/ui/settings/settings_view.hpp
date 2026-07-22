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
        content->setPadding(24, 34, 24, 34);

        addSection(content, t("Consola", "Console"));
        
        auto* langToggle = new brls::SelectorCell();
        langToggle->init(t("Idioma / Language", "Language / Idioma"), {"Español", "English"},
            brls::Platform::APP_LOCALE_DEFAULT == brls::LOCALE_EN_US ? 1 : 0,
            [settings](int selected) {
                brls::Platform::APP_LOCALE_DEFAULT = selected == 1 ? brls::LOCALE_EN_US : brls::LOCALE_ES;
                auto vals = settings->get();
                vals.language = (selected == 1) ? 2 : 1;
                std::string err;
                settings->update(vals, err);
                brls::Application::notify(t("Reinicia la app para aplicar el idioma.", "Restart the app to apply language."));
            });
        content->addView(langToggle);

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
        fwLabel->setFontSize(18);
        fwLabel->setMarginTop(15);
        fwLabel->setMarginBottom(10);
        content->addView(fwLabel);

        addSection(content, t("Almacenamiento (microSD)", "Storage (microSD)"));

        std::error_code ec;
        auto spaceInfo = std::filesystem::space("sdmc:/", ec);
        std::string sdStr = t("No se pudo leer la SD", "Could not read SD");
        if (!ec) {
            double totalGB = static_cast<double>(spaceInfo.capacity) / (1024.0 * 1024.0 * 1024.0);
            double freeGB = static_cast<double>(spaceInfo.available) / (1024.0 * 1024.0 * 1024.0);
            char sd_buf[256];
            snprintf(sd_buf, sizeof(sd_buf), t("Capacidad: %.2f GB / Libre: %.2f GB", "Capacity: %.2f GB / Free: %.2f GB"), totalGB, freeGB);
            sdStr = sd_buf;
        }
        
        auto* sdLabel = new brls::Label();
        sdLabel->setText(sdStr);
        sdLabel->setFontSize(18);
        sdLabel->setMarginBottom(20);
        content->addView(sdLabel);

        addSection(content, t("Mantenimiento", "Maintenance"));

        content->addView(actionCell(t("Limpiar archivos huérfanos", "Clean orphaned files"), "",
            [] { 
                try {
                    u64 freedBytes = 0;
                    int deletedTickets = 0;
                    std::string errorOut;
                    
                    if (CleanupHelper::cleanSystem(errorOut, freedBytes, deletedTickets)) {
                        double sizeMB = freedBytes / (1024.0 * 1024.0);
                        char msg[256];
                        snprintf(msg, sizeof(msg), t("Limpieza completada! Eliminados %d tickets huérfanos y liberados %.2f MB.", "Cleanup completed! Deleted %d orphan tickets and freed %.2f MB."), deletedTickets, sizeMB);
                        brls::Application::notify(msg);
                    } else {
                        brls::Application::notify("Error: " + errorOut);
                    }
                } catch (...) {
                    brls::Application::notify(t("Error al limpiar archivos.", "Error cleaning files."));
                }
            }));

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1);
        scroll->setContentView(content);
        addView(scroll);
        
        addView(new brls::BottomBar());
        
        // Add bottom bar action
        this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [](brls::View*) { 
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
        title->setFontSize(25);
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
        cell->registerClickAction(
            [callback = std::move(callback)](brls::View*) {
                callback();
                return true;
            });
        return cell;
    }

    bool persist(const AppSettingsData& values, const char* tag) {
        std::string error;
        if (settings_->update(values, error))
            return true;
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
                    brls::Application::notify("pipensx is up to date.");
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
                const std::string helper = updater_->helperPath();
                const std::string arguments = helper + " --finish-update";
                const Result result = envSetNextLoad(helper.c_str(),
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
