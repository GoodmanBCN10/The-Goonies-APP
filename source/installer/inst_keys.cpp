#include "installer/keys.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdio.h>

namespace Installer {

KeysManager& KeysManager::Get() {
    static KeysManager instance;
    return instance;
}

std::vector<u8> KeysManager::ParseHex(const std::string& hex) const {
    std::vector<u8> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (i + 1 >= hex.length()) break;
        std::string byteString = hex.substr(i, 2);
        bytes.push_back(static_cast<u8>(strtol(byteString.c_str(), nullptr, 16)));
    }
    return bytes;
}

bool KeysManager::LoadKeys(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        printf("KeysManager: Could not open keys file %s\n", filepath.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove spaces
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        auto delimiterPos = line.find('=');
        if (delimiterPos == std::string::npos) continue;

        std::string name = line.substr(0, delimiterPos);
        std::string value = line.substr(delimiterPos + 1);
        
        m_keys[name] = ParseHex(value);
    }
    
    printf("KeysManager: Loaded %zu keys from %s\n", m_keys.size(), filepath.c_str());
    return true;
}

bool KeysManager::HasKey(const std::string& name) const {
    return m_keys.find(name) != m_keys.end();
}

std::vector<u8> KeysManager::GetKey(const std::string& name) const {
    auto it = m_keys.find(name);
    if (it != m_keys.end()) {
        return it->second;
    }
    return std::vector<u8>();
}

} // namespace Installer
