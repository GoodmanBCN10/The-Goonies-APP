#include "RealDebridProvider.hpp"
#include "json.hpp"
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <fstream>
#include <mutex>
#include <iostream>
#include "json.hpp"
#include <chrono>
#include <nx_thread.hpp>
#include <atomic>
#include <filesystem>

using json = nlohmann::json;

#define RD_CLIENT_ID "X245A4XAIBGVM"
#define TOKEN_FILE "sdmc:/switch/thegoonies/rd_token.json"

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

RealDebridProvider::RealDebridProvider() {
    LoadTokens();
}

RealDebridProvider::~RealDebridProvider() {
    Shutdown();
}

bool RealDebridProvider::Initialize() {
    LoadTokens();
    return true;
}

void RealDebridProvider::Shutdown() {
    m_stopAuthThread = true;
    if (m_authThread.joinable()) {
        m_authThread.join();
    }
}

void RealDebridProvider::LoadTokens() {
    std::ifstream f(TOKEN_FILE);
    if (f.is_open()) {
        try {
            json j;
            f >> j;
            m_accessToken = j.value("access_token", "");
            m_refreshToken = j.value("refresh_token", "");
        } catch (...) {}
    }
}

static bool makeDirectoriesRD(const std::string& path) {
    if (path.empty()) return false;
    char buffer[1024];
    if (path.size() >= sizeof(buffer)) return false;
    std::snprintf(buffer, sizeof(buffer), "%s", path.c_str());
    for (char* p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    return mkdir(buffer, 0755) == 0 || errno == EEXIST;
}

void RealDebridProvider::SaveTokens() {
    makeDirectoriesRD("sdmc:/switch/thegoonies");
    
    std::ofstream f(TOKEN_FILE);
    if (f.is_open()) {
        json j;
        j["access_token"] = m_accessToken;
        j["refresh_token"] = m_refreshToken;
        f << j.dump(4);
    }
}

bool RealDebridProvider::IsAuthenticated() {
    return !m_accessToken.empty();
}

void RealDebridProvider::Logout() {
    m_accessToken = "";
    m_refreshToken = "";
    remove(TOKEN_FILE);
}

std::string RealDebridProvider::HttpGet(const std::string& url, const std::string& authHeader) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
        
        struct curl_slist* headers = NULL;
        if (!authHeader.empty()) {
            headers = curl_slist_append(headers, ("Authorization: " + authHeader).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        
        curl_easy_perform(curl);
        
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

std::string RealDebridProvider::HttpPost(const std::string& url, const std::string& postData, const std::string& authHeader) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
        
        struct curl_slist* headers = NULL;
        if (!authHeader.empty()) {
            headers = curl_slist_append(headers, ("Authorization: " + authHeader).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        
        curl_easy_perform(curl);
        
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

bool RealDebridProvider::StartOAuthDeviceFlow() {
    if (m_authThread.joinable()) {
        m_stopAuthThread = true;
        m_authThread.join();
    }
    
    m_stopAuthThread = false;
    m_authPending = false;
    m_authFailed = false;
    m_authSuccess = false;
    
    std::string res = HttpGet("https://api.real-debrid.com/oauth/v2/device/code?client_id=" RD_CLIENT_ID "&new_credentials=yes");
    if (res.empty()) return false;
    
    try {
        json j = json::parse(res);
        m_deviceCode = j["device_code"].get<std::string>();
        m_userCode = j["user_code"].get<std::string>();
        m_verificationUrl = j["verification_url"].get<std::string>();
        
        m_authPending = true;
        m_authThread = nx::thread(&RealDebridProvider::AuthPollingThread, this);
        return true;
    } catch (...) {
        return false;
    }
}

void RealDebridProvider::AuthPollingThread() {
    while (!m_stopAuthThread && m_authPending) {
        for (int i = 0; i < 50 && !m_stopAuthThread; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (m_stopAuthThread) break;
        
        std::string res = HttpGet("https://api.real-debrid.com/oauth/v2/device/credentials?client_id=" RD_CLIENT_ID "&code=" + m_deviceCode);
        if (res.empty()) continue;
        
        try {
            json j = json::parse(res);
            if (j.contains("error") && j["error"].is_string()) {
                std::string err = j["error"].get<std::string>();
                if (err != "authorization_pending") {
                    m_authFailed = true;
                    m_authPending = false;
                    break;
                }
            }
            
            if (j.contains("client_secret") && j["client_secret"].is_string() && j.contains("client_id") && j["client_id"].is_string()) {
                std::string clientSecret = j["client_secret"].get<std::string>();
                std::string clientId = j["client_id"].get<std::string>();
                
                // Now get the token
                std::string postData = "client_id=" + clientId + "&client_secret=" + clientSecret + "&code=" + m_deviceCode + "&grant_type=http%3A%2F%2Foauth.net%2Fgrant_type%2Fdevice%2F1.0";
                std::string tokenRes = HttpPost("https://api.real-debrid.com/oauth/v2/token", postData);
                
                json tj = json::parse(tokenRes);
                if (tj.contains("access_token")) {
                    m_accessToken = tj["access_token"].get<std::string>();
                    m_refreshToken = tj["refresh_token"].get<std::string>();
                    SaveTokens();
                    m_authSuccess = true;
                    m_authPending = false;
                    break;
                }
            }
        } catch (...) {}
    }
}

std::string RealDebridProvider::GetUserCode() const { return m_userCode; }
std::string RealDebridProvider::GetVerificationUrl() const { return m_verificationUrl; }
bool RealDebridProvider::IsAuthPending() const { return m_authPending; }
bool RealDebridProvider::DidAuthFail() const { return m_authFailed; }

// --- Download Management ---

struct RDActiveDownload {
    int id;
    std::string outFolder;
    std::atomic<float> progress{0.0f};
    std::atomic<bool> finished{false};
    std::atomic<bool> hasError{false};
    std::atomic<bool> stopFlag{false};
    std::atomic<bool> pausedFlag{false};
    std::atomic<uint64_t> downloadedBytes{0};
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<uint64_t> speedBps{0};
    std::string errorMessage;
    std::string stage;
    std::mutex stageMtx;
    nx::thread worker;
    std::string rdTorrentId;
    std::string downloadUrl;
};

static std::mutex g_rdMtx;
static std::vector<RDActiveDownload*> g_rdDownloads;
static int g_rdNextId = 1;

int RealDebridProvider::StartDownload(const std::string& torrentPathOrUrl, const std::string& outputFolder) {
    if (m_accessToken.empty()) return -1;
    
    auto* d = new RDActiveDownload();
    {
        std::lock_guard<std::mutex> lock(g_rdMtx);
        d->id = g_rdNextId++;
        d->outFolder = outputFolder;
        g_rdDownloads.push_back(d);
    }
    
    // Copy token to use in thread
    std::string token = m_accessToken;
    
    d->worker = nx::thread([this, d, torrentPathOrUrl, token]() {
        // 1. Add Magnet
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Añadiendo magnet a Real-Debrid...";
        }
        std::string addRes = this->HttpPost("https://api.real-debrid.com/rest/1.0/torrents/addMagnet", "magnet=" + torrentPathOrUrl, "Bearer " + token);
        std::string tId;
        try {
            json j = json::parse(addRes);
            if (!j.contains("id")) throw std::runtime_error("Invalid response");
            tId = j["id"].get<std::string>();
            d->rdTorrentId = tId;
        } catch(...) {
            d->hasError = true; d->errorMessage = "Error añadiendo magnet a RD"; d->finished = true; return;
        }
        
        // 2. Select files (all)
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Seleccionando archivos...";
        }
        this->HttpPost("https://api.real-debrid.com/rest/1.0/torrents/selectFiles/" + tId, "files=all", "Bearer " + token);
        
        // 3. Poll until downloaded on RD servers
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Esperando cache en Real-Debrid...";
        }
        std::string unrestrictLink;
        while (!d->stopFlag) {
            std::string infoRes = this->HttpGet("https://api.real-debrid.com/rest/1.0/torrents/info/" + tId, "Bearer " + token);
            try {
                json j = json::parse(infoRes);
                std::string status = j.value("status", "");
                if (status == "downloaded") {
                    auto links = j["links"];
                    if (links.is_array() && !links.empty()) {
                        unrestrictLink = links[0].get<std::string>();
                        break;
                    }
                } else if (status == "error" || status == "dead") {
                    d->hasError = true; d->errorMessage = "Torrent erróneo o muerto en RD"; d->finished = true; return;
                } else if (status == "downloading") {
                    d->progress = j.value("progress", 0.0f) / 100.0f;
                    std::lock_guard<std::mutex> lk(d->stageMtx);
                    d->stage = "RD Descargando: " + std::to_string((int)(d->progress * 100)) + "%";
                }
            } catch(...) {}
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        if (d->stopFlag) return;
        
        // 4. Unrestrict link
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Desbloqueando enlace premium...";
        }
        std::string unresRes = this->HttpPost("https://api.real-debrid.com/rest/1.0/unrestrict/link", "link=" + unrestrictLink, "Bearer " + token);
        try {
            json j = json::parse(unresRes);
            if (!j.contains("download")) throw std::runtime_error("No download link");
            d->downloadUrl = j["download"].get<std::string>();
        } catch(...) {
            d->hasError = true; d->errorMessage = "Error desbloqueando enlace"; d->finished = true; return;
        }
        
        // 5. Download HTTP via curl
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Descargando HTTP...";
            d->progress = 0.0f;
        }
        
        // We'll simulate HTTP download progress for now, or implement actual curl download to file.
        // For production, we'd use CURLOPT_WRITEFUNCTION writing to an ofstream, and CURLOPT_XFERINFOFUNCTION for progress.
        CURL* curl = curl_easy_init();
        if (curl) {
            FILE* fp = fopen((d->outFolder + "/game.nsp").c_str(), "wb");
            curl_easy_setopt(curl, CURLOPT_URL, d->downloadUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            
            // In a real implementation we pass a struct to track progress and stopFlag
            // We'll just perform it synchronously here for simplicity of the stub
            curl_easy_perform(curl);
            if(fp) fclose(fp);
            curl_easy_cleanup(curl);
        }
        
        d->progress = 1.0f;
        d->finished = true;
        {
            std::lock_guard<std::mutex> lk(d->stageMtx);
            d->stage = "Completado";
        }
    });
    
    return d->id;
}

void RealDebridProvider::StopDownload(int downloadId) {
    std::lock_guard<std::mutex> lock(g_rdMtx);
    for (auto it = g_rdDownloads.begin(); it != g_rdDownloads.end(); ++it) {
        if ((*it)->id == downloadId) {
            auto* d = *it;
            d->stopFlag = true;
            if (d->worker.joinable()) d->worker.join();
            delete d;
            g_rdDownloads.erase(it);
            break;
        }
    }
}

std::string RealDebridProvider::GetUnlockedLinkFromMagnet(const std::string& magnetLink, std::function<void(const std::string&, float)> progressCallback, const std::atomic<bool>& cancelFlag) {
    if (m_accessToken.empty()) {
        if (progressCallback) progressCallback("No autenticado", 0.0f);
        return "";
    }
    std::string token = m_accessToken;

    if (progressCallback) progressCallback("Añadiendo magnet a Real-Debrid...", 0.0f);
    std::string addRes = this->HttpPost("https://api.real-debrid.com/rest/1.0/torrents/addMagnet", "magnet=" + magnetLink, "Bearer " + token);
    
    std::string torrentId = "";
    try {
        json j = json::parse(addRes);
        if (!j.contains("id")) return "";
        torrentId = j["id"].get<std::string>();
    } catch(...) { return ""; }
    
    if (cancelFlag) return "";

    bool selected = false;
    std::string unrestrictLink = "";

    while (!cancelFlag) {
        std::string infoRes = this->HttpGet("https://api.real-debrid.com/rest/1.0/torrents/info/" + torrentId, "Bearer " + token);
        try {
            json j = json::parse(infoRes);
            std::string status = j.value("status", "");
            
            if (status == "waiting_files_selection" && !selected) {
                if (progressCallback) progressCallback("Seleccionando archivos...", 0.1f);
                
                std::string fileIds = "all";
                if (j.contains("files") && j["files"].is_array()) {
                    int bestId = -1;
                    size_t bestSize = 0;
                    for (const auto& file : j["files"]) {
                        if (!file.contains("path") || !file.contains("id") || !file.contains("bytes")) continue;
                        std::string path = file["path"].get<std::string>();
                        std::string lowerPath = path;
                        for (char& c : lowerPath) c = std::tolower(c);
                        
                        if (lowerPath.size() >= 4 && (lowerPath.substr(lowerPath.size() - 4) == ".nsp" || lowerPath.substr(lowerPath.size() - 4) == ".nsz")) {
                            size_t size = file["bytes"].get<size_t>();
                            if (size > bestSize) {
                                bestSize = size;
                                bestId = file["id"].get<int>();
                            }
                        }
                    }
                    if (bestId != -1) {
                        fileIds = std::to_string(bestId);
                    }
                }
                
                this->HttpPost("https://api.real-debrid.com/rest/1.0/torrents/selectFiles/" + torrentId, "files=" + fileIds, "Bearer " + token);
                selected = true;
            } else if (status == "downloaded") {
                auto links = j["links"];
                if (links.size() > 0) {
                    unrestrictLink = links[0].get<std::string>();
                }
                break;
            } else if (status == "downloading") {
                float progress = 0.0f;
                if (j.contains("progress")) {
                    auto p = j["progress"];
                    if (p.is_number()) progress = p.get<float>() / 100.0f;
                }
                if (progressCallback) progressCallback("RD Descargando: " + std::to_string((int)(progress * 100)) + "%", progress);
            } else if (status == "magnet_error" || status == "error" || status == "dead") {
                if (progressCallback) progressCallback("Error en Real-Debrid: " + status, 0.0f);
                return "";
            } else {
                if (progressCallback) progressCallback("Estado: " + status, 0.0f);
            }
        } catch(...) {}
        for (int i = 0; i < 10 && !cancelFlag; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    
    if (cancelFlag || unrestrictLink.empty()) return "";

    if (progressCallback) progressCallback("Desbloqueando enlace premium...", 1.0f);
    std::string unresRes = this->HttpPost("https://api.real-debrid.com/rest/1.0/unrestrict/link", "link=" + unrestrictLink, "Bearer " + token);
    try {
        json j = json::parse(unresRes);
        if (j.contains("error")) {
            std::string err = j["error"].get<std::string>();
            if (progressCallback) progressCallback("Error de RD: " + err, 1.0f);
            return "";
        }
        if (!j.contains("download")) {
            if (progressCallback) progressCallback("Error: sin enlace de descarga", 1.0f);
            return "";
        }
        return j["download"].get<std::string>();
    } catch(...) {
        if (progressCallback) progressCallback("Error parseando respuesta RD", 1.0f);
        return "";
    }
}
