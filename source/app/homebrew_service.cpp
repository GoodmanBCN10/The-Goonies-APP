#include "homebrew_service.hpp"
#include "nro.hpp"
#include <utility>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>

namespace pipensx {

void normalizePath(std::string& path) {
    for (char &c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    size_t pos = 0;
    while ((pos = path.find("//", pos)) != std::string::npos) {
        if (pos >= 5 && path.substr(pos - 5, 7) == "sdmc://") {
            path.erase(pos, 1);
        } else if (pos > 0 && path[pos-1] == ':') {
            pos += 2;
        } else {
            path.erase(pos, 1);
        }
    }
}

struct DirectoryContentInfo {
    bool has_subdirs = false;
    std::vector<std::string> nro_files;
};

DirectoryContentInfo scanDirectory(const std::string& dirPath) {
    DirectoryContentInfo info;
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return info;
    
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        
        std::string fullPath = dirPath + "/" + name;
        normalizePath(fullPath);
        
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                info.has_subdirs = true;
            } else if (name.size() > 4 && strcasecmp(name.substr(name.size() - 4).c_str(), ".nro") == 0) {
                info.nro_files.push_back(fullPath);
            }
        }
    }
    closedir(dir);
    return info;
}

HomebrewService::HomebrewService() {}

bool HomebrewService::refresh(const std::string& rootPath, std::string& error) {
    titles_.clear();
    
    std::string cleanRoot = rootPath;
    normalizePath(cleanRoot);
    
    DIR* dir = opendir(cleanRoot.c_str());
    if (!dir) {
        error = "Directorio " + cleanRoot + " no encontrado";
        return false;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;

        std::string path = cleanRoot + "/" + name;
        normalizePath(path);
        
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            std::string nroPath = "";

            if (S_ISDIR(st.st_mode)) {
                DirectoryContentInfo info = scanDirectory(path);
                if (!info.nro_files.empty()) {
                    for (const auto& npath : info.nro_files) {
                        HomebrewTitle title;
                        title.path = npath;
                        normalizePath(title.path);
                        
                        // Obtener nombre del archivo sin extensión
                        size_t lastSlash = npath.find_last_of('/');
                        std::string filename = (lastSlash == std::string::npos) ? npath : npath.substr(lastSlash + 1);
                        size_t lastDot = filename.find_last_of('.');
                        title.name = (lastDot == std::string::npos) ? filename : filename.substr(0, lastDot);
                        title.author = "Unknown";
                        
                        GooniesInstaller::ReadNroAsset(npath, title.icon, title.name, title.author, title.nacp);
                        titles_.push_back(title);
                    }
                } else if (info.has_subdirs) {
                    HomebrewTitle folderTitle;
                    folderTitle.path = path;
                    folderTitle.name = name;
                    folderTitle.author = "Carpeta";
                    folderTitle.is_folder = true;
                    titles_.push_back(folderTitle);
                }
                continue;
            } else if (name.size() > 4 && strcasecmp(name.substr(name.size() - 4).c_str(), ".nro") == 0) {
                nroPath = path;
            }

            if (!nroPath.empty()) {
                HomebrewTitle title;
                title.path = nroPath;
                normalizePath(title.path);
                title.name = name.substr(0, name.size() - 4); // default stem
                title.author = "Unknown";
                
                GooniesInstaller::ReadNroAsset(nroPath, title.icon, title.name, title.author, title.nacp);
                titles_.push_back(title);
            }
        }
    }
    closedir(dir);

    // Ordenar con burbuja
    if (!titles_.empty()) {
        for (size_t i = 0; i < titles_.size() - 1; i++) {
            for (size_t j = 0; j < titles_.size() - 1 - i; j++) {
                bool swap_needed = false;
                if (titles_[j].is_folder != titles_[j + 1].is_folder) {
                    swap_needed = titles_[j].is_folder && !titles_[j + 1].is_folder;
                } else {
                    swap_needed = titles_[j].name > titles_[j + 1].name;
                }
                if (swap_needed) {
                    std::swap(titles_[j], titles_[j + 1]);
                }
            }
        }
    }

    return true;
}

std::vector<HomebrewTitle> HomebrewService::titles() const {
    return titles_;
}

} // namespace pipensx
