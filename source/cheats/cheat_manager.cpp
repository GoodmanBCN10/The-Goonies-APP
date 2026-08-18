#include "cheats/cheat_manager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <dirent.h>

// Helper to check if a directory/file exists
static bool pathExists(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0);
}

// Helper to check if a path is a directory
static bool isDir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

// Simple recursive mkdir
static void createDirectories(const std::string& path) {
    size_t pos = 0;
    std::string currentPath;
    while ((pos = path.find_first_of("/\\", pos + 1)) != std::string::npos) {
        currentPath = path.substr(0, pos);
        if (!currentPath.empty() && currentPath != "sdmc:") {
            mkdir(currentPath.c_str(), 0777);
        }
    }
    mkdir(path.c_str(), 0777);
}

// Simple get parent path
static std::string getParentPath(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
    return "";
}

namespace pipensx::cheats {

void CheatManager::init() {
    std::string masterPath = getMasterCheatsPath();
    if (!pathExists(masterPath)) {
        createDirectories(masterPath);
    }
}

bool CheatManager::hasMasterDatabase() {
    std::string masterPath = getMasterCheatsPath();
    // A simple check to see if it has folders inside (the title IDs)
    if (!pathExists(masterPath)) return false;
    
    DIR* dir = opendir(masterPath.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            
            std::string fullPath = masterPath + "/" + name;
            if (isDir(fullPath)) {
                closedir(dir);
                return true;
            }
        }
        closedir(dir);
    }
    return false;
}

void CheatManager::downloadDatabase(std::function<void(bool success, const std::string& error)> callback) {
    // This will be integrated with DownloadManager to download HamletDuFromage/switch-cheats-db
    // For now, returning success as placeholder since download manager integration will take more code.
    // The implementation will:
    // 1. Download zip from https://github.com/HamletDuFromage/switch-cheats-db/releases/latest/download/cheats.zip
    // 2. Extract contents to getMasterCheatsPath()
    
    // Placeholder callback for compilation
    if (callback) {
        callback(false, "Download integration not yet fully implemented.");
    }
}

std::string CheatManager::getMasterCheatsPath() const {
    return "sdmc:/switch/thegoonies/cheats";
}

std::string CheatManager::getAtmosphereCheatsPath(uint64_t titleId, const std::string& buildId) const {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << std::uppercase << titleId;
    std::string tidStr = ss.str();
    
    return "sdmc:/atmosphere/contents/" + tidStr + "/cheats/" + buildId + ".txt";
}

std::vector<CheatEntry> CheatManager::parseCheatFile(const std::string& filepath) {
    std::vector<CheatEntry> cheats;
    if (!pathExists(filepath)) {
        return cheats;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) return cheats;

    std::string line;
    CheatEntry currentCheat;
    bool inCheat = false;

    while (std::getline(file, line)) {
        // Trim whitespace and remove carriage returns
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            if (inCheat) {
                cheats.push_back(currentCheat);
            }
            currentCheat = CheatEntry();
            currentCheat.name = line.substr(1, line.length() - 2);
            currentCheat.enabled = false;
            inCheat = true;
        } else if (inCheat) {
            currentCheat.codes.push_back(line);
        }
    }

    if (inCheat) {
        cheats.push_back(currentCheat);
    }

    return cheats;
}

bool CheatManager::writeCheatFile(const std::string& filepath, const std::vector<CheatEntry>& cheats) {
    std::string parent = getParentPath(filepath);
    if (!parent.empty() && !pathExists(parent)) {
        createDirectories(parent);
    }

    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open()) return false;

    for (const auto& cheat : cheats) {
        if (!cheat.enabled) continue; // Only write enabled cheats

        file << "[" << cheat.name << "]\n";
        for (const auto& code : cheat.codes) {
            file << code << "\n";
        }
        file << "\n";
    }

    return true;
}

std::vector<CheatEntry> CheatManager::getAvailableCheats(uint64_t titleId, const std::string& buildId) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << std::uppercase << titleId;
    std::string tidStr = ss.str();
    
    std::vector<CheatEntry> allCheats;
    
    // Check possible locations
    std::vector<std::string> searchDirs = {
        getMasterCheatsPath() + "/" + tidStr + "/cheats",
        getMasterCheatsPath() + "/titles/" + tidStr + "/cheats",
        getMasterCheatsPath() + "/contents/" + tidStr + "/cheats"
    };

    for (const auto& dirPath : searchDirs) {
        DIR* dir = opendir(dirPath.c_str());
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                std::string fname = ent->d_name;
                if (fname.length() >= 4 && fname.substr(fname.length() - 4) == ".txt") {
                    auto cheats = parseCheatFile(dirPath + "/" + fname);
                    // Add filename as prefix to cheat name to distinguish build IDs
                    std::string buildPrefix = "[" + fname.substr(0, fname.length() - 4) + "] ";
                    for (auto& c : cheats) {
                        c.name = buildPrefix + c.name;
                    }
                    allCheats.insert(allCheats.end(), cheats.begin(), cheats.end());
                }
            }
            closedir(dir);
            if (!allCheats.empty()) break; // Found cheats in one of the directories
        }
    }

    return allCheats;
}

std::vector<CheatEntry> CheatManager::getAtmosphereCheats(uint64_t titleId, const std::string& buildId) {
    std::string amsPath = getAtmosphereCheatsPath(titleId, buildId);
    return parseCheatFile(amsPath);
}

bool CheatManager::saveAtmosphereCheats(uint64_t titleId, const std::string& buildId, const std::vector<CheatEntry>& cheats) {
    std::string amsPath = getAtmosphereCheatsPath(titleId, buildId);
    
    // Check if any are enabled
    bool anyEnabled = false;
    for (const auto& cheat : cheats) {
        if (cheat.enabled) {
            anyEnabled = true;
            break;
        }
    }

    if (!anyEnabled) {
        // If none are enabled, just delete the file so Atmosphere doesn't load an empty cheat file
        if (pathExists(amsPath)) {
            remove(amsPath.c_str());
        }
        return true;
    }

    return writeCheatFile(amsPath, cheats);
}

} // namespace pipensx::cheats
