#include "save_manager.hpp"

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>

namespace pipensx::saves {

static bool CopyDirectoryRecursive(const std::string& src, const std::string& dst, std::string& errorOut) {
    std::error_code ec;
    if (!std::filesystem::exists(dst, ec)) {
        std::filesystem::create_directories(dst, ec);
    }

    DIR* dir = opendir(src.c_str());
    if (!dir) {
        errorOut = "Failed to open directory: " + src;
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename == "." || filename == "..") {
            continue;
        }

        std::string srcPath = src + "/" + filename;
        std::string dstPath = dst + "/" + filename;

        struct stat st;
        if (stat(srcPath.c_str(), &st) != 0) {
            errorOut = "Failed to stat: " + srcPath;
            closedir(dir);
            return false;
        }

        if (S_ISDIR(st.st_mode)) {
            if (!CopyDirectoryRecursive(srcPath, dstPath, errorOut)) {
                closedir(dir);
                return false;
            }
        } else {
            std::ifstream source(srcPath, std::ios::binary);
            std::ofstream dest(dstPath, std::ios::binary);

            if (!source.is_open()) {
                errorOut = "Failed to open source file: " + srcPath;
                closedir(dir);
                return false;
            }
            if (!dest.is_open()) {
                errorOut = "Failed to open dest file: " + dstPath;
                closedir(dir);
                return false;
            }

            dest << source.rdbuf();

            if (!dest.good()) {
                errorOut = "Error writing file: " + dstPath;
                closedir(dir);
                return false;
            }
        }
    }
    closedir(dir);
    return true;
}

std::string SaveManager::CleanGameName(const std::string& name) {
    std::string clean = name;
    std::replace(clean.begin(), clean.end(), '/', '_');
    std::replace(clean.begin(), clean.end(), '\\', '_');
    std::replace(clean.begin(), clean.end(), ':', '_');
    std::replace(clean.begin(), clean.end(), '*', '_');
    std::replace(clean.begin(), clean.end(), '?', '_');
    std::replace(clean.begin(), clean.end(), '"', '_');
    std::replace(clean.begin(), clean.end(), '<', '_');
    std::replace(clean.begin(), clean.end(), '>', '_');
    std::replace(clean.begin(), clean.end(), '|', '_');
    return clean;
}

std::string SaveManager::GetBackupPath(uint64_t titleId, const std::string& gameName) {
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(16) << std::hex << titleId;
    std::string titleIdHex = stream.str();
    
    std::string cleanName = CleanGameName(gameName);
    if (cleanName.empty()) {
        cleanName = "Unknown";
    }
    
    return "sdmc:/switch/thegoonies/saves/" + cleanName + " [" + titleIdHex + "]";
}

bool SaveManager::HasBackup(uint64_t titleId, const std::string& gameName) {
    std::string backupPath = GetBackupPath(titleId, gameName);
    return std::filesystem::exists(backupPath);
}

bool SaveManager::BackupSave(uint64_t titleId, const std::string& gameName, std::string& outMessage) {
    AccountUid uid;
    bool userFound = false;
    if (R_SUCCEEDED(accountGetPreselectedUser(&uid))) {
        userFound = true;
    } else {
        AccountUid uids[8];
        s32 num_uids = 0;
        if (R_SUCCEEDED(accountListAllUsers(uids, 8, &num_uids)) && num_uids > 0) {
            uid = uids[0];
            userFound = true;
        }
    }

    if (!userFound) {
        outMessage = "Error: No se ha encontrado un perfil de usuario activo.";
        return false;
    }

    if (R_SUCCEEDED(fsdevMountSaveData("save", titleId, uid))) {
        std::string destPath = GetBackupPath(titleId, gameName);
        std::error_code ec;
        std::filesystem::create_directories(destPath, ec);
        
        std::string errorOut;
        bool copyOk = CopyDirectoryRecursive("save:/", destPath, errorOut);

        fsdevCommitDevice("save");
        fsdevUnmountDevice("save");
        
        if (!copyOk) {
            outMessage = "Error al copiar: " + errorOut;
            return false;
        }
        
        outMessage = "Partida guardada en la SD con exito!";
        return true;
    } else {
        outMessage = "Error: No se pudo montar la partida (Posiblemente no exista).";
        return false;
    }
}

bool SaveManager::RestoreSave(uint64_t titleId, const std::string& gameName, std::string& outMessage) {
    AccountUid uid;
    bool userFound = false;
    if (R_SUCCEEDED(accountGetPreselectedUser(&uid))) {
        userFound = true;
    } else {
        AccountUid uids[8];
        s32 num_uids = 0;
        if (R_SUCCEEDED(accountListAllUsers(uids, 8, &num_uids)) && num_uids > 0) {
            uid = uids[0];
            userFound = true;
        }
    }

    if (!userFound) {
        outMessage = "Error: No se ha encontrado un perfil de usuario activo.";
        return false;
    }

    std::string srcPath = GetBackupPath(titleId, gameName);
    if (!std::filesystem::exists(srcPath)) {
        outMessage = "Error: No se encontro ninguna copia de seguridad en la SD.";
        return false;
    }

    if (R_SUCCEEDED(fsdevMountSaveData("save", titleId, uid))) {
        std::string errorOut;
        bool copyOk = CopyDirectoryRecursive(srcPath, "save:/", errorOut);

        fsdevCommitDevice("save");
        fsdevUnmountDevice("save");
        
        if (!copyOk) {
            outMessage = "Error al restaurar: " + errorOut;
            return false;
        }
        
        outMessage = "Partida restaurada con exito!";
        return true;
    } else {
        outMessage = "Error: No se pudo montar la partida destino en la consola.";
        return false;
    }
}

bool SaveManager::DeleteBackup(uint64_t titleId, const std::string& gameName, std::string& outMessage) {
    std::string backupPath = GetBackupPath(titleId, gameName);
    if (!std::filesystem::exists(backupPath)) {
        outMessage = "No existe copia de seguridad para eliminar.";
        return false;
    }
    
    std::error_code ec;
    std::uintmax_t deleted = std::filesystem::remove_all(backupPath, ec);
    if (ec) {
        outMessage = "Error al eliminar: " + ec.message();
        return false;
    }
    
    outMessage = "Copia de seguridad eliminada con exito!";
    return true;
}

} // namespace pipensx::saves
