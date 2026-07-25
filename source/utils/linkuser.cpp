#include "linkuser.hpp"
#include <filesystem>
#include <random>
#include <chrono>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <borealis.hpp>

namespace pipensx::linkuser {

namespace {

#ifdef __SWITCH__
#define ACCOUNT_PATH "account:/su"
#else
#define ACCOUNT_PATH "sdmc:/switch/thegoonies/linkuser/debug"
#endif

const std::string BACKUP_BASE_DIR = "sdmc:/switch/thegoonies/linkuser/backups";

// Predefined headers and JSON template from Linkalho
const uint64_t BAAS_HEADER1 = 0xA5D192EA40AD1304ULL;
const uint64_t BAAS_HEADER2 = 0x0000006E00000001ULL;
const uint64_t BAAS_HEADER3 = 0x0000000100000001ULL;
const std::string PROFILE_JSON_TEMPLATE = R"({"id":"#NAS_ID#","language":"en-US","timezone":"Europe/Lisbon","country":"PT","analyticsOptedIn":false,"gender":"male","emailOptedIn":false,"birthday":"1980-01-01","isChild":false,"email":"•","screenName":"•","region":"","loginId":"•","nickname":"•","isNnLinked":false,"isTwitterLinked":false,"isFacebookLinked":false,"isGoogleLinked":false})";

struct Generator {
    uint64_t nas_id;
    uint64_t baas_user_id;
    std::string nas_id_hex;
    std::string baas_password;
    std::string profile_dat;

    static Generator generate() {
        Generator g;
        // Seed using steady clock and random device
        std::random_device rd;
        std::mt19937_64 engine(rd() ^ std::chrono::steady_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFFULL);

        g.nas_id = dist(engine);
        g.baas_user_id = dist(engine);

        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << g.nas_id;
        g.nas_id_hex = ss.str();

        const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::uniform_int_distribution<size_t> char_dist(0, sizeof(chars) - 2);

        g.baas_password = std::string(40, '\0');
        for (int i = 0; i < 40; ++i) {
            g.baas_password[i] = chars[char_dist(engine)];
        }

        g.profile_dat = std::string(128, '\0');
        for (int i = 0; i < 128; ++i) {
            g.profile_dat[i] = chars[char_dist(engine)];
        }

        return g;
    }
};

std::string stringReplace(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos) return str;
    str.replace(start_pos, from.length(), to);
    return str;
}

// Shuts down account and olsc services (libnx only)
void shutdownServices() {
#ifdef __SWITCH__
    brls::Logger::info("LinkUser: Attempting to shut down account/olsc services...");
    pmshellInitialize();
    pmshellTerminateProgram(0x010000000000001E); // Account service
    pmshellTerminateProgram(0x010000000000003E); // OLSC service
    pmshellExit();
    brls::Logger::info("LinkUser: Shutdown services complete.");
#endif
}

bool copyFileStream(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

void copyDirectoryRecursive(const std::filesystem::path& src, const std::filesystem::path& dest) {
    if (!std::filesystem::exists(src)) return;
    std::filesystem::create_directories(dest);
    for (const auto& entry : std::filesystem::directory_iterator(src)) {
        const auto& path = entry.path();
        auto dest_path = dest / path.filename();
        if (std::filesystem::is_directory(path)) {
            copyDirectoryRecursive(path, dest_path);
        } else {
            copyFileStream(path, dest_path);
        }
    }
}

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&now_time);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

std::string performBackup(const std::string& suffix) {
    try {
        std::string folderName = "profiles_" + getTimestamp() + "_" + suffix;
        std::filesystem::path backupPath = std::filesystem::path(BACKUP_BASE_DIR) / folderName;
        std::filesystem::create_directories(backupPath);

        std::filesystem::path src_path(ACCOUNT_PATH);
        if (std::filesystem::exists(src_path)) {
            // Backup registry.dat
            auto registry_src = src_path / "registry.dat";
            if (std::filesystem::exists(registry_src)) {
                copyFileStream(registry_src, backupPath / "registry.dat");
            }
            // Backup directories
            copyDirectoryRecursive(src_path / "avators", backupPath / "avators");
            copyDirectoryRecursive(src_path / "baas", backupPath / "baas");
            copyDirectoryRecursive(src_path / "nas", backupPath / "nas");
            brls::Logger::info("LinkUser: Profile backup created successfully at %s", backupPath.string().c_str());
        }
        return "";
    } catch (const std::exception& e) {
        brls::Logger::error("LinkUser: Backup failed: %s", e.what());
        return std::string("Backup failed: ") + e.what();
    }
}

} // namespace

std::string linkAccounts() {
    shutdownServices();

    // 1. Mount system save data of the accounts service
#ifdef __SWITCH__
    FsFileSystem acc;
    brls::Logger::info("LinkUser: Attempting to mount system save data...");
    Result rc = fsOpen_SystemSaveData(&acc, FsSaveDataSpaceId_System, 0x8000000000000010, (AccountUid){0});
    if (R_FAILED(rc)) {
        // Fallback to ProperSystem space ID (100) on newer firmwares
        rc = fsOpen_SystemSaveData(&acc, (FsSaveDataSpaceId)100, 0x8000000000000010, (AccountUid){0});
    }
    if (R_FAILED(rc)) {
        brls::Logger::error("LinkUser: Failed to open system save data (0x%08x)", rc);
        return "Failed to open system save data. Error: " + std::to_string(rc);
    }
    fsdevMountDevice("account", acc);
#else
    // PC dummy path setup
    std::filesystem::create_directories(ACCOUNT_PATH);
    std::filesystem::create_directories(std::string(ACCOUNT_PATH) + "/avators");
    // Create dummy registry and avatar for testing on PC
    {
        std::ofstream reg(std::string(ACCOUNT_PATH) + "/registry.dat");
        reg << "dummy_registry";
        std::ofstream av(std::string(ACCOUNT_PATH) + "/avators/0123456789abcdef0123456789abcdef.jpg");
        av << "dummy_avatar";
    }
#endif

    // 2. Perform backup before doing any modifications
    std::string backup_err = performBackup("link");
    if (!backup_err.empty()) {
#ifdef __SWITCH__
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        return backup_err;
    }

    // 3. Perform linking operations
    try {
        std::filesystem::path root(ACCOUNT_PATH);
        std::filesystem::path baas_dir = root / "baas";
        std::filesystem::path nas_dir = root / "nas";

        // Remove and recreate directories to start fresh
        std::filesystem::remove_all(baas_dir);
        std::filesystem::remove_all(nas_dir);
        std::filesystem::create_directories(baas_dir);
        std::filesystem::create_directories(nas_dir);

        // Find all jpg profiles to link
        std::vector<std::filesystem::path> jpg_files;
        if (std::filesystem::exists(root / "avators")) {
            for (const auto& entry : std::filesystem::directory_iterator(root / "avators")) {
                if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
                    jpg_files.push_back(entry.path());
                }
            }
        }

        brls::Logger::info("LinkUser: Found %zu profiles to link.", jpg_files.size());

        for (const auto& file : jpg_files) {
            std::string user_uid = file.stem().string();
            Generator gen = Generator::generate();

            // Write baas/<uid>.dat
            std::filesystem::path baas_file = baas_dir / (user_uid + ".dat");
            std::ofstream out_baas(baas_file, std::ios::binary);
            if (out_baas) {
                out_baas.write(reinterpret_cast<const char*>(&BAAS_HEADER1), sizeof(BAAS_HEADER1));
                out_baas.write(reinterpret_cast<const char*>(&BAAS_HEADER2), sizeof(BAAS_HEADER2));
                out_baas.write(reinterpret_cast<const char*>(&gen.nas_id), sizeof(gen.nas_id));
                out_baas.write(reinterpret_cast<const char*>(&BAAS_HEADER3), sizeof(BAAS_HEADER3));
                out_baas.write(reinterpret_cast<const char*>(&gen.baas_user_id), sizeof(gen.baas_user_id));
                out_baas.write(gen.baas_password.data(), gen.baas_password.size());
            }

            // Write nas/<nas_id>.dat
            std::filesystem::path nas_dat_file = nas_dir / (gen.nas_id_hex + ".dat");
            std::ofstream out_nas_dat(nas_dat_file, std::ios::binary);
            if (out_nas_dat) {
                out_nas_dat.write(gen.profile_dat.data(), gen.profile_dat.size());
            }

            // Write nas/<nas_id>_user.json
            std::filesystem::path nas_json_file = nas_dir / (gen.nas_id_hex + "_user.json");
            std::ofstream out_nas_json(nas_json_file, std::ios::binary);
            if (out_nas_json) {
                std::string json = stringReplace(PROFILE_JSON_TEMPLATE, "#NAS_ID#", gen.nas_id_hex);
                out_nas_json.write(json.data(), json.size());
            }
        }

        brls::Logger::info("LinkUser: Accounts linked successfully.");

        // 4. Commit and unmount
#ifdef __SWITCH__
        fsdevCommitDevice("account");
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        return "";
    } catch (const std::exception& e) {
#ifdef __SWITCH__
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        brls::Logger::error("LinkUser: Error during linking: %s", e.what());
        return std::string("Error during linking: ") + e.what();
    }
}

std::string unlinkAccounts() {
    shutdownServices();

#ifdef __SWITCH__
    FsFileSystem acc;
    brls::Logger::info("LinkUser: Attempting to mount system save data for unlinking...");
    Result rc = fsOpen_SystemSaveData(&acc, FsSaveDataSpaceId_System, 0x8000000000000010, (AccountUid){0});
    if (R_FAILED(rc)) {
        // Fallback to ProperSystem space ID (100) on newer firmwares
        rc = fsOpen_SystemSaveData(&acc, (FsSaveDataSpaceId)100, 0x8000000000000010, (AccountUid){0});
    }
    if (R_FAILED(rc)) {
        brls::Logger::error("LinkUser: Failed to open system save data (0x%08x)", rc);
        return "Failed to open system save data. Error: " + std::to_string(rc);
    }
    fsdevMountDevice("account", acc);
#endif

    // 1. Perform backup
    std::string backup_err = performBackup("unlink");
    if (!backup_err.empty()) {
#ifdef __SWITCH__
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        return backup_err;
    }

    // 2. Perform unlinking operations
    try {
        std::filesystem::path root(ACCOUNT_PATH);
        std::filesystem::path baas_dir = root / "baas";
        std::filesystem::path nas_dir = root / "nas";

        std::filesystem::remove_all(baas_dir);
        std::filesystem::remove_all(nas_dir);

        brls::Logger::info("LinkUser: Accounts unlinked successfully.");

#ifdef __SWITCH__
        fsdevCommitDevice("account");
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        return "";
    } catch (const std::exception& e) {
#ifdef __SWITCH__
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        brls::Logger::error("LinkUser: Error during unlinking: %s", e.what());
        return std::string("Error during unlinking: ") + e.what();
    }
}

std::vector<std::string> getBackups() {
    std::vector<std::string> backups;
    try {
        if (std::filesystem::exists(BACKUP_BASE_DIR)) {
            for (const auto& entry : std::filesystem::directory_iterator(BACKUP_BASE_DIR)) {
                if (entry.is_directory()) {
                    backups.push_back(entry.path().filename().string());
                }
            }
        }
    } catch (const std::exception& e) {
        brls::Logger::error("LinkUser: Failed to read backups list: %s", e.what());
    }
    // Sort backups alphabetically to have consistent ordering
    std::sort(backups.begin(), backups.end());
    return backups;
}

std::string restoreBackup(const std::string& folderName) {
    shutdownServices();

#ifdef __SWITCH__
    FsFileSystem acc;
    brls::Logger::info("LinkUser: Attempting to mount system save data for restoring...");
    Result rc = fsOpen_SystemSaveData(&acc, FsSaveDataSpaceId_System, 0x8000000000000010, (AccountUid){0});
    if (R_FAILED(rc)) {
        // Fallback to ProperSystem space ID (100) on newer firmwares
        rc = fsOpen_SystemSaveData(&acc, (FsSaveDataSpaceId)100, 0x8000000000000010, (AccountUid){0});
    }
    if (R_FAILED(rc)) {
        brls::Logger::error("LinkUser: Failed to open system save data (0x%08x)", rc);
        return "Failed to open system save data. Error: " + std::to_string(rc);
    }
    fsdevMountDevice("account", acc);
#endif

    // Perform backup of current state just in case
    std::string backup_err = performBackup("prerestore");
    if (!backup_err.empty()) {
#ifdef __SWITCH__
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        return backup_err;
    }

    try {
        std::filesystem::path backupPath = std::filesystem::path(BACKUP_BASE_DIR) / folderName;
        if (!std::filesystem::exists(backupPath)) {
#ifdef __SWITCH__
            fsdevUnmountDevice("account");
            fsFsClose(&acc);
#endif
            return "Selected backup folder does not exist.";
        }

        std::filesystem::path dest(ACCOUNT_PATH);

        // Delete current files in accounts partition before restoring
        std::filesystem::remove_all(dest / "baas");
        std::filesystem::remove_all(dest / "nas");
        std::filesystem::remove_all(dest / "avators");
        std::filesystem::remove(dest / "registry.dat");

        // Copy files back
        auto registry_src = backupPath / "registry.dat";
        if (std::filesystem::exists(registry_src)) {
            copyFileStream(registry_src, dest / "registry.dat");
        }
        copyDirectoryRecursive(backupPath / "avators", dest / "avators");
        copyDirectoryRecursive(backupPath / "baas", dest / "baas");
        copyDirectoryRecursive(backupPath / "nas", dest / "nas");

        brls::Logger::info("LinkUser: Restore from backup %s completed successfully.", folderName.c_str());

#ifdef __SWITCH__
        fsdevCommitDevice("account");
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        return "";
    } catch (const std::exception& e) {
#ifdef __SWITCH__
        fsdevUnmountDevice("account");
        fsFsClose(&acc);
#endif
        brls::Logger::error("LinkUser: Restore failed: %s", e.what());
        return std::string("Restore failed: ") + e.what();
    }
}

bool g_shouldReboot = false;

void rebootSystem() {
#ifdef __SWITCH__
    // Try clean system reboot first to prevent audio clicking
    Result rc = appletRequestToReboot();
    if (R_FAILED(rc)) {
        rc = bpcInitialize();
        if (R_SUCCEEDED(rc)) {
            bpcRebootSystem();
            bpcExit();
        }
    }
#endif
}

} // namespace pipensx::linkuser
