#include "ui/updater/updater_view.hpp"
#include <borealis/views/bottom_bar.hpp>
#include <curl/curl.h>
#include <minizip/unzip.h>
#include <filesystem>
#include <string>
#include <vector>
#include <dirent.h>
#include <thread>
#include <fstream>
#include <iostream>
#include <switch.h>

namespace fs = std::filesystem;

namespace goonies::ui {

static const char* t(const char* es, const char* en) {
    return brls::Platform::APP_LOCALE_DEFAULT == brls::LOCALE_EN_US ? en : es;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    size_t written = fwrite(ptr, size, nmemb, (FILE*)stream);
    return written;
}

static void ExtractZip(const std::string& temp_zip, brls::Label* status_label, brls::Rectangle* progress_fill) {
    unzFile uf = unzOpen64(temp_zip.c_str());
    if (uf == NULL) {
        brls::sync([status_label]() { status_label->setText(t("Error al abrir el archivo ZIP.", "Error opening ZIP file.")); });
        return;
    }

    unz_global_info64 gi;
    if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) {
        unzClose(uf);
        return;
    }

    for (uLong i = 0; i < gi.number_entry; i++) {
        char filename_inzip[256];
        unz_file_info64 file_info;
        
        if (unzGetCurrentFileInfo64(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) {
            break;
        }

        std::string out_path = std::string("sdmc:/") + filename_inzip;
        
        if (out_path.back() == '/') {
            fs::create_directories(out_path);
        } else {
            fs::path p(out_path);
            fs::create_directories(p.parent_path());
            
            if (unzOpenCurrentFile(uf) == UNZ_OK) {
                std::ofstream out(out_path, std::ios::binary);
                char buf[8192];
                int len = 0;
                while ((len = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) {
                    out.write(buf, len);
                }
                unzCloseCurrentFile(uf);
            }
        }
        
        if (i % 20 == 0 || (i + 1) == gi.number_entry) {
            brls::sync([status_label, progress_fill, i, gi]() {
                status_label->setText(std::string(t("Descomprimiendo: ", "Extracting: ")) + std::to_string(i + 1) + t(" de ", " of ") + std::to_string(gi.number_entry) + "...");
                float pct = (float)(i + 1) / (float)gi.number_entry;
                progress_fill->setWidth(600.0f * pct);
            });
        }
        
        if ((i + 1) < gi.number_entry) {
            if (unzGoToNextFile(uf) != UNZ_OK) {
                break;
            }
        }
    }
    unzClose(uf);
}

UpdaterView::UpdaterView() : brls::Box(brls::Axis::COLUMN), is_fetching(true), is_updating(false) {
    this->setAlignItems(brls::AlignItems::STRETCH);

    centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setAlignItems(brls::AlignItems::CENTER);
    centerBox->setJustifyContent(brls::JustifyContent::CENTER);
    centerBox->setGrow(1.0f);

    brls::Label* title = new brls::Label();
    title->setText(t("Actualización CFW The Goonies OS", "The Goonies OS CFW Update"));
    title->setFontSize(48);
    title->setMarginBottom(40);
    centerBox->addView(title);

    status_label = new brls::Label();
    status_label->setText(t("Buscando última versión en GitHub...", "Searching for latest version on GitHub..."));
    status_label->setFontSize(24);
    status_label->setMarginBottom(30);
    centerBox->addView(status_label);

    scroll_frame = new brls::ScrollingFrame();
    scroll_frame->setMarginBottom(40);
    scroll_frame->setHeight(300);
    scroll_frame->setWidth(600);

    checkboxes_box = new brls::Box(brls::Axis::COLUMN);
    checkboxes_box->setWidth(600);
    
    brls::Label* info_label = new brls::Label();
    info_label->setText(t("Selecciona las carpetas a conservar (Nintendo y emuMMC siempre se conservan).", "Select folders to keep (Nintendo and emuMMC are always kept)."));
    info_label->setFontSize(20);
    info_label->setMarginBottom(10);
    checkboxes_box->addView(info_label);
    
    scroll_frame->setContentView(checkboxes_box);
    // Do not add to centerBox yet

    progress_bar_bg = new brls::Box(brls::Axis::ROW);
    progress_bar_bg->setHeight(20);
    progress_bar_bg->setWidth(600);
    progress_bar_bg->setBackgroundColor(brls::Application::getTheme().getColor("brls/sidebar/background"));
    progress_bar_bg->setCornerRadius(10);
    progress_bar_bg->setMarginBottom(20);
    
    progress_bar_fill = new brls::Rectangle();
    progress_bar_fill->setHeight(20);
    progress_bar_fill->setWidth(0);
    progress_bar_fill->setColor(brls::Application::getTheme().getColor("brls/accent"));
    progress_bar_fill->setCornerRadius(10);
    
    progress_bar_bg->addView(progress_bar_fill);
    // Do not add progress_bar_bg to centerBox yet

    update_button = new brls::Button();
    update_button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    update_button->setText(t("Comenzar Actualización", "Start Update"));
    update_button->setVisibility(brls::Visibility::VISIBLE);
    update_button->setState(brls::ButtonState::DISABLED);
    update_button->registerClickAction([this](brls::View* view) {
        if(!is_updating && !download_url.empty()) {
            is_updating = true;
            update_button->setState(brls::ButtonState::DISABLED);
            PerformUpdate();
        }
        return true;
    });
    centerBox->addView(update_button);

    this->addView(centerBox);
    this->addView(new brls::BottomBar());

    this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    brls::async([this]() { FetchLatestVersion(); });
}

UpdaterView::~UpdaterView() {}

brls::View* UpdaterView::create() { return new UpdaterView(); }

void UpdaterView::FetchLatestVersion() {
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string readBuffer;
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/GoodmanBCN10/The-Goonies-OS-for-Switch/releases/latest");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo Switch; WifiWebAuthApplet) AppleWebKit/606.4 (KHTML, like Gecko) NF/6.0.1.15.4 NintendoBrowser/5.1.0.22443");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            std::string search_str = "\"browser_download_url\"";
            size_t pos = readBuffer.find(search_str);
            while (pos != std::string::npos) {
                pos += search_str.length();
                
                // Find the start of the URL (the next quote)
                size_t start_quote = readBuffer.find("\"", pos);
                if (start_quote != std::string::npos) {
                    size_t end_pos = readBuffer.find("\"", start_quote + 1);
                    if (end_pos != std::string::npos) {
                        std::string url = readBuffer.substr(start_quote + 1, end_pos - start_quote - 1);
                    
                    // Sanitize URL: remove any backslashes (JSON escaped forward slashes) and carriage returns
                    url.erase(std::remove(url.begin(), url.end(), '\\'), url.end());
                    url.erase(std::remove(url.begin(), url.end(), '\r'), url.end());
                    url.erase(std::remove(url.begin(), url.end(), '\n'), url.end());

                    if (url.find(".zip") != std::string::npos) {
                        download_url = url;
                        brls::sync([this]() {
                            status_label->setText(t("Última versión encontrada. ¿Deseas instalarla?", "Latest version found. Do you want to install it?"));
                            
                            centerBox->removeView(update_button, false);
                            centerBox->addView(scroll_frame);
                            centerBox->addView(update_button);
                            
                            std::vector<std::string> default_keep = {"Nintendo", "emuMMC", "switch", "System Volume Information", "goonies_update.zip"};
                            DIR* dir = opendir("sdmc:/");
                            if (dir) {
                                struct dirent* ent;
                                while ((ent = readdir(dir)) != NULL) {
                                    if (ent->d_type == DT_DIR) {
                                        std::string dirname = ent->d_name;
                                        if (dirname == "." || dirname == "..") continue;
                                        bool is_default = false;
                                        for (const auto& d : default_keep) {
                                            if (d == dirname) is_default = true;
                                        }
                                        if (!is_default) {
                                            brls::BooleanCell* cell = new brls::BooleanCell();
                                            cell->init(dirname, false, [](bool){});
                                            checkboxes_box->addView(cell);
                                            folder_toggles.push_back(cell);
                                        }
                                    }
                                }
                                closedir(dir);
                            }

                            if (!folder_toggles.empty()) {
                                folder_toggles.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, update_button);
                                update_button->setCustomNavigationRoute(brls::FocusDirection::UP, folder_toggles.back());
                                brls::Application::giveFocus(folder_toggles.front());
                            } else {
                                update_button->setCustomNavigationRoute(brls::FocusDirection::UP, nullptr);
                                brls::Application::giveFocus(update_button);
                            }

                            scroll_frame->invalidate();
                            checkboxes_box->invalidate();
                            this->invalidate();

                            update_button->setState(brls::ButtonState::ENABLED);
                        });
                        return;
                    }
                    }
                }
                pos = readBuffer.find(search_str, pos);
            }
            brls::sync([this, readBuffer]() {
                if (readBuffer.find("API rate limit") != std::string::npos) {
                    status_label->setText(t("Error: Límite de peticiones de GitHub excedido.", "Error: GitHub API rate limit exceeded."));
                } else {
                    status_label->setText(t("Error: No se encontró un archivo .zip en el release.", "Error: No .zip file found in release."));
                }
            });
        } else {
            brls::sync([this]() {
                status_label->setText(t("Error de conexión al buscar actualizaciones.", "Connection error while checking for updates."));
            });
        }
    }
}

void UpdaterView::PerformUpdate() {
    brls::async([this]() {
        brls::sync([this]() {
            status_label->setText(t("Descargando actualización...", "Downloading update..."));
        });

        std::string temp_zip = "sdmc:/goonies_update.zip";
        
        appletSetMediaPlaybackState(true);
        
        CURL* curl = curl_easy_init();
        if (curl) {
            FILE* fp = fopen(temp_zip.c_str(), "wb");
            if (fp) {
                curl_easy_setopt(curl, CURLOPT_URL, download_url.c_str());
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo Switch; WifiWebAuthApplet) AppleWebKit/606.4 (KHTML, like Gecko) NF/6.0.1.15.4 NintendoBrowser/5.1.0.22443");
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
                
                CURLcode res = curl_easy_perform(curl);
                fclose(fp);
                curl_easy_cleanup(curl);

                if (res != CURLE_OK) {
                    std::ofstream log("sdmc:/url_debug.txt");
                    log << "Length: " << download_url.length() << "\nURL: " << download_url << "\nError: " << res << std::endl;
                    log.close();

                    std::string err_msg = "Error 3 - URL len: " + std::to_string(download_url.length());
                    brls::sync([this, err_msg]() { status_label->setText(err_msg); });
                    appletSetMediaPlaybackState(false);
                    return;
                }
            } else {
                appletSetMediaPlaybackState(false);
                return;
            }
        } else {
            appletSetMediaPlaybackState(false);
            return;
        }
        
        brls::sync([this]() { 
            status_label->setText(t("Limpiando SD e instalando actualización...", "Cleaning SD and installing update...")); 
            centerBox->removeView(scroll_frame, false);
            centerBox->removeView(update_button, false);
            centerBox->addView(progress_bar_bg);
        });
        
        std::vector<std::string> keep_folders = {"Nintendo", "emuMMC", "switch", "goonies_update.zip", "System Volume Information"};
        for (auto* toggle : folder_toggles) {
            if (toggle->isOn()) {
                keep_folders.push_back(toggle->title->getFullText());
            }
        }
        
        try {
            DIR* dir = opendir("sdmc:/");
            if (dir) {
                struct dirent* ent;
                while ((ent = readdir(dir)) != NULL) {
                    std::string filename = ent->d_name;
                    if (filename == "." || filename == "..") continue;
                    
                    bool keep = false;
                    for (const auto& k : keep_folders) {
                        if (filename == k) {
                            keep = true;
                            break;
                        }
                    }
                    if (!keep) {
                        std::string full_path = "sdmc:/" + filename;
                        if (ent->d_type == DT_DIR) {
                            fs::remove_all(full_path);
                        } else {
                            fs::remove(full_path);
                        }
                    }
                }
                closedir(dir);
            }
        } catch (const std::exception& e) {
        }

        brls::sync([this]() { status_label->setText(t("Extrayendo archivos...", "Extracting files...")); });
        ExtractZip(temp_zip, status_label, progress_bar_fill);

        fs::remove(temp_zip);

        appletSetMediaPlaybackState(false);

        brls::sync([this]() {
            centerBox->removeView(progress_bar_bg, false);
            centerBox->addView(update_button);
            status_label->setText(t("¡Actualización completada con éxito!", "Update completed successfully!"));
            update_button->setText(t("Cerrar App", "Close App"));
            update_button->setState(brls::ButtonState::ENABLED);
            update_button->registerClickAction([](brls::View* view) {
                brls::Application::quit();
                return true;
            });
        });
    });
}

int UpdaterView::ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (dltotal > 0) {
        int percent = (int)((dlnow * 100) / dltotal);
        UpdaterView* view = static_cast<UpdaterView*>(clientp);
        if (percent != view->last_progress_percent) {
            view->last_progress_percent = percent;
            std::string text = std::string(t("Descargando actualización... ", "Downloading update... ")) + std::to_string(percent) + "%";
            brls::sync([view, text, percent]() { 
                view->status_label->setText(text); 
                view->progress_bar_fill->setWidth(600.0f * ((float)percent / 100.0f));
            });
        }
    }
    return 0;
}

} // namespace goonies::ui
