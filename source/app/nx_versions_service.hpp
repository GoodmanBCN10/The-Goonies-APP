#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <algorithm>
#include <ctime>
#include <sys/stat.h>
#include <curl/curl.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

extern "C" {
#include "core/util.h"
}

namespace goonies {

class NxVersionsService {
public:
    static NxVersionsService& instance() {
        static NxVersionsService s_instance;
        return s_instance;
    }

    NxVersionsService() {
#ifdef __SWITCH__
        cachePath_ = "sdmc:/switch/thegoonies/catalog/versions.txt";
#else
        cachePath_ = "catalog/versions.txt";
#endif
    }

    void init() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loaded_) return;

        // Try to load cached file or bundled romfs first for immediate availability
        loadCacheLocked();

        // If still not loaded from cache or romfs, try remote fetch
        if (!loaded_) {
            fetchRemoteLocked();
        }
    }

    uint32_t getLatestVersion(uint64_t appId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!loaded_) {
            loadCacheLocked();
        }
        // Patch / update title ID is always (appId & ~0x1FFF) | 0x800
        uint64_t updateId = (appId & ~0x1FFFULL) | 0x800ULL;
        auto it = versions_.find(updateId);
        return (it != versions_.end()) ? it->second : 0;
    }

    uint32_t getLatestVersion(const std::string& titleIdHex) {
        if (titleIdHex.empty()) return 0;
        char* end = nullptr;
        uint64_t appId = std::strtoull(titleIdHex.c_str(), &end, 16);
        if (end == titleIdHex.c_str()) return 0;
        return getLatestVersion(appId);
    }

    bool hasUpdate(const std::string& titleIdHex, uint32_t installedVersion) {
        uint32_t latest = getLatestVersion(titleIdHex);
        return latest > 0 && latest > installedVersion;
    }

private:
    std::mutex mutex_;
    std::string cachePath_;
    std::unordered_map<uint64_t, uint32_t> versions_;
    bool loaded_ = false;

    static time_t getFileMtime(const std::string& path) {
        struct stat st;
        return (stat(path.c_str(), &st) == 0) ? st.st_mtime : 0;
    }

    void loadCacheLocked() {
        FILE* f = std::fopen(cachePath_.c_str(), "rb");
        if (!f) {
            f = std::fopen("romfs:/versions.txt", "rb");
        }
        if (!f) return;
        std::fseek(f, 0, SEEK_END);
        long len = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (len <= 0 || len > 16 * 1024 * 1024) {
            std::fclose(f);
            return;
        }
        std::string text;
        text.resize(static_cast<size_t>(len));
        size_t readBytes = std::fread(&text[0], 1, text.size(), f);
        std::fclose(f);
        text.resize(readBytes);

        parseVersionsTxtLocked(text);
        if (!versions_.empty()) {
            loaded_ = true;
            log_msg("[nx_versions] Loaded %zu title versions\n", versions_.size());
        }
    }

    void parseVersionsTxtLocked(const std::string& text) {
        versions_.clear();
        versions_.reserve(text.size() / 40);

        const char* p = text.data();
        const char* end = p + text.size();
        while (p < end) {
            const char* eol = static_cast<const char*>(std::memchr(p, '\n', static_cast<size_t>(end - p)));
            if (!eol) eol = end;
            
            char* tidEnd = nullptr;
            uint64_t tid = std::strtoull(p, &tidEnd, 16);
            if (tidEnd != p && tidEnd < eol && *tidEnd == '|') {
                const char* last = eol;
                while (last > tidEnd && *(last - 1) != '|') --last;
                char* verEnd = nullptr;
                unsigned long ver = std::strtoul(last, &verEnd, 10);
                if (verEnd != last) {
                    versions_[tid] = static_cast<uint32_t>(ver);
                }
            }
            p = eol + 1;
        }
    }

    static size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* str = static_cast<std::string*>(userdata);
        size_t total = size * nmemb;
        if (str->size() + total > 16 * 1024 * 1024) return 0; // limit to 16MB
        str->append(ptr, total);
        return total;
    }

    bool fetchUrl(const std::string& url, std::string& out) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        out.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "TheGooniesAPP");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        return (res == CURLE_OK && httpCode == 200 && !out.empty());
    }

    void fetchRemoteLocked() {
        static const char* const kUrls[] = {
            "https://raw.githubusercontent.com/16BitWonder/nx-versions/master/versions.txt",
            "https://cdn.jsdelivr.net/gh/16BitWonder/nx-versions@master/versions.txt"
        };

        std::string body;
        bool ok = false;
        for (const char* url : kUrls) {
            if (fetchUrl(url, body)) {
                ok = true;
                break;
            }
        }

        if (ok && !body.empty()) {
            std::string tmpPath = cachePath_ + ".tmp";
            FILE* f = std::fopen(tmpPath.c_str(), "wb");
            if (f) {
                std::fwrite(body.data(), 1, body.size(), f);
                std::fclose(f);
                std::remove(cachePath_.c_str());
                std::rename(tmpPath.c_str(), cachePath_.c_str());
            }
            parseVersionsTxtLocked(body);
            loaded_ = true;
            log_msg("[nx_versions] Refreshed %zu title versions from remote\n", versions_.size());
        }
    }
};

} // namespace goonies
