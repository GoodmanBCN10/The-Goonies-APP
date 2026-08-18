#include "ui/installed/cheat_downloader_view.hpp"
#include <borealis.hpp>
#include <curl/curl.h>
#include <minizip/unzip.h>
#include <sys/stat.h>
#include <fstream>
#include "cheats/cheat_manager.hpp"
#include <dirent.h>
#include <cstdio>
#include <memory>

#include "ui/common/ui_helpers.hpp"

namespace pipensx::ui {

struct CurlProgressData {
    std::shared_ptr<bool> alive;
    CheatDownloaderView* view;
};

static size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, (FILE*)stream);
}

static int curlProgressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    auto* data = static_cast<CurlProgressData*>(clientp);
    if (!data || !*data->alive) {
        return 1; // Abort transfer if view is dead
    }

    if (dltotal > 0) {
        float pct = (float)dlnow / (float)dltotal * 100.0f;
        auto alive = data->alive;
        auto* view = data->view;
        // Don't flood main thread with syncs, only roughly every few percent
        static int last_pct = 0;
        int current_pct = (int)pct;
        if (current_pct != last_pct && current_pct % 5 == 0) {
            last_pct = current_pct;
            brls::sync([alive, view, current_pct]() {
                if (*alive) {
                    view->statusLabel->setText("Descargando base de datos... " + std::to_string(current_pct) + "%");
                }
            });
        }
    }
    return 0;
}

// POSIX directory creation helper
static void createDirs(const std::string& path) {
    size_t pos = 0;
    std::string currentPath;
    while ((pos = path.find_first_of("/\\", pos + 1)) != std::string::npos) {
        currentPath = path.substr(0, pos);
        if (!currentPath.empty() && currentPath != "sdmc:") {
            mkdir(currentPath.c_str(), 0777);
        }
    }
    mkdir(path.c_str(), 0777);
}

CheatDownloaderView::CheatDownloaderView(const std::string& titleIdStr, std::function<void()> onComplete) 
    : brls::Box(brls::Axis::COLUMN), m_titleIdStr(titleIdStr), onCompleteCb(onComplete) {
    
    isAlive = std::make_shared<bool>(true);
    
    this->setFocusable(false);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setGrow(1.0f);

    auto* titleLabel = new brls::Label();
    titleLabel->setText(t("Base de Datos de Trucos", "Cheat Database", "Base de Dados de Cheats"));
    titleLabel->setFontSize(30);
    titleLabel->setMarginBottom(40);
    this->addView(titleLabel);

    statusLabel = new brls::Label();
    statusLabel->setText(t("Iniciando descarga...", "Starting download...", "Iniciando download..."));
    statusLabel->setFontSize(24);
    statusLabel->setMarginBottom(30);
    this->addView(statusLabel);

    auto alive = this->isAlive;
    brls::async([this, alive]() { 
        if (*alive) doDownload(); 
    });
}

CheatDownloaderView::~CheatDownloaderView() {
    *isAlive = false;
}

brls::View* CheatDownloaderView::create(const std::string& titleIdStr, std::function<void()> onComplete) { 
    return new CheatDownloaderView(titleIdStr, onComplete); 
}

void CheatDownloaderView::doDownload() {
    isDownloading = true;
    std::string url = "https://github.com/sthetix/nx-cheats-db/releases/latest/download/titles.zip";

    auto alive = this->isAlive;
    brls::sync([this, alive]() {
        if (*alive) statusLabel->setText(t("Descargando base de datos...", "Downloading database...", "Baixando banco de dados..."));
    });

    std::string temp_zip = "sdmc:/switch/thegoonies/cheats_temp.zip";
    
    FILE* fp = fopen(temp_zip.c_str(), "wb");
    if (!fp) {
        brls::sync([this, alive]() {
            if (*alive) {
                statusLabel->setText("Error al crear archivo zip");
                isDownloading = false;
            }
        });
        return;
    }

    CurlProgressData progData{isAlive, this};

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progData);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "TheGooniesAPP/1.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && *alive) {
            extractZip(temp_zip);
        } else if (*alive) {
            brls::sync([this, alive]() {
                if (*alive) statusLabel->setText("Error de descarga");
            });
        }
    } else {
        fclose(fp);
    }
    
    // remove zip
    unlink(temp_zip.c_str());
    
    auto cb = this->onCompleteCb;
    brls::sync([alive, cb]() {
        if (*alive) {
            brls::Application::popActivity();
            if (cb) cb();
        }
    });
}

void CheatDownloaderView::extractZip(const std::string& temp_zip) {
    isExtracting = true;
    auto alive = this->isAlive;

    brls::sync([this, alive]() {
        if (*alive) statusLabel->setText(t("Extrayendo trucos...", "Extracting cheats...", "Extraindo cheats..."));
    });

    unzFile zipfile = unzOpen(temp_zip.c_str());
    if (zipfile == NULL) {
        brls::sync([this, alive]() {
            if (*alive) statusLabel->setText("Error: zip invalido");
        });
        isExtracting = false;
        return;
    }

    unz_global_info global_info;
    if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK) {
        unzClose(zipfile);
        isExtracting = false;
        return;
    }

    std::string cheats_base = cheats::CheatManager::getInstance().getMasterCheatsPath() + "/" + m_titleIdStr + "/cheats/";
    createDirs(cheats::CheatManager::getInstance().getMasterCheatsPath());
    createDirs(cheats::CheatManager::getInstance().getMasterCheatsPath() + "/" + m_titleIdStr);
    createDirs(cheats_base);

    for (uLong i = 0; i < global_info.number_entry; ++i) {
        if (!*alive) break; // abort early

        char filename_inzip[256];
        unz_file_info file_info;
        if (unzGetCurrentFileInfo(zipfile, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) {
            unzClose(zipfile);
            isExtracting = false;
            return;
        }

        std::string filename_str(filename_inzip);
        
        // ONLY extract cheats for our titleId
        if (filename_str.find(m_titleIdStr) != std::string::npos) {
            size_t last_slash = filename_str.find_last_of('/');
            std::string basename = (last_slash == std::string::npos) ? filename_str : filename_str.substr(last_slash + 1);
            
            // Only extract files, not directories, and only if they end in .txt
            if (!basename.empty() && basename.find(".txt") != std::string::npos) {
                if (unzOpenCurrentFile(zipfile) == UNZ_OK) {
                    std::string out_path = cheats_base + basename;
                    std::ofstream out(out_path, std::ios::binary);
                    if (out.is_open()) {
                        char buf[8192];
                        int len = 0;
                        while ((len = unzReadCurrentFile(zipfile, buf, sizeof(buf))) > 0) {
                            if (!*alive) break;
                            out.write(buf, len);
                        }
                    }
                    unzCloseCurrentFile(zipfile);
                }
            }
        }
        
        if (i % 20 == 0 || (i + 1) == global_info.number_entry) {
            float pct = (float)(i + 1) / (float)global_info.number_entry * 100.0f;
            int current_pct = (int)pct;
            brls::sync([this, alive, current_pct]() {
                if (*alive) {
                    statusLabel->setText("Extrayendo trucos... " + std::to_string(current_pct) + "%");
                }
            });
        }
        
        if ((i + 1) < global_info.number_entry) {
            unzGoToNextFile(zipfile);
        }
    }
    unzClose(zipfile);
}

} // namespace pipensx::ui
