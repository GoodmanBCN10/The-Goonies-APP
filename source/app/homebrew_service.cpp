#include "homebrew_service.hpp"
#include <filesystem>
#include "nro.hpp"

namespace pipensx {

HomebrewService::HomebrewService() {}

bool HomebrewService::refresh(std::string& error) {
    titles_.clear();
    std::string rootPath = "sdmc:/switch/";
    
    std::error_code ec;
    if (!std::filesystem::exists(rootPath, ec)) {
        error = "Directorio sdmc:/switch/ no encontrado";
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec)) {
        if (ec) continue;

        std::string path = entry.path().string();
        std::string nroPath = "";

        if (std::filesystem::is_directory(entry.status(ec))) {
            // Check if directory has an .nro with the same name
            std::string dirName = entry.path().filename().string();
            std::string possibleNro = path + "/" + dirName + ".nro";
            if (std::filesystem::exists(possibleNro, ec)) {
                nroPath = possibleNro;
            }
        } else if (entry.path().extension() == ".nro") {
            nroPath = path;
        }

        if (!nroPath.empty()) {
            HomebrewTitle title;
            title.path = nroPath;
            title.name = entry.path().stem().string(); // Default name
            title.author = "Unknown";
            
            // This reads NACP details + icon from the NRO
            GooniesInstaller::ReadNroAsset(nroPath, title.icon, title.name, title.author, title.nacp);
            
            titles_.push_back(title);
        }
    }

    return true;
}

std::vector<HomebrewTitle> HomebrewService::titles() const {
    return titles_;
}

} // namespace pipensx
