#include "torrent_engine.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <filesystem>

extern "C" {
#include "torrent_core/torrent.h"
#include "torrent_core/metainfo.h"
}

#include "torrent_core/magnet_resolver.hpp"

struct ActiveDownload {
    int id;
    std::string url;
    std::string outFolder;
    std::atomic<float> progress{0.0f};
    std::atomic<bool> finished{false};
    std::atomic<bool> hasError{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> pausedFlag{false};
    std::atomic<uint64_t> downloadedBytes{0};
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<uint64_t> speedBps{0};
    std::atomic<uint32_t> numPeers{0};
    std::atomic<uint32_t> numActivePeers{0};
    std::string errorMessage;
    std::string stage;
    std::mutex stageMtx;
    std::thread worker;
};

static std::mutex g_mtx;
static std::vector<ActiveDownload*> g_downloads;
static int g_nextId = 1;

bool TorrentEngine::Initialize() {
    return true;
}

void TorrentEngine::Shutdown() {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto* d : g_downloads) {
        d->stopFlag = true;
        if (d->worker.joinable()) d->worker.join();
        delete d;
    }
    g_downloads.clear();
}

int TorrentEngine::StartDownload(const std::string& torrentPathOrUrl, const std::string& outputFolder) {
    auto* d = new ActiveDownload();
    
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        d->id = g_nextId++;
        d->url = torrentPathOrUrl;
        d->outFolder = outputFolder;
        g_downloads.push_back(d);
    }
    
    d->worker = std::thread([d]() {
        // Ensure output directory exists
        std::error_code ec;
        std::filesystem::create_directories(d->outFolder, ec);
        
        std::string torrentFilePath = d->outFolder + "/temp.torrent";
        
        if (d->url.rfind("magnet:", 0) == 0) {
            {
                std::lock_guard<std::mutex> lk(d->stageMtx);
                d->stage = "Resolviendo magnet link...";
            }
            pipensx::MagnetResolver resolver;
            std::string error;
            std::atomic<bool> cancelled{false};
            
            bool ok = resolver.resolveToFile(d->url, torrentFilePath, cancelled,
                [d](const pipensx::MagnetProgress& mp) {
                    if (d->stopFlag) return;
                    std::lock_guard<std::mutex> lk(d->stageMtx);
                    switch (mp.stage) {
                        case pipensx::MagnetProgress::Stage::FindingPeers:
                            d->stage = "Buscando peers DHT...";
                            break;
                        case pipensx::MagnetProgress::Stage::Connecting:
                            d->stage = "Conectando a peer " + std::to_string(mp.peerIndex+1) + "/" + std::to_string(mp.peerCount) + "...";
                            break;
                        case pipensx::MagnetProgress::Stage::FetchingMetadata:
                            d->stage = "Descargando metadatos " + std::to_string(mp.completedPieces) + "/" + std::to_string(mp.totalPieces);
                            break;
                        case pipensx::MagnetProgress::Stage::Validating:
                            d->stage = "Validando metadatos...";
                            break;
                    }
                }, error);
                
            if (!ok || d->stopFlag) {
                if (!ok) {
                    d->hasError = true;
                    d->errorMessage = error.empty() ? "Error resolviendo magnet" : error;
                }
                d->finished = true;
                return;
            }
        } else {
            torrentFilePath = d->url; // local .torrent file
        }
        
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Cargando torrent...";
        }
        
        metainfo_t mi;
        if (!metainfo_load(torrentFilePath.c_str(), &mi)) {
            d->hasError = true;
            d->errorMessage = "Error al leer archivo .torrent";
            d->finished = true;
            return;
        }
        
        torrent_options_t options = {0};
        options.request_pipeline_limit = 256;
        torrent_t* t = torrent_create_ex(&mi, 6881, d->outFolder.c_str(), &options);
        if (!t) {
            metainfo_free(&mi);
            d->hasError = true;
            d->errorMessage = "Error al crear sesion torrent";
            d->finished = true;
            return;
        }
        
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Descargando...";
        }
        
        while (!d->stopFlag && !d->finished) {
            if (d->pausedFlag) {
                d->speedBps = 0; // Show 0 speed when paused
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            if (torrent_tick(t) == 0) {
                d->progress = 1.0f;
                d->finished = true;
                std::lock_guard<std::mutex> lk(d->stageMtx);
                d->stage = "Completado";
                break;
            }
            
            torrent_stat_t st;
            torrent_stat(t, &st);
            if (st.total_bytes > 0) {
                d->progress = (float)((double)st.completed_bytes / (double)st.total_bytes);
            }
            d->downloadedBytes = st.completed_bytes;
            d->totalBytes = st.total_bytes;
            d->speedBps = st.speed_bps;
            d->numPeers = st.num_peers;
            d->numActivePeers = st.num_active_peers;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        torrent_destroy(t);
        metainfo_free(&mi);
        
        // Mark as finished for safety
        d->finished = true;
    });
    
    return d->id;
}

float TorrentEngine::GetProgress(int downloadId) {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto* d : g_downloads) {
        if (d->id == downloadId) return d->progress;
    }
    return 0.0f;
}

TorrentStatus TorrentEngine::GetStatus(int downloadId) {
    TorrentStatus s;
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto* d : g_downloads) {
        if (d->id == downloadId) {
            s.progress = d->progress;
            s.finished = d->finished;
            s.paused = d->pausedFlag;
            s.error = d->hasError;
            s.errorMessage = d->errorMessage;
            s.downloadedBytes = d->downloadedBytes;
            s.totalBytes = d->totalBytes;
            s.speedBps = d->speedBps;
            s.numPeers = d->numPeers;
            s.numActivePeers = d->numActivePeers;
            {
                std::lock_guard<std::mutex> lk(d->stageMtx);
                s.stage = d->stage;
            }
            return s;
        }
    }
    s.finished = true;
    return s;
}

bool TorrentEngine::IsFinished(int downloadId) {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto* d : g_downloads) {
        if (d->id == downloadId) return d->finished;
    }
    return true;
}

void TorrentEngine::PauseDownload(int downloadId) {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto* d : g_downloads) {
        if (d->id == downloadId) {
            d->pausedFlag = true;
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Pausado";
            break;
        }
    }
}

void TorrentEngine::ResumeDownload(int downloadId) {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto* d : g_downloads) {
        if (d->id == downloadId) {
            d->pausedFlag = false;
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Descargando...";
            break;
        }
    }
}

void TorrentEngine::StopDownload(int downloadId) {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto it = g_downloads.begin(); it != g_downloads.end(); ++it) {
        if ((*it)->id == downloadId) {
            auto* d = *it;
            d->stopFlag = true;
            d->pausedFlag = false; // Unpause if paused so thread can exit
            
            // Join worker thread synchronously if joinable
            if (d->worker.joinable()) {
                d->worker.join();
            }
            
            delete d;
            g_downloads.erase(it);
            break;
        }
    }
}
