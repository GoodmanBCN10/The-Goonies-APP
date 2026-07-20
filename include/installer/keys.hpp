#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <switch.h>

namespace Installer {

class KeysManager {
public:
    static KeysManager& Get();

    bool LoadKeys(const std::string& filepath = "sdmc:/switch/prod.keys");
    bool HasKey(const std::string& name) const;
    std::vector<u8> GetKey(const std::string& name) const;

private:
    KeysManager() = default;
    ~KeysManager() = default;

    std::unordered_map<std::string, std::vector<u8>> m_keys;
    
    std::vector<u8> ParseHex(const std::string& hex) const;
};

} // namespace Installer
