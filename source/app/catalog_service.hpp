#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace pipensx {

struct CatalogEntry {
    std::string name; // Unique ID from Switchbru
    std::string title;
    std::string directDownloadUrl;
    std::string posterUrl;
    
    std::string description;
    std::string developer;
    std::string version;
    std::string category;
    
    uint64_t size = 0;
};

class CatalogService {
public:
    explicit CatalogService(std::string rootPath,
                            std::string bundledPath = {});

    bool load(std::string& error);

    bool fetchLatest(std::vector<CatalogEntry>& parsed, std::string& error);
    void adopt(std::vector<CatalogEntry> parsed);

    const std::vector<CatalogEntry>& entries() const { return entries_; }

    const std::string& sourceLabel() const { return sourceLabel_; }

    bool loadFile(const std::string& path, const std::string& label,
                  std::string& error);
                  
    static bool downloadAndExtractZip(const std::string& url, const std::string& entryName,
                                      const std::string& targetDir,
                                      std::atomic<bool>* cancelled,
                                      std::function<void(uint64_t, uint64_t)> progressCb,
                                      std::function<void(uint64_t, uint64_t)> extractProgressCb,
                                      std::string& error);

private:
    bool parseJson(const std::string& json,
                   std::vector<CatalogEntry>& entries,
                   std::string& error);

    std::string rootPath_;
    std::string cachePath_;
    std::string bundledPath_;

    std::vector<CatalogEntry> entries_;
    std::string sourceLabel_;
};

} // namespace pipensx
