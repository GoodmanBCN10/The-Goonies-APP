#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

namespace pipensx::cheats {

struct CheatEntry {
    std::string name;
    std::vector<std::string> codes;
    bool enabled;
};

class CheatManager {
public:
    static CheatManager& getInstance() {
        static CheatManager instance;
        return instance;
    }

    // Initialize paths
    void init();

    // Check if the master database exists
    bool hasMasterDatabase();

    // Download the latest cheats database (ZIP) and extract it to master folder
    void downloadDatabase(std::function<void(bool success, const std::string& error)> callback);

    // Get all available cheats from the master db for a specific title and build ID
    std::vector<CheatEntry> getAvailableCheats(uint64_t titleId, const std::string& buildId);

    // Read currently enabled cheats from Atmosphere directory
    std::vector<CheatEntry> getAtmosphereCheats(uint64_t titleId, const std::string& buildId);

    // Save the selected cheats directly to Atmosphere's folder (creates the .txt with ONLY enabled cheats)
    bool saveAtmosphereCheats(uint64_t titleId, const std::string& buildId, const std::vector<CheatEntry>& cheats);

    std::string getMasterCheatsPath() const;

private:
    CheatManager() = default;
    
    std::string getAtmosphereCheatsPath(uint64_t titleId, const std::string& buildId) const;
    
    std::vector<CheatEntry> parseCheatFile(const std::string& filepath);
    bool writeCheatFile(const std::string& filepath, const std::vector<CheatEntry>& cheats);
};

} // namespace pipensx::cheats
