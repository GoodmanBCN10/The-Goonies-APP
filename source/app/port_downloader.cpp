#include <switch.h>
#include <curl/curl.h>
#include <minizip/unzip.h>
#include <string>
#include <atomic>
#include <functional>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <dirent.h>

#include "download_manager.hpp"
#include "../owo.hpp"
#include "../fs.hpp"
#include "../yati/yati.hpp"
#include "installer/installer_core.hpp"

namespace pipensx {

struct PortDownloadContext {
    std::function<void(const std::string&, uint64_t, uint64_t)> progressCb;
    std::atomic<bool>* cancelFlag;
    double totalSize = 0;
};

static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

static int progress_callback(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    auto* ctx = static_cast<PortDownloadContext*>(clientp);
    if (ctx->cancelFlag && ctx->cancelFlag->load()) {
        return 1; // Abort download
    }
    if (dltotal > 0) {
        ctx->totalSize = dltotal;
        ctx->progressCb("Descargando...", static_cast<uint64_t>(dlnow), static_cast<uint64_t>(dltotal));
    }
    return 0;
}

bool processPortTask(const std::string& id, const std::string& url, const std::string& dataPath, std::atomic<bool>& cancelFlag, std::function<void(const std::string&, uint64_t, uint64_t)> progressCb) {
    std::vector<std::string> urls;
    size_t s = 0;
    size_t e = url.find(',');
    while (e != std::string::npos) {
        urls.push_back(url.substr(s, e - s));
        s = e + 1;
        e = url.find(',', s);
    }
    urls.push_back(url.substr(s));

    std::vector<std::string> downloadedZips;

        for (size_t u = 0; u < urls.size(); ++u) {
        std::string currentUrl = urls[u];
        std::string zipPath = dataPath + "/" + id;
        if (urls.size() > 1) {
            zipPath += "_part" + std::to_string(u + 1);
        }
        zipPath += ".zip";
        
        std::string prefix = "";
        if (urls.size() > 1) {
            prefix = "[P" + std::to_string(u + 1) + "/" + std::to_string(urls.size()) + "] ";
        }

        // 1. DOWNLOAD ZIP
        CURL* curl = curl_easy_init();
        if (!curl) { progressCb("Error: curl init",0,0); return false; }
        
        FILE* fp = fopen(zipPath.c_str(), "wb");
        if (!fp) { curl_easy_cleanup(curl); progressCb("Error: fopen zip fail",0,0); return false; }
        
        std::vector<char> ioBuf(128 * 1024);
        setvbuf(fp, ioBuf.data(), _IOFBF, ioBuf.size());

        PortDownloadContext ctx;
        ctx.progressCb = [progressCb, prefix](const std::string& stage, uint64_t dlnow, uint64_t dltotal) {
            progressCb(prefix + stage, dlnow, dltotal);
        };
        ctx.cancelFlag = &cancelFlag;
        
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 128 * 1024L);
        curl_easy_setopt(curl, CURLOPT_URL, currentUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        
        CURLcode res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK || cancelFlag.load()) { remove(zipPath.c_str()); progressCb(std::string("Error: descarga fallida (") + curl_easy_strerror(res) + ")",0,0); return false; }

        // 2. EXTRACT THIS ZIP IMMEDIATELY
        progressCb(prefix + "Preparando extraccion...", 0, 100);
        
        unzFile uf = unzOpen64(zipPath.c_str());
        if (uf == NULL) { remove(zipPath.c_str()); progressCb("Error: zip invalido",0,0); return false; }
        
        unz_global_info64 gi;
        if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) { unzClose(uf); progressCb("Error: zip sin info",0,0); return false; }
        
        for (u64 i = 0; i < gi.number_entry; ++i) {
            if (cancelFlag.load()) { unzClose(uf); progressCb("Error: cancelado",0,0); return false; }
            
            progressCb(prefix + "Extrayendo...", i, gi.number_entry);
            
            char filename_inzip[256];
            unz_file_info64 file_info;
            if (unzGetCurrentFileInfo64(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) {
                break;
            }
            
            std::string extractPath = std::string("sdmc:/") + filename_inzip;
            
            if (extractPath.back() == '/') {
                std::filesystem::create_directories(extractPath);
            } else {
                std::filesystem::path p(extractPath);
                std::filesystem::create_directories(p.parent_path());
                
                if (unzOpenCurrentFile(uf) == UNZ_OK) {
                    FILE* out = fopen(extractPath.c_str(), "wb");
                    if (out) {
                        std::vector<char> outBuf(128 * 1024);
                        setvbuf(out, outBuf.data(), _IOFBF, outBuf.size());
                        
                        std::vector<char> buffer(128 * 1024);
                        int readBytes;
                        while ((readBytes = unzReadCurrentFile(uf, buffer.data(), buffer.size())) > 0) {
                            fwrite(buffer.data(), 1, readBytes, out);
                            if (cancelFlag.load()) break;
                        }
                        fclose(out);
                    }
                    unzCloseCurrentFile(uf);
                }
            }
            
            if (i < gi.number_entry - 1) {
                if (unzGoToNextFile(uf) != UNZ_OK) break;
            }
        }
        
        unzClose(uf);
        remove(zipPath.c_str()); // Clean up zip immediately
    }
    
    // 3. SCAN FOR NSP or NRO in switch/id/
    // NOTE: do NOT call progressCb("Finalizando...") here - download_manager treats that
    // as "task done" and kills the thread before install_forwarder can run.
    // We call it AFTER the forwarder/NSP install is complete.
    
    std::string searchDir = "sdmc:/switch/" + id;
    std::string nroPath = "";
    std::string nspPath = "";
    
    if (std::filesystem::exists(searchDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(searchDir)) {
            std::string ext = entry.path().extension().string();
            if (ext == ".nsp") {
                nspPath = entry.path().string();
            } else if (ext == ".nro" && nroPath.empty()) {
                nroPath = entry.path().string();
            }
        }
    }
    
    if (!nspPath.empty()) {
        Installer::Core installer;
        if (installer.StartInstallation(nspPath)) {
            FILE* fp = fopen(nspPath.c_str(), "rb");
            if (fp) {
                std::vector<char> ioBuf(1024 * 1024);
                setvbuf(fp, ioBuf.data(), _IOFBF, ioBuf.size());
                
                std::vector<char> buffer(1024 * 1024);
                while (true) {
                    size_t bytes = fread(buffer.data(), 1, buffer.size(), fp);
                    if (bytes == 0) break;
                    if (!installer.WriteData(buffer.data(), bytes)) break;
                    if (cancelFlag.load()) {
                        installer.AbortInstallation();
                        break;
                    }
                }
                fclose(fp);
                if (!cancelFlag.load() && !installer.HasError()) {
                    installer.FinishInstallation();
                } else {
                    installer.AbortInstallation();
                    remove(nspPath.c_str());
                    progressCb("Error: NSP abort 1",0,0); return false; } } else { installer.AbortInstallation(); remove(nspPath.c_str()); progressCb("Error: NSP abort 2",0,0); return false; } } else { remove(nspPath.c_str()); progressCb("Error: NSP abort 3",0,0); return false; }
        
        remove(nspPath.c_str());
    } else if (!nroPath.empty()) {
        GooniesInstaller::OwoConfig config;
        config.nro_path = nroPath;
        if (nroPath.find("sdmc:") == 0) config.nro_path = nroPath.substr(5);
        
        std::string iconPath = searchDir + "/icon.jpg";
        if (std::filesystem::exists(iconPath)) {
            FILE* ficon = fopen(iconPath.c_str(), "rb");
            if (ficon) {
                fseek(ficon, 0, SEEK_END);
                size_t sz = ftell(ficon);
                rewind(ficon);
                config.icon.resize(sz);
                fread(config.icon.data(), 1, sz, ficon);
                fclose(ficon);
            }
        }
        
                config.name = id;
        config.author = "Port";
        Result fwd_res = GooniesInstaller::install_forwarder(config, NcmStorageId_SdCard);
        if (R_FAILED(fwd_res)) {
            char ebuf[64];
            snprintf(ebuf, sizeof(ebuf), "Error: Forwarder fallo 0x%08X", fwd_res);
            progressCb(ebuf, 0, 0);
            return false;
        }
    }
    
    progressCb("Finalizando instalacion...", 100, 100);
    return true;
}

} // namespace pipensx




