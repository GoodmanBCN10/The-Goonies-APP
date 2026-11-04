#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

#include <sys/stat.h>

namespace goonies {

// Remembers, per title, the official update version for which the catalog
// torrent was already inspected and found to contain nothing newer than what
// is installed. The catalog carries no per-update version information, so
// this is the only reliable way to stop "ghost" updates from reappearing.
// An entry stops applying as soon as the official version moves past it.
class UpdateDismissStore {
public:
    static UpdateDismissStore& instance() {
        static UpdateDismissStore s;
        return s;
    }

    bool isDismissed(const std::string& titleIdHex, uint32_t officialVersion) {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureLoadedLocked();
        auto it = map_.find(upper(titleIdHex));
        return it != map_.end() && officialVersion <= it->second;
    }

    void dismiss(const std::string& titleIdHex, uint32_t officialVersion) {
        if (titleIdHex.empty() || officialVersion == 0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        ensureLoadedLocked();
        uint32_t& v = map_[upper(titleIdHex)];
        if (officialVersion > v) v = officialVersion;
        saveLocked();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, uint32_t> map_;
    bool loaded_ = false;

    static std::string path() {
#ifdef __SWITCH__
        return "sdmc:/switch/thegoonies/catalog/update_checked.txt";
#else
        return "update_checked.txt";
#endif
    }

    static std::string upper(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    void ensureLoadedLocked() {
        if (loaded_) return;
        loaded_ = true;
        FILE* f = std::fopen(path().c_str(), "rb");
        if (!f) return;
        char line[128];
        while (std::fgets(line, sizeof(line), f)) {
            std::string s(line);
            size_t bar = s.find('|');
            if (bar == std::string::npos) continue;
            std::string id = s.substr(0, bar);
            uint32_t v = static_cast<uint32_t>(std::strtoul(s.c_str() + bar + 1, nullptr, 10));
            if (!id.empty() && v > 0) map_[upper(id)] = v;
        }
        std::fclose(f);
    }

    void saveLocked() {
        FILE* f = std::fopen(path().c_str(), "wb");
        if (!f) return;
        for (const auto& kv : map_)
            std::fprintf(f, "%s|%u\n", kv.first.c_str(), kv.second);
        std::fclose(f);
    }
};

} // namespace goonies
