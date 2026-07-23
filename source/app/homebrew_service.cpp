#include "homebrew_service.hpp"
#include <filesystem>
#include <utility>
#include "nro.hpp"

namespace pipensx {

void normalizePath(std::string& path) {
    // Reemplazar backslashes con forward slashes
    for (char &c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    
    // Quitar barra diagonal al final si existe
    if (!path.empty() && path.back() == '/') {
        path.pop_back();
    }

    // Corregir dobles barras diagonales (como "sdmc://", o "switch//")
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

HomebrewService::HomebrewService() {}

bool HomebrewService::refresh(const std::string& rootPath, std::string& error) {
    titles_.clear();
    
    std::string cleanRoot = rootPath;
    normalizePath(cleanRoot);
    
    std::error_code ec;
    if (!std::filesystem::exists(cleanRoot, ec)) {
        error = "Directorio " + cleanRoot + " no encontrado";
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(cleanRoot, ec)) {
        if (ec) continue;

        std::string path = entry.path().string();
        normalizePath(path);
        std::string nroPath = "";

        if (std::filesystem::is_directory(entry.status(ec))) {
            // Check if directory has an .nro with the same name
            std::string dirName = entry.path().filename().string();
            // Ignorar carpeta thegoonies o thegoonies_installer para no ensuciar
            if (dirName == "thegoonies" || dirName == "thegoonies_installer") continue;

            std::string possibleNro = path + "/" + dirName + ".nro";
            normalizePath(possibleNro);
            if (std::filesystem::exists(possibleNro, ec)) {
                nroPath = possibleNro;
            } else {
                // Es una subcarpeta que no es una app directa, la añadimos como carpeta
                HomebrewTitle folderTitle;
                folderTitle.path = path;
                folderTitle.name = dirName;
                folderTitle.author = "Carpeta";
                folderTitle.is_folder = true;
                titles_.push_back(folderTitle);
                continue;
            }
        } else if (entry.path().extension() == ".nro") {
            nroPath = path;
        }

        if (!nroPath.empty()) {
            HomebrewTitle title;
            title.path = nroPath;
            normalizePath(title.path);
            title.name = entry.path().stem().string(); // Default name
            title.author = "Unknown";
            
            // This reads NACP details + icon from the NRO
            GooniesInstaller::ReadNroAsset(nroPath, title.icon, title.name, title.author, title.nacp);
            
            titles_.push_back(title);
        }
    }

    // Ordenar para que las carpetas salgan al final
    // Usamos un ordenamiento de burbuja simple para evitar arrastrar cabeceras pesadas y posibles bucles de plantilla en el compilador
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
