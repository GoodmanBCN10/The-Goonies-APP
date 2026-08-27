#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <nx_thread.hpp>
#include <queue>
#include <curl/curl.h>
#include "app/catalog_service.hpp" // Has downloadAndExtractZip
#include "app/app_settings.hpp"
#include <borealis.hpp>
#include "forwarder.hpp"
#include <dirent.h>
#include <sstream>

namespace goonies {

enum class Status {
    Queued,
    Downloading,
    Extracting,
    Completed,
    Error
};

struct DLTask {
    std::string id; // also the output folder name
    std::string title;
    std::string url;
    std::vector<std::string> urls;
    int current_part = 1;
    int total_parts = 1;
    std::string author;
    std::string posterUrl;
    Status status = Status::Queued;
    std::string error;
    uint64_t progress = 0;
    uint64_t total = 0;
    std::atomic<bool> cancelled {false};
};

class SimpleDownloadManager {
public:
    pipensx::AppSettings* settings() { return settings_; }
    
    SimpleDownloadManager(pipensx::AppSettings* settings = nullptr) : settings_(settings) {
        worker_ = nx::thread(&SimpleDownloadManager::processQueue, this);
    }
    
    ~SimpleDownloadManager() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

    void add(const std::string& id, const std::string& title, const std::string& url, const std::string& author = "Goonies Ports", const std::string& posterUrl = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& t : tasks_) if (t->id == id && (t->status == Status::Queued || t->status == Status::Downloading)) return; // ya existe
        
        auto task = std::make_shared<DLTask>();
        task->id = id;
        task->title = title;
        task->url = url;
        task->author = author;
        task->posterUrl = posterUrl;
        
        // Parse comma-separated URLs
        std::stringstream ss(url);
        std::string item;
        while (std::getline(ss, item, ',')) {
            task->urls.push_back(item);
        }
        if (task->urls.empty()) task->urls.push_back(url);
        task->total_parts = task->urls.size();
        
        tasks_.push_back(task);
    }

    std::vector<std::shared_ptr<DLTask>> snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_;
    }

    void remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ) {
            if ((*it)->id == id) {
                (*it)->cancelled = true;
                it = tasks_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void clearCompleted() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ) {
            if ((*it)->status == Status::Completed || (*it)->status == Status::Error) {
                it = tasks_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    void processQueue() {
        bool isAwake = false;
        while (running_) {
            std::shared_ptr<DLTask> current = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& t : tasks_) {
                    if (t->status == Status::Queued) {
                        current = t;
                        break;
                    }
                }
            }
            
            if (current) {
                if (!isAwake) { appletSetMediaPlaybackState(true); isAwake = true; }
                current->status = Status::Downloading;
                std::string targetDir = "sdmc:";
                std::string err;
                
                bool all_ok = true;
                for (size_t i = 0; i < current->urls.size(); ++i) {
                    if (current->cancelled) break;
                    current->current_part = i + 1;
                    current->status = Status::Downloading;
                    current->progress = 0;
                    current->total = 0;
                    
                    bool ok = pipensx::CatalogService::downloadAndExtractZip(
                        current->urls[i], 
                        current->id, 
                        targetDir, 
                        &current->cancelled,
                        [current](uint64_t dltotal, uint64_t dlnow) {
                            current->progress = dlnow;
                            current->total = dltotal;
                        }, 
                        [current](uint64_t extnow, uint64_t exttotal) { current->status = Status::Extracting; current->progress = extnow; current->total = exttotal; }, 
                        err
                    );
                    
                    if (!ok || current->cancelled) {
                        all_ok = false;
                        break;
                    }
                }
                
                if (current->cancelled) {
                    // Se borro mientras bajaba
                } else if (all_ok) {
                    // Instalar forwarder
                    std::vector<u8> icon_data;
                    
                    std::string nro_path;
                    std::string base_dir = targetDir + "/switch/" + current->id;
                    DIR* dir = opendir(base_dir.c_str());
                    if (dir) {
                        struct dirent* ent;
                        while ((ent = readdir(dir)) != NULL) {
                            std::string name = ent->d_name;
                            if (name.length() > 4 && name.substr(name.length() - 4) == ".nro") {
                                nro_path = base_dir + "/" + name;
                                break;
                            }
                        }
                        closedir(dir);
                    }
                    
                    if (!nro_path.empty()) {
                        GooniesInstaller::OwoConfig config;
                        config.nro_path = nro_path;
                        config.name = current->title;
                        config.author = current->author.empty() ? "Goonies Ports" : current->author;
                        config.icon = icon_data;
                        
                        if (config.icon.empty()) {
                            FILE* f = fopen((base_dir + "/icon.jpg").c_str(), "rb");
                            if (f) {
                                fseek(f, 0, SEEK_END);
                                size_t sz = ftell(f);
                                fseek(f, 0, SEEK_SET);
                                config.icon.resize(sz);
                                fread(config.icon.data(), 1, sz, f);
                                fclose(f);
                            }
                        }
                        
                        if (!config.icon.empty()) {
                            GooniesInstaller::install_forwarder(config, NcmStorageId_SdCard);
                        }
                    }
                    
                    current->status = Status::Completed;
                } else {
                    current->status = Status::Error;
                    current->error = err;
                }
            } else {
                if (isAwake) { appletSetMediaPlaybackState(false); isAwake = false; }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }

    std::vector<std::shared_ptr<DLTask>> tasks_;
    std::mutex mutex_;
    nx::thread worker_;
    std::atomic<bool> running_{true};
    pipensx::AppSettings* settings_;
};

} // namespace goonies
