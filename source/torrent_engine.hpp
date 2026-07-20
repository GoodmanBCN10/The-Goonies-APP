#pragma once
#include <string>

struct TorrentStatus {
    float progress = 0.0f;       // 0.0 to 1.0
    bool finished = false;
    bool paused = false;
    bool error = false;
    std::string errorMessage;
    std::string stage;           // "Resolviendo magnet...", "Descargando...", etc.
    uint64_t downloadedBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t speedBps = 0;       // bytes per second
    uint32_t numPeers = 0;
    uint32_t numActivePeers = 0;
};

class TorrentEngine {
public:
    static bool Initialize();
    static void Shutdown();
    
    // Starts downloading a torrent to the specified output folder
    // Returns a handle or ID, or -1 on error
    static int StartDownload(const std::string& torrentPathOrUrl, const std::string& outputFolder);
    
    // Check progress of a download (0.0 to 1.0)
    static float GetProgress(int downloadId);
    
    // Get detailed status
    static TorrentStatus GetStatus(int downloadId);
    
    // Check if finished
    static bool IsFinished(int downloadId);
    
    // Pause / Resume
    static void PauseDownload(int downloadId);
    static void ResumeDownload(int downloadId);
    
    // Stop and cleanup
    static void StopDownload(int downloadId);
};
