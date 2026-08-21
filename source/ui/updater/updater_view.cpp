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

#include "ui/common/ui_helpers.hpp"
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    size_t written = fwrite(ptr, size, nmemb, (FILE*)stream);
    return written;
}

static void remove_recursive(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return;
    
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path.c_str());
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                std::string name = ent->d_name;
                if (name == "." || name == "..") continue;
                std::string child = path + "/" + name;
                remove_recursive(child);
            }
            closedir(dir);
        }
        rmdir(path.c_str());
    } else {
        unlink(path.c_str());
    }
}

static void ExtractZip(const std::string& temp_zip, const std::string& target_dir, brls::Label* status_label, brls::Rectangle* progress_fill) {
    unzFile uf = unzOpen64(temp_zip.c_str());
    if (uf == NULL) {
        brls::sync([status_label]() { status_label->setText(t("Error al abrir el archivo ZIP.", "Error opening ZIP file.", "Erro ao abrir arquivo ZIP.")); });
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

        std::string out_path = target_dir + "/" + filename_inzip;
        
        std::error_code ec;
        if (out_path.back() == '/') {
            fs::create_directories(out_path, ec);
        } else {
            fs::path p(out_path);
            fs::create_directories(p.parent_path(), ec);
            
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
                status_label->setText(std::string(t("Descomprimiendo: ", "Extracting: ", "Descompactando:")) + std::to_string(i + 1) + t(" de ", " of ", "de") + std::to_string(gi.number_entry) + "...");
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

UpdaterView::UpdaterView(UpdateMode mode) : brls::Box(brls::Axis::COLUMN), is_fetching(true), is_updating(false), current_mode(mode) {
    this->setAlignItems(brls::AlignItems::STRETCH);

    centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setAlignItems(brls::AlignItems::CENTER);
    centerBox->setJustifyContent(brls::JustifyContent::CENTER);
    centerBox->setGrow(1.0f);

    brls::Label* title = new brls::Label();
    if (current_mode == UpdateMode::Firmware) {
        title->setText(t("Actualización de Firmware", "Firmware Update", "Atualização de Firmware"));
    } else {
        title->setText(t("Actualización CFW The Goonies OS", "The Goonies OS CFW Update", "Atualização do sistema operacional CFW The Goonies"));
    }
    title->setFontSize(48);
    title->setMarginBottom(40);
    centerBox->addView(title);

    status_label = new brls::Label();
    status_label->setText(t("Buscando última versión en GitHub...", "Searching for latest version on GitHub...", "Procurando pela versão mais recente no GitHub..."));
    status_label->setFontSize(24);
    status_label->setMarginBottom(30);
    centerBox->addView(status_label);

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
    update_button->setText(t("Comenzar Actualización", "Start Update", "Iniciar atualização"));
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

    this->registerAction(t("Volver", "Back", "Retornar"), brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    brls::async([this]() { FetchLatestVersion(); });
}

UpdaterView::~UpdaterView() {}

brls::View* UpdaterView::create(UpdateMode mode) { return new UpdaterView(mode); }

void UpdaterView::FetchLatestVersion() {
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string readBuffer;
        std::string target_url = (current_mode == UpdateMode::Firmware) ? 
            "https://api.github.com/repos/GoodmanBCN10/NS_Firmware/releases/latest" : 
            "https://api.github.com/repos/GoodmanBCN10/The-Goonies-OS-for-Switch/releases/latest";
        curl_easy_setopt(curl, CURLOPT_URL, target_url.c_str());
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
                            status_label->setText(t("Última versión encontrada. ¿Deseas instalarla?", "Latest version found. Do you want to install it?", "Última versão encontrada. Você quer instalá-lo?"));
                            
                            this->invalidate();

                            update_button->setState(brls::ButtonState::ENABLED);
                            brls::Application::giveFocus(update_button);
                        });
                        return;
                    }
                    }
                }
                pos = readBuffer.find(search_str, pos);
            }
            brls::sync([this, readBuffer]() {
                if (readBuffer.find("API rate limit") != std::string::npos) {
                    status_label->setText(t("Error: Límite de peticiones de GitHub excedido.", "Error: GitHub API rate limit exceeded.", "Erro: limite de solicitação do GitHub excedido."));
                } else {
                    status_label->setText(t("Error: No se encontró un archivo .zip en el release.", "Error: No .zip file found in release.", "Erro: um arquivo .zip não foi encontrado na versão."));
                }
            });
        } else {
            brls::sync([this]() {
                status_label->setText(t("Error de conexión al buscar actualizaciones.", "Connection error while checking for updates.", "Erro de conexão ao verificar atualizações."));
            });
        }
    }
}

void UpdaterView::PerformUpdate() {
    brls::async([this]() {
        brls::sync([this]() {
            status_label->setText(t("Descargando actualización...", "Downloading update...", "Baixando atualização..."));
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
            if (current_mode == UpdateMode::Firmware) {
                status_label->setText(t("Preparando firmware...", "Preparing firmware...", "Preparando firmware...")); 
            } else {
                status_label->setText(t("Limpiando SD e instalando actualización...", "Cleaning SD and installing update...", "Limpando SD e instalando atualização...")); 
            }
            centerBox->removeView(update_button, false);
            centerBox->addView(progress_bar_bg);
        });

        std::error_code ec;

        if (current_mode == UpdateMode::Firmware) {
            fs::remove_all("sdmc:/firmware", ec);
            fs::create_directories("sdmc:/firmware", ec);
            
            brls::sync([this]() { status_label->setText(t("Extrayendo archivos de firmware...", "Extracting firmware files...", "Extraindo arquivos de firmware...")); });
            ExtractZip(temp_zip, "sdmc:/firmware", status_label, progress_bar_fill);
            
            fs::remove(temp_zip, ec);
        } else {
            fs::remove_all("sdmc:/switch_old", ec);
            fs::rename("sdmc:/switch", "sdmc:/switch_old", ec);
            
            std::vector<std::string> keep_folders = {"Nintendo", "emuMMC", "bootloader", "goonies_update.zip", "System Volume Information", "firmware"};
            
            DIR* dir = opendir("sdmc:/");
            if (dir) {
                struct dirent* ent;
                while ((ent = readdir(dir)) != NULL) {
                    std::string filename = ent->d_name;
                    if (filename == "." || filename == "..") continue;
                    
                    bool keep = false;
                    for (const auto& k : keep_folders) {
                        if (strcasecmp(filename.c_str(), k.c_str()) == 0) {
                            keep = true;
                            break;
                        }
                    }
                    if (!keep) {
                        std::string full_path = "sdmc:/" + filename;
                        remove_recursive(full_path);
                    }
                }
                closedir(dir);
            }

            // Rename atmosphere/contents and exefs_patches to avoid locked sysmodule issues causing 0xffe on reboot
            fs::remove_all("sdmc:/atmosphere/contents_old", ec);
            fs::rename("sdmc:/atmosphere/contents", "sdmc:/atmosphere/contents_old", ec);
            fs::remove_all("sdmc:/atmosphere/exefs_patches_old", ec);
            fs::rename("sdmc:/atmosphere/exefs_patches", "sdmc:/atmosphere/exefs_patches_old", ec);
            fs::remove("sdmc:/atmosphere/stratosphere.romfs_old", ec);
            fs::rename("sdmc:/atmosphere/stratosphere.romfs", "sdmc:/atmosphere/stratosphere.romfs_old", ec);
            fs::remove("sdmc:/atmosphere/package3_old", ec);
            fs::rename("sdmc:/atmosphere/package3", "sdmc:/atmosphere/package3_old", ec);

            // Backup hekate_ipl.ini to preserve emuMMC config
            if (fs::exists("sdmc:/bootloader/hekate_ipl.ini")) {
                fs::copy_file("sdmc:/bootloader/hekate_ipl.ini", "sdmc:/bootloader/hekate_ipl.ini.bak", fs::copy_options::overwrite_existing, ec);
            }

            brls::sync([this]() { status_label->setText(t("Extrayendo archivos...", "Extracting files...", "Extraindo arquivos...")); });
            ExtractZip(temp_zip, "sdmc:", status_label, progress_bar_fill);

            // Restore hekate_ipl.ini
            if (fs::exists("sdmc:/bootloader/hekate_ipl.ini.bak")) {
                fs::copy_file("sdmc:/bootloader/hekate_ipl.ini.bak", "sdmc:/bootloader/hekate_ipl.ini", fs::copy_options::overwrite_existing, ec);
                fs::remove("sdmc:/bootloader/hekate_ipl.ini.bak", ec);
            }

            fs::remove(temp_zip, ec);
        }

        appletSetMediaPlaybackState(false);

        if (current_mode == UpdateMode::Firmware) {
            brls::sync([this]() { status_label->setText(t("Instalando firmware con Daybreak...", "Installing firmware...", "Instalando firmware...")); });
            
            Result rc = amssuInitialize();
            if (R_FAILED(rc)) {
                brls::sync([this]() { status_label->setText("Error: amssuInitialize falló"); });
                return;
            }
            
            std::string fw_path = "/firmware/";
            DIR* dir = opendir("sdmc:/firmware");
            if (dir) {
                struct dirent* ent;
                while ((ent = readdir(dir)) != NULL) {
                    if (ent->d_type == DT_DIR) {
                        std::string dirname = ent->d_name;
                        if (dirname == "." || dirname == "..") continue;
                        
                        std::string subpath = "sdmc:/firmware/" + dirname;
                        DIR* subdir = opendir(subpath.c_str());
                        if (subdir) {
                            struct dirent* subent;
                            bool has_nca = false;
                            while ((subent = readdir(subdir)) != NULL) {
                                std::string fname = subent->d_name;
                                if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".nca") {
                                    has_nca = true;
                                    break;
                                }
                            }
                            closedir(subdir);
                            if (has_nca) {
                                fw_path = "/firmware/" + dirname + "/";
                                break;
                            }
                        }
                    } else {
                        // Directly inside /firmware?
                        std::string fname = ent->d_name;
                        if (fname.length() > 4 && fname.substr(fname.length() - 4) == ".nca") {
                            fw_path = "/firmware/";
                            break; // Stop looking, it's right here!
                        }
                    }
                }
                closedir(dir);
            }

            AmsSuUpdateInformation info{};
            rc = amssuGetUpdateInformation(fw_path.c_str(), &info);
            if (R_FAILED(rc)) {
                amssuExit();
                brls::sync([this]() { status_label->setText("Error: amssuGetUpdateInformation falló"); });
                return;
            }

            AmsSuUpdateValidationInfo validation{};
            Result out_rc = 0;
            Result out_exfat_rc = 0;
            rc = amssuValidateUpdate(fw_path.c_str(), &validation, &out_rc, &out_exfat_rc);
            if (R_FAILED(rc) || R_FAILED(out_rc)) {
                amssuExit();
                brls::sync([this]() { status_label->setText("Error: Firmware corrupto o inválido"); });
                return;
            }

            bool use_exfat = info.exfat_supported && R_SUCCEEDED(out_exfat_rc);

            rc = amssuSetupUpdate(NULL, 0x100000, fw_path.c_str(), use_exfat);
            if (R_FAILED(rc)) {
                amssuExit();
                brls::sync([this]() { status_label->setText("Error: amssuSetupUpdate falló"); });
                return;
            }
            
            AsyncResult prepare{};
            rc = amssuRequestPrepareUpdate(&prepare);
            if (R_FAILED(rc)) {
                amssuExit();
                brls::sync([this]() { status_label->setText("Error: amssuRequestPrepareUpdate falló"); });
                return;
            }
            
            while (true) {
                rc = asyncResultWait(&prepare, 0);
                if (R_FAILED(rc) && rc != 0xea01) break;
                if (R_SUCCEEDED(rc)) {
                    asyncResultGet(&prepare);
                    asyncResultClose(&prepare);
                    break;
                }
                bool prepared = false;
                amssuHasPreparedUpdate(&prepared);
                if (prepared) break;
                
                NsSystemUpdateProgress progress{};
                if (R_SUCCEEDED(amssuGetPrepareUpdateProgress(&progress))) {
                    float pct = 0.0f;
                    if (progress.total_size > 0) {
                        pct = (float)progress.current_size / (float)progress.total_size;
                    }
                    brls::sync([this, pct]() {
                        status_label->setText(t("Preparando sistema: ", "Preparing system: ", "Preparando sistema: ") + std::to_string((int)(pct * 100)) + "%");
                        progress_bar_fill->setWidth(600.0f * pct);
                    });
                }
                svcSleepThread(100'000'000ULL);
            }
            
            rc = amssuApplyPreparedUpdate();
            amssuExit();
            if (R_FAILED(rc)) {
                brls::sync([this]() { status_label->setText("Error: amssuApplyPreparedUpdate falló"); });
                return;
            }
        }

        auto show_success_screen = [this]() {
            brls::sync([this]() {
                centerBox->removeView(progress_bar_bg, false);
                centerBox->addView(update_button);
                status_label->setText(t("¡Actualización completada con éxito!", "Update completed successfully!", "Atualização concluída com sucesso!"));
                update_button->setText(t("Reiniciar", "Reboot", "Reiniciar"));
                update_button->setState(brls::ButtonState::ENABLED);
                update_button->registerClickAction([](brls::View* view) {
                    Result rc = spsmInitialize();
                    if (R_SUCCEEDED(rc)) {
                        spsmShutdown(true);
                        spsmExit();
                    }
                    return true;
                });
            });
        };
        show_success_screen();
    });
}

int UpdaterView::ProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (dltotal > 0) {
        int percent = (int)((dlnow * 100) / dltotal);
        UpdaterView* view = static_cast<UpdaterView*>(clientp);
        if (percent != view->last_progress_percent) {
            view->last_progress_percent = percent;
            std::string text = std::string(t("Descargando actualización... ", "Downloading update... ", "Baixando atualização...")) + std::to_string(percent) + "%";
            brls::sync([view, text, percent]() { 
                view->status_label->setText(text); 
                view->progress_bar_fill->setWidth(600.0f * ((float)percent / 100.0f));
            });
        }
    }
    return 0;
}

} // namespace goonies::ui
