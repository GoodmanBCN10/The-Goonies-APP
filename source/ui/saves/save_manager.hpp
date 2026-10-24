#pragma once

#include <string>
#include <switch.h>

namespace goonies::saves {

class SaveManager {
public:
    static bool HasBackup(uint64_t titleId, const std::string& gameName);
    static bool BackupSave(uint64_t titleId, const std::string& gameName, std::string& outMessage);
    static bool RestoreSave(uint64_t titleId, const std::string& gameName, std::string& outMessage);
    static bool DeleteBackup(uint64_t titleId, const std::string& gameName, std::string& outMessage);

private:
    static std::string GetBackupPath(uint64_t titleId, const std::string& gameName);
    static std::string CleanGameName(const std::string& name);
};

} // namespace goonies::saves
