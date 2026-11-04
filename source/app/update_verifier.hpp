#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "app/magnet_resolver.hpp"
#include "app/package_utils.hpp"

namespace goonies {

// The catalogue carries no per-update version data, so whether a torrent
// really holds an update newer than what is installed can only be known by
// reading its file list. This service resolves those torrents in the
// background (one at a time), extracts the update packages and their versions
// and caches the result per info-hash on disk. The "available updates" list
// only shows games whose torrent was verified to contain something newer.
class UpdateVerifier {
public:
    enum class State { Unknown, Yes, No };

    struct Job {
        std::string infoHash;  // lowercase hex
        std::string magnet;
        std::vector<uint8_t> infoDict;
        uint64_t appId = 0;
    };

    static UpdateVerifier& instance() {
        static UpdateVerifier s;
        return s;
    }

    State check(const std::string& infoHash, uint32_t installedVersion) {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureLoadedLocked();
        auto it = cache_.find(infoHash);
        if (it == cache_.end()) return State::Unknown;
        for (uint32_t v : it->second) {
            if (v > 0 ? v > installedVersion : installedVersion == 0)
                return State::Yes;
        }
        return State::No;
    }

    bool isKnown(const std::string& infoHash) {
        std::lock_guard<std::mutex> lock(mutex_);
        ensureLoadedLocked();
        return cache_.count(infoHash) != 0;
    }

    size_t pending() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size() + (running_ ? 1 : 0);
    }

    // Queue torrents to inspect; skips ones already cached or queued.
    // `notify` is invoked on the UI thread after each torrent is processed.
    void enqueue(std::vector<Job> jobs, std::string tmpDir,
                 std::function<void()> notify) {
        bool start = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ensureLoadedLocked();
            notify_ = std::move(notify);
            tmpDir_ = std::move(tmpDir);
            for (auto& j : jobs) {
                if (cache_.count(j.infoHash) || queued_.count(j.infoHash)) continue;
                queued_.insert(j.infoHash);
                queue_.push_back(std::move(j));
            }
            if (!running_ && !queue_.empty()) {
                running_ = true;
                cancelled_.store(false);
                start = true;
            }
        }
        if (start)
            brls::async([this] { worker(); });
    }

    // Stop background work (view closed / app exiting).
    void cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        queued_.clear();
        notify_ = nullptr;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<uint32_t>> cache_;
    std::vector<Job> queue_;
    std::set<std::string> queued_;
    std::function<void()> notify_;
    std::string tmpDir_;
    std::atomic<bool> cancelled_{false};
    bool running_ = false;
    bool loaded_ = false;

    static std::string cachePath() {
#ifdef __SWITCH__
        return "sdmc:/switch/thegoonies/catalog/update_scan.txt";
#else
        return "update_scan.txt";
#endif
    }

    void ensureLoadedLocked() {
        if (loaded_) return;
        loaded_ = true;
        FILE* f = std::fopen(cachePath().c_str(), "rb");
        if (!f) return;
        char line[512];
        while (std::fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            size_t bar = s.find('|');
            if (bar == std::string::npos || bar == 0) continue;
            std::string hash = s.substr(0, bar);
            std::vector<uint32_t> versions;
            std::string rest = s.substr(bar + 1);
            size_t pos = 0;
            while (pos < rest.size()) {
                size_t comma = rest.find(',', pos);
                std::string tok = rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty() && tok != "-")
                    versions.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 10)));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            cache_[hash] = std::move(versions);
        }
        std::fclose(f);
    }

    void appendCacheLocked(const std::string& hash, const std::vector<uint32_t>& versions) {
        FILE* f = std::fopen(cachePath().c_str(), "ab");
        if (!f) return;
        std::fprintf(f, "%s|", hash.c_str());
        if (versions.empty()) {
            std::fprintf(f, "-");
        } else {
            for (size_t i = 0; i < versions.size(); ++i)
                std::fprintf(f, "%s%u", i ? "," : "", versions[i]);
        }
        std::fprintf(f, "\n");
        std::fclose(f);
    }

    void worker() {
        for (;;) {
            Job job;
            std::string tmpDir;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (cancelled_.load() || queue_.empty()) {
                    running_ = false;
                    return;
                }
                job = std::move(queue_.front());
                queue_.erase(queue_.begin());
                tmpDir = tmpDir_;
            }

            std::string tmp = tmpDir + "/_verify_" + job.infoHash + ".torrent";
            std::string err;
            MagnetResolver resolver;
            std::atomic<bool>& cancel = cancelled_;
            bool ok = resolver.resolveToFile(
                job.magnet, tmp, cancel, [](const MagnetProgress&) {}, err,
                nullptr, job.infoDict.empty() ? nullptr : &job.infoDict);

            bool resolved = false;
            std::vector<uint32_t> versions;
            if (ok) {
                TorrentPreview preview;
                std::string perr;
                if (DownloadManager::previewTorrent(tmp, preview, perr)) {
                    resolved = true;
                    for (const auto& file : preview.files) {
                        if (!file.package) continue;
                        PackageInfo pkg = classifyPackage(file.path, job.appId);
                        if (pkg.kind == PackageKind::Update)
                            versions.push_back(pkg.version);
                    }
                    std::sort(versions.begin(), versions.end());
                    versions.erase(std::unique(versions.begin(), versions.end()), versions.end());
                }
            }
            ::unlink(tmp.c_str());

            std::function<void()> notify;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queued_.erase(job.infoHash);
                // Unresolvable torrents (no peers) are NOT cached so they are
                // retried next time; they simply never appear as updates.
                if (resolved) {
                    cache_[job.infoHash] = versions;
                    appendCacheLocked(job.infoHash, versions);
                }
                notify = notify_;
            }
            if (notify)
                brls::sync([notify] { notify(); });
        }
    }
};

} // namespace goonies
