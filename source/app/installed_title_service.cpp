#include "installed_title_service.hpp"

extern "C" {
#include "../core/util.h"
}

#include <switch.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include "../app_state.hpp"

namespace goonies {
namespace {

std::string resultText(const char* operation, Result result) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s (0x%08x).", operation, result);
    return text;
}

std::string boundedText(const char* value, size_t size) {
    if (!value || size == 0)
        return {};
    size_t length = strnlen(value, size);
    return std::string(value, length);
}

std::string upperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::toupper(c));
                   });
    return value;
}

bool writeIconIfMissing(const std::string& path, const uint8_t* bytes,
                        size_t size) {
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0)
        return true;
    if (!bytes || size < 8)
        return false;
    std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(reinterpret_cast<const char*>(bytes),
                     static_cast<std::streamsize>(size));
        output.flush();
        if (!output.good()) {
            unlink(temporary.c_str());
            return false;
        }
    }
    if (rename(temporary.c_str(), path.c_str()) == 0)
        return true;
    if ((unlink(path.c_str()) == 0 || errno == ENOENT) &&
        rename(temporary.c_str(), path.c_str()) == 0)
        return true;
    unlink(temporary.c_str());
    return false;
}

} // namespace

InstalledTitleService::InstalledTitleService(std::string rootPath)
    : rootPath_(std::move(rootPath)),
      iconRoot_(rootPath_ + "/installed-icons") {
    mkdir(iconRoot_.c_str(), 0755);
}

std::string InstalledTitleService::formatTitleId(uint64_t applicationId) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llX",
                  static_cast<unsigned long long>(applicationId));
    return text;
}

bool InstalledTitleService::contains(const std::string& titleId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !titleId.empty() && titleIds_.count(upperAscii(titleId)) != 0;
}

std::optional<InstalledTitle> InstalledTitleService::find(const std::string& titleId) const {
    if (titleId.empty()) return std::nullopt;
    std::string upper = upperAscii(titleId);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : titles_) {
        if (upperAscii(t.titleId) == upper)
            return t;
    }
    return std::nullopt;
}

std::optional<InstalledTitle> InstalledTitleService::findByApplicationId(uint64_t appId) const {
    if (appId == 0) return std::nullopt;
    uint64_t baseId = appId & ~0x1FFFULL;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : titles_) {
        if ((t.applicationId & ~0x1FFFULL) == baseId)
            return t;
    }
    return std::nullopt;
}

uint32_t InstalledTitleService::getInstalledVersion(uint64_t appId) const {
    auto title = findByApplicationId(appId);
    return title ? title->internalVersion : 0;
}

bool InstalledTitleService::isPackageInstalled(const PackageInfo& pkg) const {
    uint64_t baseId = pkg.baseTitleId;
    if (baseId == 0 && pkg.titleId != 0) {
        baseId = pkg.titleId & ~0x1FFFULL;
    }
    if (baseId == 0) return false;

    std::string baseIdStr = formatTitleId(baseId);
    bool hasBase = contains(baseIdStr);
    if (!hasBase) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& t : titles_) {
            if ((t.applicationId & ~0x1FFFULL) == baseId) {
                hasBase = true;
                break;
            }
        }
    }
    if (!hasBase) {
        return false;
    }

    if (pkg.kind == PackageKind::BaseGame) {
        return true;
    }

    if (pkg.kind == PackageKind::Update) {
        uint32_t installedVer = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& t : titles_) {
                if ((t.applicationId & ~0x1FFFULL) == baseId) {
                    installedVer = t.internalVersion;
                    break;
                }
            }
        }
        if (pkg.version > 0) {
            return installedVer >= pkg.version;
        }
        return (installedVer > 0);
    }

    if (pkg.kind == PackageKind::DLC) {
        if (pkg.titleId != 0) {
#if defined(__SWITCH__)
            NcmStorageId storages[] = {NcmStorageId_SdCard, NcmStorageId_BuiltInUser};
            for (int s = 0; s < 2; s++) {
                NcmContentMetaDatabase db;
                if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storages[s]))) {
                    NcmContentMetaKey key;
                    Result rc = ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, pkg.titleId);
                    ncmContentMetaDatabaseClose(&db);
                    if (R_SUCCEEDED(rc)) return true;
                }
            }
#endif
        }
        return false;
    }

    return false;
}

std::vector<InstalledTitle> InstalledTitleService::titles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return titles_;
}

uint64_t InstalledTitleService::generation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

bool InstalledTitleService::refresh(std::string& error) {
    std::lock_guard<std::mutex> refreshLock(refreshMutex_);
    const uint64_t startedMs = now_ms();
    error.clear();
    
    Result pdm_rc = pdmqryInitialize();
    bool has_pdm = R_SUCCEEDED(pdm_rc);
    bool has_ncm = R_SUCCEEDED(ncmInitialize());
    std::vector<NsApplicationRecord> records;
    constexpr s32 PageSize = 64;
    s32 offset = 0;

    std::ofstream logOut("sdmc:/switch/thegoonies/logs/debug_log.txt", std::ios::app);
    auto writeLog = [&](const std::string& msg) {
        logOut << msg << std::endl;
    };

    writeLog("InstalledTitleService: starting nsListApplicationRecord loop");

    while (records.size() < 4096) {
        std::array<NsApplicationRecord, PageSize> page {};
        s32 returned = 0;
        Result rc = nsListApplicationRecord(page.data(), PageSize, offset,
                                            &returned);
        if (R_FAILED(rc)) {
            error = resultText("Unable to list installed applications", rc);
            diagnostic_error("installed", "list", "result=0x%08x", rc);
            writeLog("InstalledTitleService: nsListApplicationRecord FAILED: " + std::to_string(rc));
            return false;
        }
        if (returned <= 0)
            break;
        returned = std::min(returned, PageSize);
        records.insert(records.end(), page.begin(), page.begin() + returned);
        offset += returned;
        if (returned < PageSize)
            break;
    }

    writeLog("InstalledTitleService: found " + std::to_string(records.size()) + " records. Starting control data loop.");

    NcmContentMetaDatabase db_sd{};
    bool has_db_sd = has_ncm && R_SUCCEEDED(ncmOpenContentMetaDatabase(&db_sd, NcmStorageId_SdCard));
    NcmContentMetaDatabase db_nand{};
    bool has_db_nand = has_ncm && R_SUCCEEDED(ncmOpenContentMetaDatabase(&db_nand, NcmStorageId_BuiltInUser));

    std::vector<InstalledTitle> next;
    next.reserve(records.size());
    auto control = std::make_unique<NsApplicationControlData>();
    for (const NsApplicationRecord& record : records) {
        if (g_appExiting) break;
        InstalledTitle title;
        title.applicationId = record.application_id;
        title.titleId = formatTitleId(record.application_id);
        title.name = title.titleId;
        title.iconPath = iconRoot_ + "/" + title.titleId + ".jpg";

        std::memset(control.get(), 0, sizeof(*control));
        u64 actualSize = 0;
        Result rc = nsGetApplicationControlData(
            NsApplicationControlSource_CacheOnly, record.application_id,
            control.get(), sizeof(*control), &actualSize);
        if (R_FAILED(rc)) {
            rc = nsGetApplicationControlData(
                NsApplicationControlSource_Storage, record.application_id,
                control.get(), sizeof(*control), &actualSize);
        }
        if (R_SUCCEEDED(rc)) {
            title.displayVersion = boundedText(control->nacp.display_version, sizeof(control->nacp.display_version));
            NacpLanguageEntry* language = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &language)) &&
                language) {
                std::string name = boundedText(language->name,
                                               sizeof(language->name));
                if (!name.empty())
                    title.name = std::move(name);
                title.publisher = boundedText(language->author,
                                              sizeof(language->author));
            }
            size_t iconSize = actualSize > sizeof(NacpStruct)
                ? static_cast<size_t>(actualSize - sizeof(NacpStruct)) : 0;
            iconSize = std::min(iconSize, sizeof(control->icon));
            if (!writeIconIfMissing(title.iconPath, control->icon, iconSize))
                title.iconPath.clear();
        } else {
            title.iconPath.clear();
            diagnostic_error("installed", title.titleId.c_str(),
                             "event=control_data result=0x%08x", rc);
        }
        
        uint32_t installedVer = 0;
        auto queryDb = [&](NcmContentMetaDatabase* db) {
            s32 total = 0, written = 0;
            std::vector<NcmContentMetaKey> keys(4);
            if (R_SUCCEEDED(ncmContentMetaDatabaseList(db, &total, &written, keys.data(), keys.size(),
                                                       NcmContentMetaType_Unknown, record.application_id, 0, UINT64_MAX,
                                                       NcmContentInstallType_Full))) {
                if (total > static_cast<s32>(keys.size())) {
                    keys.resize(total);
                    ncmContentMetaDatabaseList(db, &total, &written, keys.data(), keys.size(),
                                               NcmContentMetaType_Unknown, record.application_id, 0, UINT64_MAX,
                                               NcmContentInstallType_Full);
                }
                for (s32 i = 0; i < written; ++i) {
                    if (keys[i].type == NcmContentMetaType_Patch) {
                        if (keys[i].version > installedVer) installedVer = keys[i].version;
                    } else if (keys[i].type == NcmContentMetaType_Application) {
                        if (keys[i].version > installedVer) installedVer = keys[i].version;
                    }
                }
            }
        };
        if (has_db_sd) queryDb(&db_sd);
        if (has_db_nand) queryDb(&db_nand);

        // Always check GetLatestContentMetaKey for patchId to ensure installed update version is captured
        uint64_t patchId = (record.application_id & ~0x1FFFULL) | 0x800ULL;
        NcmContentMetaKey pKey;
        if (has_db_sd && R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db_sd, &pKey, patchId))) {
            if (pKey.version > installedVer) installedVer = pKey.version;
        }
        if (has_db_nand && R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db_nand, &pKey, patchId))) {
            if (pKey.version > installedVer) installedVer = pKey.version;
        }

        title.internalVersion = installedVer;
        
        if (has_pdm) {
            PdmPlayStatistics stats;
            if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(record.application_id, false, &stats))) {
                title.playtimeMinutes = stats.playtime / 60000000000ULL; // nanoseconds to minutes
                title.lastPlayedTimestamp = stats.last_timestamp_user;
                title.installTimestamp = stats.first_timestamp_user;
            }
        }
        
        next.push_back(std::move(title));
    }

    if (has_pdm) {
        pdmqryExit();
    }
    if (has_db_sd) {
        ncmContentMetaDatabaseClose(&db_sd);
    }
    if (has_db_nand) {
        ncmContentMetaDatabaseClose(&db_nand);
    }
    if (has_ncm) {
        ncmExit();
    }

    std::stable_sort(next.begin(), next.end(),
                     [](const InstalledTitle& left,
                        const InstalledTitle& right) {
                         return left.name < right.name;
                     });
    std::unordered_set<std::string> ids;
    ids.reserve(next.size());
    for (const InstalledTitle& title : next)
        ids.insert(title.titleId);
    const size_t count = next.size();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        titles_ = std::move(next);
        titleIds_ = std::move(ids);
        ++generation_;
    }
    log_msg("[installed] loaded %zu applications\n", count);
    telemetry_log("installed", "system",
                  "event=refresh count=%zu duration_ms=%llu", count,
                  static_cast<unsigned long long>(now_ms() - startedMs));
    return true;
}

bool InstalledTitleService::launchTitle(uint64_t applicationId) {
    Result rc = appletRequestLaunchApplication(applicationId, NULL);
    return R_SUCCEEDED(rc);
}

bool InstalledTitleService::deleteTitle(uint64_t applicationId) {
    if (R_SUCCEEDED(ncmInitialize())) {
        nsInitialize();
        nsDeleteApplicationCompletely(applicationId);
        nsExit();
        ncmExit();
        return true;
    }
    return false;
}

} // namespace goonies
