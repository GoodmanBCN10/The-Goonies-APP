#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include "installer/installer_core.hpp"
#include <usbhsfs.h>
#include <vector>
#include <set>
#include "yati/nx/es.hpp"
#include <string>
#include <dirent.h>
#include "forwarder.hpp"
#include "nro.hpp"
#include "utils/utils.hpp"
#include "mtp/haze_helper.hpp"
#include <sys/stat.h>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <map>
#include <fstream>
#include "catalog.hpp"
#include "torrent_engine.hpp"
#include <sys/statvfs.h>
#include <switch/services/set.h>
#include <thread>
#include <algorithm>
#include <curl/curl.h>
#include <pthread.h>

std::string GetUsbMountPath() {
    u32 count = usbHsFsGetMountedDeviceCount();
    if (count > 0) {
        UsbHsFsDevice devices[5];
        memset(devices, 0, sizeof(devices));
        u32 listed = usbHsFsListMountedDevices(devices, 5);
        for (u32 i = 0; i < listed; i++) {
            std::string name(devices[i].name, strnlen(devices[i].name, 32));
            if (!name.empty()) {
                if (name.back() != ':') name += ":";
                name += "/";
                DIR* dir = opendir(name.c_str());
                if (dir) {
                    closedir(dir);
                    return name;
                }
            }
        }
        // Fallback to the first one so the error is printed
        if (listed > 0) {
            std::string name(devices[0].name, strnlen(devices[0].name, 32));
            if (!name.empty()) {
                if (name.back() != ':') name += ":";
                name += "/";
            }
            return name;
        }
    }
    return "";
}

std::string GetTitleIdHex(u64 titleId) {
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(16) << std::hex << std::uppercase << titleId;
    return stream.str();
}

struct ContentItem {
    std::string type;
    u64 titleId;
    u32 version;
    u64 size;
    std::string storage;
};

std::string FormatSize(u64 size) {
    if (size < 1024 * 1024) {
        float kb = (float)size / 1024.0f;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << kb << " KB";
        return ss.str();
    } else {
        float mb = (float)size / (1024.0f * 1024.0f);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << mb << " MB";
        return ss.str();
    }
}

void FetchContentsFromDB(NcmContentMetaDatabase* db, u64 targetId, const std::string& typeName, const std::string& storageName, std::vector<ContentItem>& outList) {
    NcmContentMetaKey key;
    if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(db, &key, targetId))) {
        s32 count = 0;
        NcmContentInfo infos[20];
        if (R_SUCCEEDED(ncmContentMetaDatabaseListContentInfo(db, &count, infos, 20, &key, 0))) {
            u64 totalSize = 0;
            for (int i = 0; i < count; i++) {
                totalSize += (u64)infos[i].size_low | ((u64)infos[i].size_high << 32);
            }
            ContentItem item;
            item.type = typeName;
            item.titleId = targetId;
            item.version = key.version;
            item.size = totalSize;
            item.storage = storageName;
            outList.push_back(item);
        }
    }
}

std::vector<ContentItem> GetGameContents(u64 baseTitleId) {
    std::vector<ContentItem> list;
    
    NcmStorageId storages[] = {NcmStorageId_SdCard, NcmStorageId_BuiltInUser};
    std::string storageNames[] = {"Tarjeta microSD", "Memoria Interna"};
    
    for (int s = 0; s < 2; s++) {
        NcmContentMetaDatabase db;
        if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storages[s]))) {
            
            // Base Application
            FetchContentsFromDB(&db, baseTitleId, "Aplicación", storageNames[s], list);
            
            // Required System Version (solo lo miramos en la base)
            if (s == 0 || list.size() > 0) { // Si ya tenemos contenido
                NcmContentMetaKey key;
                if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, baseTitleId))) {
                    u32 reqVersion = 0;
                    if (R_SUCCEEDED(ncmContentMetaDatabaseGetRequiredSystemVersion(&db, &reqVersion, &key))) {
                        // reqVersion suele estar en un formato específico, pero al menos lo capturamos
                        // Aunque libnx tiene ncmContentMetaDatabaseGetRequiredSystemVersion, vamos a usarlo.
                    }
                }
            }
            
            // Update
            FetchContentsFromDB(&db, baseTitleId | 0x800, "Actualizar", storageNames[s], list);
            
            // DLCs (brute-forcing typical DLC IDs: baseTitleId + 0x1000 + i)
            u64 dlcBase = (baseTitleId & 0xFFFFFFFFFFFFF000ULL) | 0x1000ULL;
            for (int i = 0; i < 256; i++) {
                FetchContentsFromDB(&db, dlcBase + i, "DLC", storageNames[s], list);
            }
            
            ncmContentMetaDatabaseClose(&db);
        }
    }
    return list;
}

bool DeleteContent(u64 targetId, const std::string& storageStr) {
    NcmStorageId storage = (storageStr == "Memoria Interna") ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
    
    NcmContentMetaDatabase db;
    if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storage))) {
        NcmContentMetaKey key;
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, targetId))) {
            s32 count = 0;
            NcmContentInfo infos[20];
            if (R_SUCCEEDED(ncmContentMetaDatabaseListContentInfo(&db, &count, infos, 20, &key, 0))) {
                NcmContentStorage cs;
                if (R_SUCCEEDED(ncmOpenContentStorage(&cs, storage))) {
                    for (int i = 0; i < count; i++) {
                        ncmContentStorageDelete(&cs, &infos[i].content_id);
                    }
                    ncmContentStorageClose(&cs);
                }
            }
            ncmContentMetaDatabaseRemove(&db, &key);
            ncmContentMetaDatabaseCommit(&db);
        }
        ncmContentMetaDatabaseClose(&db);
        return true;
    }
    return false;
}

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

SDL_Color colorBgDark = {20, 20, 20, 255};
SDL_Color colorPanelBg = {40, 40, 40, 200};
SDL_Color colorPanelBorder = {60, 60, 60, 255};
SDL_Color colorTextMain = {240, 240, 240, 255};
SDL_Color colorAccent = {255, 153, 0, 255};

int settingAccentColor = 0;

void UpdateAccentColor() {
    switch (settingAccentColor) {
        case 0: colorAccent = {255, 153, 0, 255}; break;
        case 1: colorAccent = {0, 191, 255, 255}; break;
        case 2: colorAccent = {50, 205, 50, 255}; break;
        case 3: colorAccent = {153, 50, 204, 255}; break;
        default: colorAccent = {255, 153, 0, 255}; break;
    }
}

enum AppState {
    STATE_HOME,
    STATE_MTP,
    STATE_GAMES,
    STATE_HOMEBREW,
    STATE_SAVES,
    STATE_EXPLORER,
    STATE_SETTINGS,
    STATE_LANGUAGE_SELECT,
    STATE_GAME_DETAILS,
    STATE_INSTALLING,
    STATE_USB,
    STATE_USB_INSTALL,
    STATE_SUBMENU_INSTALL,
    STATE_SUBMENU_DOWNLOAD,
    STATE_STORE_GOONIES,
    STATE_STORE_TORRENT,
    STATE_TORRENT_DOWNLOAD
};

AppState currentState = STATE_HOME;

struct InstalledGame {
    u64 titleId;
    std::string name;
    SDL_Texture* icon;
};
std::vector<InstalledGame> installedGamesList;
int selectedGameIndex = 0;
int selectedGameDetailsIndex = 0;
bool isGridView = true; // Por defecto cuadricula

bool isSavesPopupOpen = false;
int savesPopupSelection = 0;
bool isDetailsPopupOpen = false; // Popup informativo (A)
bool isOptionsPopupOpen = false; // Menú de Opciones (X)
int optionsPopupSelection = 0; // 0 = Desinstalar, 1 = Cancelar
bool isDeleteConfirmOpen = false; // Confirmación final de borrado
int deleteConfirmSelection = 0; // 0 = No, 1 = Sí

bool isHomebrewPopupOpen = false;
int homebrewPopupSelection = 0; // 0 = Lanzar, 1 = Crear Forwarder, 2 = Cancelar

std::string popupMessage = ""; // Para mostrar estado ("Copiando...", "Éxito")

int selectedSettingIndex = 0;
bool settingDarkMode = true;

std::vector<std::string> usbInstallQueue;
int usbInstallCurrentIndex = 0;
bool settingCheckUpdates = true;
int settingLanguage = 0; // 0=ES, 1=EN

std::map<std::string, std::vector<std::string>> langDict = {
    {"menu_install", {"Instalar juegos", "Install games"}},
    {"menu_download", {"Descarga de juegos", "Download games"}},
    {"menu_submenu_mtp", {"Instalar por MTP", "Install via MTP"}},
    {"menu_submenu_usb", {"Instalar desde pendrive o disco", "Install from USB drive"}},
    {"menu_submenu_sd", {"Instalar desde microSD", "Install from microSD"}},
    {"menu_store_goonies", {"Descargar juego desde The Goonies", "Download game from The Goonies"}},
    {"menu_store_torrent", {"Descargar desde archivo .torrent", "Download from .torrent file"}},
    {"app_title", {"THE GOONIES INSTALLER", "THE GOONIES INSTALLER"}},
    {"menu_mtp", {"Instalar juegos por MTP", "Install games via MTP"}},
    {"menu_usb", {"Instalar juegos desde USB o disco", "Install games from USB drive"}},
    {"menu_games", {"Juegos Instalados", "Installed Games"}},
    {"menu_homebrew", {"APPs homebrew instaladas", "Installed Homebrew APPs"}},
    {"menu_saves", {"Partidas Guardadas (Saves)", "Save Data Manager"}},
    {"menu_explorer", {"Explorar microSD", "Explore microSD"}},
    {"menu_settings", {"Ajustes", "Settings"}},
    {"app_subtitle", {"Instalador inteligente para Nintendo Switch", "Smart installer for Nintendo Switch"}},
    {"app_lang", {"Lang: Español", "Lang: English"}},
    {"btn_select", {"Seleccionar", "Select"}},
    {"btn_back", {"Volver", "Back"}},
    {"btn_view", {"Vista", "View"}},
    {"title_games", {"Juegos Instalados", "Installed Games"}},
    {"title_saves", {"Partidas Guardadas (Backup)", "Save Data (Backup)"}},
    {"no_games", {"No hay juegos instalados o error leyendo ns.", "No installed games found."}},
    {"unknown_game", {"Desconocido", "Unknown"}},
    {"popup_title", {"GESTOR DE PARTIDAS", "SAVE DATA MANAGER"}},
    {"popup_sd_path", {"Ruta SD: sdmc:/switch/thegoonies_installer/saves/", "SD Path: sdmc:/switch/thegoonies_installer/saves/"}},
    {"popup_opt_backup", {"1. Hacer Backup a SD", "1. Backup to SD"}},
    {"popup_opt_restore", {"2. Importar desde SD", "2. Restore from SD"}},
    {"popup_opt_cancel", {"3. Cancelar", "3. Cancel"}},
    {"popup_btn_ok", {"Aceptar", "Accept"}},
    {"msg_backup_wait", {"Copiando a la SD... Espere.", "Copying to SD... Please wait."}},
    {"msg_backup_ok", {"Backup Completado!", "Backup Completed!"}},
    {"msg_backup_err_mount", {"Error: No se pudo montar el Save (No hay partida)", "Error: Could not mount Save (No save found)"}},
    {"msg_err_profile", {"Error: No hay perfil seleccionado", "Error: No profile selected"}},
    {"msg_restore_wait", {"Restaurando desde la SD... Espere.", "Restoring from SD... Please wait."}},
    {"msg_restore_ok", {"Partida Importada con Exito!", "Save Restored Successfully!"}},
    {"msg_restore_err_mount", {"Error: No se pudo montar el Save destino", "Error: Could not mount target Save"}},
    {"msg_restore_err_notfound", {"Error: No hay backup en la SD para este juego", "Error: No backup found on SD for this game"}},
    {"title_settings", {"Ajustes de la Aplicación", "Application Settings"}},
    {"set_dark_on", {"[ ON ] Tema Oscuro", "[ ON ] Dark Theme"}},
    {"set_dark_off", {"[ OFF ] Tema Claro", "[ OFF ] Light Theme"}},
    {"set_grid_on", {"[ ON ] Vista en Cuadricula", "[ ON ] Grid View"}},
    {"set_grid_off", {"[ OFF ] Vista en Lista", "[ OFF ] List View"}},
    {"set_lang", {"[ ES ] Idioma", "[ EN ] Language"}},
    {"set_help", {"(Pulsa A para cambiar, B para volver)", "(Press A to change, B to return)"}},
    {"title_lang", {"SELECCION DE IDIOMA", "LANGUAGE SELECTION"}},
    {"lang_es", {"Español", "Spanish"}},
    {"lang_en", {"Ingles", "English"}},
    {"sd_space", {"SD: ", "SD: "}},
    {"gb", {" GB", " GB"}},
    {"btn_x", {"X", "X"}},
    {"btn_a", {"A", "A"}},
    {"btn_b", {"B", "B"}},
    {"sys_info", {"Información del Sistema", "System Information"}},
    {"sys_fw", {"Firmware Consola: ", "Console Firmware: "}},
    {"sys_app_ver", {"Versión App: ", "App Version: "}},
    {"sys_sd", {"Espacio microSD:", "microSD Space:"}},
    {"sys_used", {"% Usado)", "% Used)"}},
    {"sys_prefs", {"Preferencias", "Preferences"}},
    {"sys_lang", {"Idioma:", "Language:"}},
    {"sys_color", {"Tema de Color:", "Accent Color:"}},
    {"mtp_title", {"Instalar por MTP", "Install via MTP"}},
    {"mtp_wait1", {"Conecta el cable USB al PC", "Connect the USB cable to PC"}},
    {"mtp_wait2", {"El PC reconocerá la consola como un dispositivo MTP.", "The PC will recognize the console as an MTP device."}},
    {"mtp_wait3", {"Arrastra un archivo .nsp, .nsz o .xci para instalarlo.", "Drag a .nsp, .nsz, or .xci file to install it."}},
    {"mtp_inst_title", {"Instalando: ", "Installing: "}},
    {"mtp_last_title", {"Último archivo: ", "Last file: "}},
    {"mtp_transferred", {" transferidos", " transferred"}},
    {"mtp_err", {"ERROR DE INSTALACIÓN", "INSTALLATION ERROR"}},
    {"mtp_ok", {"COMPLETADO", "COMPLETED"}},
    {"mtp_installing", {"Instalando...", "Installing..."}},
    {"mtp_hist_title", {"Archivos recibidos en esta sesión", "Files received in this session"}},
    {"mtp_hist_nav", {"Archivos recibidos en esta sesión (use D-Pad para navegar)", "Files received in this session (use D-Pad to navigate)"}},
    {"credits", {"Desarrollado para la comunidad Switch ES - The Goonies OS por GoodmanBCN", "Developed for the Switch ES community - The Goonies OS by GoodmanBCN"}},
    {"msg_launch_err", {"Error al lanzar el juego.", "Error launching the game."}},
    {"msg_launch_base", {"Sólo se pueden iniciar aplicaciones base.", "Only base applications can be launched."}},
    {"home_exit", {"[+] Salir", "[+] Exit"}},
    {"popup_notice", {"Aviso", "Notice"}},
    {"popup_close", {"[A] Cerrar", "[A] Close"}}
};

std::string GetText(std::string key) {
    if (langDict.find(key) != langDict.end()) {
        return langDict[key][settingLanguage];
    }
    return key;
}

extern bool isGridView;

void LoadConfig() {
    std::ifstream file("sdmc:/switch/thegoonies_installer/config.ini");
    if (file.is_open()) {
        file >> settingLanguage >> settingDarkMode >> isGridView >> settingAccentColor;
        file.close();
    } else {
        currentState = STATE_LANGUAGE_SELECT;
    }
    UpdateAccentColor();
}

void SaveConfig() {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/switch/thegoonies_installer", ec);
    std::ofstream file("sdmc:/switch/thegoonies_installer/config.ini");
    if (file.is_open()) {
        file << settingLanguage << " " << settingDarkMode << " " << isGridView << " " << settingAccentColor;
        file.close();
    }
    UpdateAccentColor();
}

struct MenuButton {
    std::string text;
    AppState targetState;
    SDL_Rect rect;
    SDL_Texture* icon;
};

struct FileEntry {
    std::string name;
    bool isDir;
    bool isSelected = false;
};

std::vector<MenuButton> mainButtons;
int selectedMenuButton = 0;
int selectedSubmenuInstallIndex = 0;
int selectedSubmenuDownloadIndex = 0;

std::vector<CatalogGame> storeGamesList;
std::vector<CatalogGame> filteredStoreGamesList;
int selectedStoreGameIndex = 0;
bool isStoreLoading = false;
std::string storeLoadingMessage = "";

std::string catalogSearchQuery = "";
int catalogFilterType = 0; // 0=Todos, 1=NSP, 2=NSZ, 3=NRO
int catalogSortType = 4;   // 0=Nombre A-Z, 1=Nombre Z-A, 2=Tamaño Mayor-Menor, 3=Tamaño Menor-Mayor, 4=Novedades

// Helper to convert size string (e.g. "206.8 MB", "2.14 GB") to bytes for sorting
static double ParseSizeToMegabytes(const std::string& sizeStr) {
    if (sizeStr.empty()) return 0.0;
    double value = 0.0;
    std::stringstream ss(sizeStr);
    ss >> value;
    std::string unit;
    ss >> unit;
    // Normalize to uppercase
    std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);
    if (unit.find("GB") != std::string::npos) return value * 1024.0;
    if (unit.find("KB") != std::string::npos) return value / 1024.0;
    return value; // Assume MB by default
}

void ApplyCatalogFilter() {
    filteredStoreGamesList.clear();
    for (const auto& game : storeGamesList) {
        // 1. Text Search Filter
        if (!catalogSearchQuery.empty()) {
            std::string titleLower = game.title;
            std::string queryLower = catalogSearchQuery;
            std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
            std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
            if (titleLower.find(queryLower) == std::string::npos) {
                continue;
            }
        }
        
        // 2. Format Filter
        if (catalogFilterType > 0) {
            std::string titleUpper = game.title;
            std::transform(titleUpper.begin(), titleUpper.end(), titleUpper.begin(), ::toupper);
            if (catalogFilterType == 1 && titleUpper.find("[NSP]") == std::string::npos) continue;
            if (catalogFilterType == 2 && titleUpper.find("[NSZ]") == std::string::npos) continue;
            if (catalogFilterType == 3 && titleUpper.find("[NRO]") == std::string::npos) continue;
        }
        
        filteredStoreGamesList.push_back(game);
    }
    
    // 3. Sorting
    std::sort(filteredStoreGamesList.begin(), filteredStoreGamesList.end(), [](const CatalogGame& a, const CatalogGame& b) {
        if (catalogSortType == 0) { // Nombre A-Z
            return a.title < b.title;
        } else if (catalogSortType == 1) { // Nombre Z-A
            return a.title > b.title;
        } else if (catalogSortType == 2) { // Tamaño Mayor-Menor
            return ParseSizeToMegabytes(a.size) > ParseSizeToMegabytes(b.size);
        } else if (catalogSortType == 3) { // Tamaño Menor-Mayor
            return ParseSizeToMegabytes(a.size) < ParseSizeToMegabytes(b.size);
        } else if (catalogSortType == 4) { // Recientes / Novedades (original order from JSON)
            return a.originalIndex < b.originalIndex;
        }
        return false;
    });
    
    if (selectedStoreGameIndex >= (int)filteredStoreGamesList.size()) {
        selectedStoreGameIndex = (int)filteredStoreGamesList.size() - 1;
    }
    if (selectedStoreGameIndex < 0) selectedStoreGameIndex = 0;
}

static std::string ShowKeyboard(const std::string& title, const std::string& initialText) {
#ifdef __SWITCH__
    SwkbdConfig kbd;
    char out_text[128] = {0};
    Result rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetHeaderText(&kbd, title.c_str());
        swkbdConfigSetInitialText(&kbd, initialText.c_str());
        rc = swkbdShow(&kbd, out_text, sizeof(out_text));
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc)) {
            return std::string(out_text);
        }
    }
#endif
    return initialText;
}

int currentTorrentId = -1;
std::string currentTorrentName = "";
std::string currentTorrentCatalogSize = "";

struct QueueEntry {
    std::string title;
    std::string torrentUrl;
    std::string size;
};
std::vector<QueueEntry> torrentQueue;
u64 lastInputTime = 0;
bool isTorrentDownloading = false;

void StartTorrentDownload(const std::string& title, const std::string& torrentUrl, const std::string& catalogSize) {
    isTorrentDownloading = true;
    currentTorrentName = title;
    currentTorrentCatalogSize = catalogSize;
    currentTorrentId = -1; // starting
    
    struct ThreadArg {
        std::string url;
    };
    ThreadArg* arg = new ThreadArg{torrentUrl};
    
    static pthread_t downloadThread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024); // 1MB stack
    
    pthread_create(&downloadThread, &attr, [](void* data) -> void* {
        ThreadArg* arg = static_cast<ThreadArg*>(data);
        std::string tUrl = arg->url;
        delete arg;
        
        try {
            std::string targetTorrentUrl = tUrl;
            bool isMagnet = (tUrl.rfind("magnet:", 0) == 0);
            
            if (isMagnet) {
                std::string hash = "";
                size_t btihPos = tUrl.find("urn:btih:");
                if (btihPos != std::string::npos) {
                    hash = tUrl.substr(btihPos + 9, 40);
                    std::transform(hash.begin(), hash.end(), hash.begin(), ::toupper);
                }
                
                if (hash.length() == 40) {
                    targetTorrentUrl = "https://itorrents.org/torrent/" + hash + ".torrent";
                } else {
                    currentTorrentId = -2;
                    return nullptr;
                }
            }
            
            std::string tPath = "sdmc:/switch/thegoonies/temp.torrent";
            std::error_code ec;
            std::filesystem::remove(tPath, ec);
            std::filesystem::create_directories("sdmc:/switch/thegoonies", ec);
            
            if (Catalog::DownloadFile(targetTorrentUrl, tPath)) {
                currentTorrentId = TorrentEngine::StartDownload(tPath, "sdmc:/switch/thegoonies/downloads/");
            } else {
                if (isMagnet) {
                    size_t btihPos = tUrl.find("urn:btih:");
                    std::string hash = tUrl.substr(btihPos + 9, 40);
                    std::transform(hash.begin(), hash.end(), hash.begin(), ::toupper);
                    targetTorrentUrl = "http://torrage.info/torrent.php?h=" + hash;
                    if (Catalog::DownloadFile(targetTorrentUrl, tPath)) {
                        currentTorrentId = TorrentEngine::StartDownload(tPath, "sdmc:/switch/thegoonies/downloads/");
                        return nullptr;
                    }
                }
                currentTorrentId = -2;
            }
        } catch (...) {
            currentTorrentId = -2;
        }
        return nullptr;
    }, arg);
    
    pthread_attr_destroy(&attr);
    pthread_detach(downloadThread);
}

SDL_Texture* currentCatalogIcon = nullptr;
std::string lastCatalogIconUrl = "";
bool isCatalogIconLoading = false;
std::string catalogIconPathToLoad = ""; // Handled in main loop for thread-safe SDL loading

std::vector<FileEntry> explorerList;
std::string explorerPath = "sdmc:/";
int selectedFileIndex = 0;

std::vector<FileEntry> homebrewList;
std::string homebrewPath = "sdmc:/switch/";
int selectedHomebrewIndex = 0;

struct MtpHistoryItem {
    std::string name;
    u64 size;
    int status; // 0 = OK, 1 = Error, 2 = Instalando
    Result errorCode = 0;
    u64 freedBytes = 0;
};
std::vector<MtpHistoryItem> mtpHistory;
std::string mtpCurrentFile = "";
u32 mtpLastTicks = 0;
u64 mtpLastBytes = 0;
float mtpSpeed = 0.0f;
int mtpHistoryScroll = 0;

// Installer Globals
Installer::Core* g_installerCore = nullptr;
std::thread g_installerReadThread;
bool g_installerReadThreadActive = false;
std::string g_installerFileName = "";
u64 g_installerTotalSize = 0;

TTF_Font* fontTitle = nullptr;
TTF_Font* fontButton = nullptr;
TTF_Font* fontSmall = nullptr;
SDL_Texture* logoTexture = nullptr;
SDL_Texture* iconMtp = nullptr;
SDL_Texture* iconGames = nullptr;
SDL_Texture* iconHomebrew = nullptr;
SDL_Texture* iconSaves = nullptr;
SDL_Texture* iconExplorer = nullptr;
SDL_Texture* iconSettings = nullptr;

void DrawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center = false, int wrapWidth = 0) {
    if(!font) return;
    SDL_Surface* surface = nullptr;
    if (wrapWidth > 0) {
        surface = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, wrapWidth);
    } else {
        surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    }
    
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dest = { x, y, surface->w, surface->h };
        if (center) dest.x -= dest.w / 2;
        SDL_RenderCopy(renderer, texture, NULL, &dest);
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
    }
}

void LoadDirectory(const std::string& path) {
    explorerList.clear();
    DIR* dir = opendir(path.c_str());
    if (dir) {
        if (path != "sdmc:/" && path != "sdmc:" && 
            path.find("ums") != 0 || path.length() > 6) {
            explorerList.push_back({"..", true});
        }
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            
            struct stat st;
            std::string fullPath = path + (path.back() == '/' ? "" : "/") + name;
            stat(fullPath.c_str(), &st);
            bool isDir = S_ISDIR(st.st_mode);
            explorerList.push_back({name, isDir});
        }
        closedir(dir);
    } else {
        std::string errStr = "Error al abrir el directorio [" + path + "]: ";
        char* err = strerror(errno);
        if (err) errStr += err;
        else errStr += "Desconocido (" + std::to_string(errno) + ")";
        explorerList.push_back({errStr, false});
    }
    selectedFileIndex = 0;
}

void LoadHomebrewList() {
    homebrewList.clear();
    DIR* dir = opendir(homebrewPath.c_str());
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            
            struct stat st;
            std::string fullPath = homebrewPath + name;
            stat(fullPath.c_str(), &st);
            if (S_ISDIR(st.st_mode)) {
                // Check if dir has an .nro inside with the same name
                std::string nroPath = fullPath + "/" + name + ".nro";
                if (access(nroPath.c_str(), F_OK) == 0) {
                    homebrewList.push_back({name, true}); // isDir true represents a folder containing NRO
                }
            } else if (name.size() > 4 && name.substr(name.size() - 4) == ".nro") {
                homebrewList.push_back({name, false});
            }
        }
        closedir(dir);
    } else {
        homebrewList.push_back({"[Error al abrir sdmc:/switch/]", false});
    }
    selectedHomebrewIndex = 0;
}

void TriggerCatalogIconDownload(const std::string& iconUrl) {
    if (iconUrl.empty() || iconUrl == lastCatalogIconUrl) return;
    
    lastCatalogIconUrl = iconUrl;
    isCatalogIconLoading = true;
    
    struct IconArg {
        std::string url;
    };
    IconArg* arg = new IconArg{iconUrl};
    
    static pthread_t iconThread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024); // 512KB stack is enough for simple download
    
    pthread_create(&iconThread, &attr, [](void* data) -> void* {
        IconArg* arg = static_cast<IconArg*>(data);
        std::string url = arg->url;
        delete arg;
        
        try {
            std::string tempPath = "sdmc:/switch/thegoonies/temp_icon.png";
            // Ensure dir exists
            std::error_code ec;
            std::filesystem::create_directories("sdmc:/switch/thegoonies", ec);
            
            if (Catalog::DownloadFile(url, tempPath)) {
                // Signal main thread to load this path as texture
                catalogIconPathToLoad = tempPath;
            } else {
                isCatalogIconLoading = false;
            }
        } catch (...) {
            isCatalogIconLoading = false;
        }
        return nullptr;
    }, arg);
    pthread_attr_destroy(&attr);
    pthread_detach(iconThread);
}

void LoadStoreCatalog() {
    if (isStoreLoading) return;
    storeGamesList.clear();
    isStoreLoading = true;
    storeLoadingMessage = "Descargando catalogo...";
    selectedStoreGameIndex = 0;
    
    static pthread_t catalogThread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024); // 1MB stack for JSON parse
    
    pthread_create(&catalogThread, &attr, [](void*) -> void* {
        try {
            std::string jsonStr;
            bool ok = Catalog::DownloadString("https://raw.githubusercontent.com/Langegen/switch-games/refs/heads/main/switch_games.json", jsonStr);
            if (ok) {
                Catalog::ParseCatalog(jsonStr, storeGamesList);
                ApplyCatalogFilter();
                if (!filteredStoreGamesList.empty()) {
                    TriggerCatalogIconDownload(filteredStoreGamesList[0].iconUrl);
                }
            } else {
                storeLoadingMessage = "Error al descargar el catalogo. Verifica la red.";
            }
        } catch (...) {
            storeLoadingMessage = "Error interno al procesar catalogo.";
        }
        isStoreLoading = false;
        return nullptr;
    }, nullptr);
    pthread_attr_destroy(&attr);
    pthread_detach(catalogThread);
}


static u64 GetTitleIdFromRightsId(const FsRightsId& rights_id) {
    u64 tid = 0;
    for (int i = 0; i < 8; i++) {
        tid = (tid << 8) | rights_id.c[i];
    }
    return tid;
}

void LoadGamesList();

int CleanupOrphanedFiles(u64& out_freed_bytes) {
    int deleted_count = 0;
    out_freed_bytes = 0;
    
    // Clean placeholders in both storages
    NcmContentStorage cs;
    if (R_SUCCEEDED(ncmOpenContentStorage(&cs, NcmStorageId_SdCard))) {
        s64 initial_free_space = 0;
        s64 final_free_space = 0;
        ncmContentStorageGetFreeSpaceSize(&cs, &initial_free_space);
        ncmContentStorageCleanupAllPlaceHolder(&cs);
        ncmContentStorageGetFreeSpaceSize(&cs, &final_free_space);
        if (final_free_space > initial_free_space) {
            out_freed_bytes += (final_free_space - initial_free_space);
        }
        ncmContentStorageClose(&cs);
    }
    if (R_SUCCEEDED(ncmOpenContentStorage(&cs, NcmStorageId_BuiltInUser))) {
        s64 initial_free_space = 0;
        s64 final_free_space = 0;
        ncmContentStorageGetFreeSpaceSize(&cs, &initial_free_space);
        ncmContentStorageCleanupAllPlaceHolder(&cs);
        ncmContentStorageGetFreeSpaceSize(&cs, &final_free_space);
        if (final_free_space > initial_free_space) {
            out_freed_bytes += (final_free_space - initial_free_space);
        }
        ncmContentStorageClose(&cs);
    }

    if (installedGamesList.empty()) {
        LoadGamesList();
    }
    
    GooniesInstaller::es::Initialize();
    
    std::set<u64> installedBaseTids;
    for (const auto& game : installedGamesList) {
        installedBaseTids.insert(game.titleId);
    }
    
    std::vector<FsRightsId> common_tickets;
    if (R_SUCCEEDED(GooniesInstaller::es::GetCommonTickets(common_tickets))) {
        for (const auto& tik : common_tickets) {
            u64 tik_tid = GetTitleIdFromRightsId(tik);
            u64 base_tid = tik_tid & ~0x1FFF;
            if (installedBaseTids.find(base_tid) == installedBaseTids.end()) {
                if (R_SUCCEEDED(GooniesInstaller::es::DeleteCommonTicket(&tik))) {
                    deleted_count++;
                }
            }
        }
    }
    
    std::vector<FsRightsId> personal_tickets;
    if (R_SUCCEEDED(GooniesInstaller::es::GetPersonalisedTickets(personal_tickets))) {
        for (const auto& tik : personal_tickets) {
            u64 tik_tid = GetTitleIdFromRightsId(tik);
            u64 base_tid = tik_tid & ~0x1FFF;
            if (installedBaseTids.find(base_tid) == installedBaseTids.end()) {
                if (R_SUCCEEDED(GooniesInstaller::es::DeletePersonalizedTicket(&tik))) {
                    deleted_count++;
                }
            }
        }
    }
    
    GooniesInstaller::es::Exit();
    return deleted_count;
}

void LoadGamesList() {
    installedGamesList.clear();
    s32 totalRecordCount = 0;
    s32 offset = 0;
    const s32 batchSize = 100;
    NsApplicationRecord* records = new NsApplicationRecord[batchSize]; 
    
    while (true) {
        s32 recordCount = 0;
        if (R_FAILED(nsListApplicationRecord(records, batchSize, offset, &recordCount))) {
            break;
        }
        if (recordCount == 0) {
            break;
        }
        
        for (s32 i = 0; i < recordCount; i++) {
            InstalledGame game;
            game.titleId = records[i].application_id;
            
            game.name = GetText("unknown_game");
            
            game.icon = nullptr;
            installedGamesList.push_back(game);
        }
        
        offset += recordCount;
        if (recordCount < batchSize) {
            break; // No more records
        }
    }
    
    delete[] records;
    selectedGameIndex = 0;
}

void InstallerReadThreadFunc(std::string path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        if (g_installerCore) g_installerCore->AbortInstallation();
        g_installerReadThreadActive = false;
        return;
    }
    
    // Disable buffering for direct reads
    setvbuf(file, NULL, _IONBF, 0);
    
    const size_t chunkSize = 1024 * 1024 * 4; // 4MB
    char* buffer = new char[chunkSize];
    
    while (g_installerReadThreadActive) {
        size_t bytesRead = fread(buffer, 1, chunkSize, file);
        if (bytesRead > 0) {
            if (g_installerCore) {
                if (!g_installerCore->WriteData(buffer, bytesRead)) {
                    break;
                }
            }
        }
        
        if (bytesRead < chunkSize) {
            if (feof(file) || ferror(file)) {
                break; // EOF or error
            }
        }
    }
    
    if (g_installerCore) {
        g_installerCore->FinishInstallation();
    }
    
    delete[] buffer;
    fclose(file);
    g_installerReadThreadActive = false;
}

int main(int argc, char *argv[]) {

    romfsInit();
    SocketInitConfig sockConf = {
        .tcp_tx_buf_size = 256 * 1024,
        .tcp_rx_buf_size = 256 * 1024,
        .tcp_tx_buf_max_size = 512 * 1024,
        .tcp_rx_buf_max_size = 512 * 1024,
        .udp_tx_buf_size = 128 * 1024,
        .udp_rx_buf_size = 128 * 1024,
        .sb_efficiency = 4,
    };
    socketInitialize(&sockConf);
    nsInitialize();
    ncmInitialize();
    if (R_FAILED(accountInitialize(AccountServiceType_System))) {
        accountInitialize(AccountServiceType_Application);
    }
    Result g_usbRes = usbHsFsInitialize(0);
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) return -1;
    if (TTF_Init() == -1) return -1;
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(imgFlags) & imgFlags)) return -1;

    SDL_Window* window = SDL_CreateWindow("The Goonies Installer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    fontTitle = TTF_OpenFont("romfs:/font.ttf", 58);
    if(fontTitle) TTF_SetFontStyle(fontTitle, TTF_STYLE_BOLD);
    fontButton = TTF_OpenFont("romfs:/font.ttf", 24);
    fontSmall = TTF_OpenFont("romfs:/font.ttf", 18);
    
    SDL_Surface* logoSurface = IMG_Load("romfs:/logo.png");
    if (logoSurface) {
        logoTexture = SDL_CreateTextureFromSurface(renderer, logoSurface);
        SDL_FreeSurface(logoSurface);
    }
    
    SDL_Surface* sMtp = IMG_Load("romfs:/icon_mtp.png");
    if (sMtp) { iconMtp = SDL_CreateTextureFromSurface(renderer, sMtp); SDL_FreeSurface(sMtp); }
    
    SDL_Surface* sGames = IMG_Load("romfs:/icon_games.png");
    if (sGames) { iconGames = SDL_CreateTextureFromSurface(renderer, sGames); SDL_FreeSurface(sGames); }
    
    SDL_Surface* sHomebrew = IMG_Load("romfs:/icon_games.png"); // Reusing games icon for now
    if (sHomebrew) { iconHomebrew = SDL_CreateTextureFromSurface(renderer, sHomebrew); SDL_FreeSurface(sHomebrew); }
    
    SDL_Surface* sSaves = IMG_Load("romfs:/icon_saves.png");
    if (sSaves) { iconSaves = SDL_CreateTextureFromSurface(renderer, sSaves); SDL_FreeSurface(sSaves); }
    
    SDL_Surface* sExplorer = IMG_Load("romfs:/icon_explorer.png");
    if (sExplorer) { iconExplorer = SDL_CreateTextureFromSurface(renderer, sExplorer); SDL_FreeSurface(sExplorer); }
    
    SDL_Surface* sSettings = IMG_Load("romfs:/icon_settings.png");
    if (sSettings) { iconSettings = SDL_CreateTextureFromSurface(renderer, sSettings); SDL_FreeSurface(sSettings); }

    LoadConfig(); // Cargar configuración (puede cambiar a STATE_LANGUAGE_SELECT)
    curl_global_init(CURL_GLOBAL_DEFAULT);
    TorrentEngine::Initialize();
    {
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/switch/thegoonies/downloads", ec);
    }

    int startX = 60;
    int startY = 220;
    int boxW = 550;
    int boxH = 100;
    int gapX = 60;
    int gapY = 40;
    
    mainButtons = {
        {GetText("menu_install"), STATE_SUBMENU_INSTALL, {startX, startY, boxW, boxH}, iconMtp},
        {GetText("menu_download"), STATE_SUBMENU_DOWNLOAD, {startX + boxW + gapX, startY, boxW, boxH}, iconMtp},
        {GetText("menu_games"), STATE_GAMES, {startX, startY + boxH + gapY, boxW, boxH}, iconGames},
        {GetText("menu_saves"), STATE_SAVES, {startX + boxW + gapX, startY + boxH + gapY, boxW, boxH}, iconSaves},
        {GetText("menu_homebrew"), STATE_HOMEBREW, {startX, startY + (boxH + gapY)*2, boxW, boxH}, iconHomebrew},
        {GetText("menu_explorer"), STATE_EXPLORER, {startX + boxW + gapX, startY + (boxH + gapY)*2, boxW, boxH}, iconExplorer}
    };

    bool done = false;
    lastInputTime = armGetSystemTick();
    
    while (appletMainLoop() && !done) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        
        if (kDown != 0 || kHeld != 0) {
            lastInputTime = armGetSystemTick();
        }
        
        // Check if download is finished or errored to clear isTorrentDownloading
        if (isTorrentDownloading) {
            if (currentTorrentId == -2) {
                isTorrentDownloading = false;
            } else if (currentTorrentId > 0 && TorrentEngine::IsFinished(currentTorrentId)) {
                isTorrentDownloading = false;
            }
        }

        bool isDownloading = isTorrentDownloading;
        if (isDownloading) {
            appletSetMediaPlaybackState(true);
        } else {
            appletSetMediaPlaybackState(false);
            // Process the download queue if idle and queue is not empty
            if (!torrentQueue.empty()) {
                QueueEntry nextGame = torrentQueue.front();
                torrentQueue.erase(torrentQueue.begin());
                StartTorrentDownload(nextGame.title, nextGame.torrentUrl, nextGame.size);
            }
        }
        
        if (kDown & HidNpadButton_Plus) done = true;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) done = true;
        }

        // Thread-safe loading of downloaded cover icon
        if (!catalogIconPathToLoad.empty()) {
            std::string path = catalogIconPathToLoad;
            catalogIconPathToLoad = ""; // Reset
            
            SDL_Surface* iconSurf = IMG_Load(path.c_str());
            if (iconSurf) {
                if (currentCatalogIcon) {
                    SDL_DestroyTexture(currentCatalogIcon);
                }
                currentCatalogIcon = SDL_CreateTextureFromSurface(renderer, iconSurf);
                SDL_FreeSurface(iconSurf);
            }
            isCatalogIconLoading = false;
        }

        // Colores Dinámicos
        if (settingDarkMode) {
            colorBgDark = {30, 30, 30, 255};
            colorTextMain = {240, 240, 240, 255};
            colorPanelBg = {45, 45, 45, 200};
        } else {
            colorBgDark = {240, 240, 240, 255};
            colorTextMain = {30, 30, 30, 255};
            colorPanelBg = {200, 200, 200, 200};
        }

        // LÓGICA DE NAVEGACIÓN Y ESTADOS
        if (currentState == STATE_LANGUAGE_SELECT) {
            if (kDown & HidNpadButton_AnyDown) settingLanguage++;
            if (kDown & HidNpadButton_AnyUp) settingLanguage--;
            if (settingLanguage < 0) settingLanguage = 1;
            if (settingLanguage > 1) settingLanguage = 0;
            
            if (kDown & HidNpadButton_A) {
                SaveConfig();
                currentState = STATE_HOME;
                
                // Actualizar textos del menú
                mainButtons[0].text = GetText("menu_install");
                mainButtons[1].text = GetText("menu_download");
                mainButtons[2].text = GetText("menu_games");
                mainButtons[3].text = GetText("menu_saves");
                mainButtons[4].text = GetText("menu_homebrew");
                mainButtons[5].text = GetText("menu_explorer");
            }
        }
        else if (currentState == STATE_HOME) {
            if (kDown & HidNpadButton_Minus) {
                currentState = STATE_SETTINGS;
                selectedSettingIndex = 0;
            }
            if (kDown & HidNpadButton_AnyDown) {
                if (selectedMenuButton + 2 < (int)mainButtons.size()) selectedMenuButton += 2;
            }
            if (kDown & HidNpadButton_AnyUp) {
                if (selectedMenuButton - 2 >= 0) selectedMenuButton -= 2;
            }
            if (kDown & HidNpadButton_AnyRight) {
                if (selectedMenuButton % 2 == 0 && selectedMenuButton + 1 < (int)mainButtons.size()) selectedMenuButton += 1;
            }
            if (kDown & HidNpadButton_AnyLeft) {
                if (selectedMenuButton % 2 != 0) selectedMenuButton -= 1;
            }
            
            if (kDown & HidNpadButton_A) {
                currentState = mainButtons[selectedMenuButton].targetState;
                if (currentState == STATE_MTP) {
                    if (!g_installerCore) g_installerCore = new Installer::Core();
                    if (MTP::Init()) {
                        MTP::InitInstallMode(
                            [](const char* filename, size_t size) -> bool {
                                bool success = g_installerCore->StartInstallation(filename);
                                if (!success) return false;
                                
                                std::string fn = filename;
                                size_t lastSlash = fn.find_last_of('/');
                                if (lastSlash != std::string::npos) fn = fn.substr(lastSlash + 1);
                                
                                MtpHistoryItem item;
                                item.name = fn;
                                item.size = size;
                                item.status = 2; // Instalando
                                mtpHistory.insert(mtpHistory.begin(), item);
                                
                                mtpCurrentFile = fn;
                                mtpLastTicks = SDL_GetTicks();
                                mtpLastBytes = 0;
                                mtpSpeed = 0.0f;
                                g_installerTotalSize = size;
                                
                                return true;
                            },
                            [](const void* data, size_t size) -> bool {
                                return g_installerCore->WriteData(data, size);
                            },
                            []() {
                                g_installerCore->FinishInstallation();
                                // Do not set status to COMPLETADO here. Let the main loop handle it when IsFinished() is true.
                            }
                        );
                    }
                } else if (currentState == STATE_EXPLORER) {
                    explorerPath = "sdmc:/";
                    LoadDirectory(explorerPath);
                    selectedFileIndex = 0;
                } else if (currentState == STATE_USB) {
                    std::string usbPath = GetUsbMountPath();
                    if (!usbPath.empty()) {
                        explorerPath = usbPath;
                        LoadDirectory(explorerPath);
                        selectedFileIndex = 0;
                    } else {
                        char errbuf[256];
                        snprintf(errbuf, sizeof(errbuf), "No se detectó USB (Count: %d).\nusbHsFsInitialize devolvió: 0x%x", 
                            (int)usbHsFsGetMountedDeviceCount(), g_usbRes);
                        popupMessage = errbuf;
                        isDetailsPopupOpen = true;
                        currentState = STATE_HOME; // Cancel transition if no USB
                    }
                } else if (currentState == STATE_HOMEBREW) {
                    LoadHomebrewList();
                } else if (currentState == STATE_GAMES || currentState == STATE_SAVES) {
                    LoadGamesList();
                }
            }
        }
        else if (currentState == STATE_SUBMENU_INSTALL) {
            if (kDown & HidNpadButton_B) {
                currentState = STATE_HOME;
            }
            if (kDown & HidNpadButton_AnyDown) {
                selectedSubmenuInstallIndex++;
                if (selectedSubmenuInstallIndex > 2) selectedSubmenuInstallIndex = 0;
            }
            if (kDown & HidNpadButton_AnyUp) {
                selectedSubmenuInstallIndex--;
                if (selectedSubmenuInstallIndex < 0) selectedSubmenuInstallIndex = 2;
            }
            if (kDown & HidNpadButton_A) {
                if (selectedSubmenuInstallIndex == 0) {
                    currentState = STATE_MTP;
                    if (!g_installerCore) g_installerCore = new Installer::Core();
                    if (MTP::Init()) {
                        MTP::InitInstallMode(
                            [](const char* filename, size_t size) -> bool {
                                bool success = g_installerCore->StartInstallation(filename);
                                if (!success) return false;
                                
                                std::string fn = filename;
                                size_t lastSlash = fn.find_last_of('/');
                                if (lastSlash != std::string::npos) fn = fn.substr(lastSlash + 1);
                                
                                MtpHistoryItem item;
                                item.name = fn;
                                item.size = size;
                                item.status = 2; // Instalando
                                mtpHistory.insert(mtpHistory.begin(), item);
                                
                                mtpCurrentFile = fn;
                                mtpLastTicks = SDL_GetTicks();
                                mtpLastBytes = 0;
                                mtpSpeed = 0.0f;
                                g_installerTotalSize = size;
                                
                                return true;
                            },
                            [](const void* data, size_t size) -> bool {
                                return g_installerCore->WriteData(data, size);
                            },
                            []() {
                                g_installerCore->FinishInstallation();
                            }
                        );
                    }
                } else if (selectedSubmenuInstallIndex == 1) {
                    currentState = STATE_USB;
                    std::string usbPath = GetUsbMountPath();
                    if (!usbPath.empty()) {
                        explorerPath = usbPath;
                        LoadDirectory(explorerPath);
                        selectedFileIndex = 0;
                    } else {
                        char errbuf[256];
                        snprintf(errbuf, sizeof(errbuf), "No se detectó USB (Count: %d).\nusbHsFsInitialize devolvió: 0x%x", 
                            (int)usbHsFsGetMountedDeviceCount(), g_usbRes);
                        popupMessage = errbuf;
                        isDetailsPopupOpen = true;
                        currentState = STATE_HOME;
                    }
                } else if (selectedSubmenuInstallIndex == 2) {
                    currentState = STATE_EXPLORER;
                    explorerPath = "sdmc:/";
                    LoadDirectory(explorerPath);
                    selectedFileIndex = 0;
                }
            }
        }
        else if (currentState == STATE_SUBMENU_DOWNLOAD) {
            if (kDown & HidNpadButton_B) {
                currentState = STATE_HOME;
            }
            if (kDown & HidNpadButton_AnyDown) {
                selectedSubmenuDownloadIndex++;
                if (selectedSubmenuDownloadIndex > 1) selectedSubmenuDownloadIndex = 0;
            }
            if (kDown & HidNpadButton_AnyUp) {
                selectedSubmenuDownloadIndex--;
                if (selectedSubmenuDownloadIndex < 0) selectedSubmenuDownloadIndex = 1;
            }
            if (kDown & HidNpadButton_A) {
                if (selectedSubmenuDownloadIndex == 0) {
                    currentState = STATE_STORE_GOONIES;
                    LoadStoreCatalog();
                } else if (selectedSubmenuDownloadIndex == 1) {
                    currentState = STATE_STORE_TORRENT;
                    // For local torrents, we can use the explorer but filtered to .torrent
                    explorerPath = "sdmc:/";
                    LoadDirectory(explorerPath);
                    selectedFileIndex = 0;
                }
            }
        }

        else if (currentState == STATE_STORE_GOONIES) {
            if (kDown & HidNpadButton_B) {
                currentState = STATE_SUBMENU_DOWNLOAD;
                if (currentCatalogIcon) {
                    SDL_DestroyTexture(currentCatalogIcon);
                    currentCatalogIcon = nullptr;
                }
                lastCatalogIconUrl = "";
            }
            if (!isStoreLoading && !filteredStoreGamesList.empty()) {
                if (kDown & HidNpadButton_AnyDown) {
                    selectedStoreGameIndex++;
                    if (selectedStoreGameIndex >= (int)filteredStoreGamesList.size()) selectedStoreGameIndex = filteredStoreGamesList.size() - 1;
                    TriggerCatalogIconDownload(filteredStoreGamesList[selectedStoreGameIndex].iconUrl);
                }
                if (kDown & HidNpadButton_AnyUp) {
                    selectedStoreGameIndex--;
                    if (selectedStoreGameIndex < 0) selectedStoreGameIndex = 0;
                    TriggerCatalogIconDownload(filteredStoreGamesList[selectedStoreGameIndex].iconUrl);
                }
                
                // [Y] - Search games by name via Soft Keyboard
                if (kDown & HidNpadButton_Y) {
                    std::string query = ShowKeyboard("Buscar juego", catalogSearchQuery);
                    catalogSearchQuery = query;
                    ApplyCatalogFilter();
                    if (!filteredStoreGamesList.empty()) {
                        TriggerCatalogIconDownload(filteredStoreGamesList[selectedStoreGameIndex].iconUrl);
                    }
                }
                
                // [X] - Cycle through format filters: Todos -> NSP -> NSZ -> NRO
                if (kDown & HidNpadButton_X) {
                    catalogFilterType = (catalogFilterType + 1) % 4;
                    ApplyCatalogFilter();
                    if (!filteredStoreGamesList.empty()) {
                        TriggerCatalogIconDownload(filteredStoreGamesList[selectedStoreGameIndex].iconUrl);
                    }
                }
                
                // [L] / [R] - Cycle Sort types
                if ((kDown & HidNpadButton_L) || (kDown & HidNpadButton_R)) {
                    if (kDown & HidNpadButton_L) {
                        catalogSortType = (catalogSortType - 1 + 5) % 5;
                    } else {
                        catalogSortType = (catalogSortType + 1) % 5;
                    }
                    ApplyCatalogFilter();
                    if (!filteredStoreGamesList.empty()) {
                        TriggerCatalogIconDownload(filteredStoreGamesList[selectedStoreGameIndex].iconUrl);
                    }
                }
                
                if (kDown & HidNpadButton_A) {
                    std::string tUrl = filteredStoreGamesList[selectedStoreGameIndex].torrentUrl;
                    std::string tTitle = filteredStoreGamesList[selectedStoreGameIndex].title;
                    std::string tSize = filteredStoreGamesList[selectedStoreGameIndex].size;
                    
                    bool isDownloading = isTorrentDownloading;
                    if (isDownloading) {
                        torrentQueue.push_back({tTitle, tUrl, tSize});
                        popupMessage = "Juego añadido a la cola de descargas.\n(" + std::to_string(torrentQueue.size()) + " en cola)";
                        isDetailsPopupOpen = true;
                    } else {
                        StartTorrentDownload(tTitle, tUrl, tSize);
                        currentState = STATE_TORRENT_DOWNLOAD;
                    }
                }
            }
        }
        else if (currentState == STATE_TORRENT_DOWNLOAD) {
            if (kDown & HidNpadButton_B) {
                // Just go back to store without stopping the download
                currentState = STATE_STORE_GOONIES;
            }
            if (kDown & HidNpadButton_Y) {
                // Cancel/Stop download explicitly
                if (currentTorrentId > 0) {
                    TorrentEngine::StopDownload(currentTorrentId);
                    currentTorrentId = -1;
                }
                isTorrentDownloading = false;
                appletSetMediaPlaybackState(false);
                currentState = STATE_STORE_GOONIES;
            }
            if (kDown & HidNpadButton_A) {
                if (currentTorrentId > 0) {
                    TorrentStatus ts = TorrentEngine::GetStatus(currentTorrentId);
                    if (ts.paused) {
                        TorrentEngine::ResumeDownload(currentTorrentId);
                    } else {
                        TorrentEngine::PauseDownload(currentTorrentId);
                    }
                }
            }
            if (kDown & HidNpadButton_X) {
                if (currentTorrentId > 0 && TorrentEngine::IsFinished(currentTorrentId)) {
                    // Open explorer in downloads
                    explorerPath = "sdmc:/switch/thegoonies/downloads/";
                    LoadDirectory(explorerPath);
                    selectedFileIndex = 0;
                    currentState = STATE_EXPLORER;
                }
            }
        }
        else if (currentState == STATE_MTP) {
            if (kDown & HidNpadButton_B) {
                MTP::Exit();
                if (g_installerCore) {
                    delete g_installerCore;
                    g_installerCore = nullptr;
                }
                mtpHistory.clear();
                mtpHistoryScroll = 0;
                currentState = STATE_HOME;
            }
            if (kDown & HidNpadButton_AnyDown) {
                int maxItems = (WINDOW_HEIGHT - 360) / 40; // Approx max items
                if ((int)mtpHistory.size() > maxItems && mtpHistoryScroll < (int)mtpHistory.size() - maxItems) mtpHistoryScroll++;
            }
            if (kDown & HidNpadButton_AnyUp) {
                if (mtpHistoryScroll > 0) mtpHistoryScroll--;
            }
        }
        else if (currentState == STATE_USB_INSTALL) {
            if (kDown & HidNpadButton_B) {
                // Cancelar instalación si está en curso
                if (g_installerReadThreadActive) {
                    if (g_installerReadThread.joinable()) g_installerReadThread.join();
                    g_installerReadThreadActive = false;
                }
                if (g_installerCore) {
                    delete g_installerCore;
                    g_installerCore = nullptr;
                }
                usbInstallQueue.clear();
                mtpHistory.clear();
                currentState = STATE_USB; // Go back to USB explorer
            }
            if (kDown & HidNpadButton_AnyDown) {
                int maxItems = (WINDOW_HEIGHT - 360) / 40;
                if ((int)mtpHistory.size() > maxItems && mtpHistoryScroll < (int)mtpHistory.size() - maxItems) mtpHistoryScroll++;
            }
            if (kDown & HidNpadButton_AnyUp) {
                if (mtpHistoryScroll > 0) mtpHistoryScroll--;
            }

            // Procesar la cola
            bool threadDone = !g_installerReadThreadActive;
            bool coreDone = !g_installerCore || g_installerCore->IsFinished();
            
            if (threadDone && coreDone) {
                if (g_installerReadThread.joinable()) {
                    // Terminó el actual
                    g_installerReadThread.join();
                    g_installerReadThreadActive = false;
                    
                    if (usbInstallCurrentIndex >= 0 && usbInstallCurrentIndex < (int)usbInstallQueue.size()) {
                        mtpHistory[usbInstallCurrentIndex].status = g_installerCore->HasError() ? 1 : 0;
                        mtpHistory[usbInstallCurrentIndex].errorCode = g_installerCore->GetErrorCode();
                        mtpHistory[usbInstallCurrentIndex].freedBytes = g_installerCore->GetFreedBytes();
                    }
                    usbInstallCurrentIndex++;
                }
                
                if (usbInstallCurrentIndex < (int)usbInstallQueue.size()) {
                    // Iniciar el siguiente
                    std::string fullPath = usbInstallQueue[usbInstallCurrentIndex];
                    g_installerFileName = fullPath;
                    mtpCurrentFile = mtpHistory[usbInstallCurrentIndex].name;
                    mtpHistory[usbInstallCurrentIndex].status = 2; // Instalando
                    
                    struct stat st;
                    if (stat(fullPath.c_str(), &st) == 0) {
                        g_installerTotalSize = st.st_size;
                        mtpHistory[usbInstallCurrentIndex].size = st.st_size;
                    } else g_installerTotalSize = 0;
                    
                    if (g_installerCore) { delete g_installerCore; g_installerCore = nullptr; }
                    g_installerCore = new Installer::Core();
                    
                    mtpLastTicks = SDL_GetTicks();
                    mtpLastBytes = 0;
                    mtpSpeed = 0.0f;
                    
                    std::string basename = g_installerFileName;
                    size_t slashPos = basename.find_last_of('/');
                    if (slashPos != std::string::npos) basename = basename.substr(slashPos + 1);
                    
                    if (g_installerCore->StartInstallation(basename)) {
                        g_installerReadThreadActive = true;
                        g_installerReadThread = std::thread(InstallerReadThreadFunc, g_installerFileName);
                    } else {
                        mtpHistory[usbInstallCurrentIndex].status = 1; // Error
                        mtpHistory[usbInstallCurrentIndex].errorCode = g_installerCore->GetErrorCode();
                        usbInstallCurrentIndex++; // Intentar el siguiente en el prox loop
                    }
                }
            }
        }
        else if (currentState == STATE_HOMEBREW) {
            if (isHomebrewPopupOpen) {
                if (kDown & HidNpadButton_AnyDown) homebrewPopupSelection++;
                if (kDown & HidNpadButton_AnyUp) homebrewPopupSelection--;
                if (homebrewPopupSelection < 0) homebrewPopupSelection = 2;
                if (homebrewPopupSelection > 2) homebrewPopupSelection = 0;
                
                if (kDown & HidNpadButton_B) {
                    isHomebrewPopupOpen = false;
                    popupMessage = "";
                }
                if (kDown & HidNpadButton_A) {
                    if (popupMessage.find("Espere") != std::string::npos) {
                        // ignore input while working
                    } else if (popupMessage.find("xito") != std::string::npos || popupMessage.find("Error") != std::string::npos || popupMessage.find("implementado") != std::string::npos) {
                        isHomebrewPopupOpen = false;
                        popupMessage = "";
                    } else {
                        if (homebrewPopupSelection == 0) { // Lanzar
                            // Lógica de lanzar NRO (requiere envSetNextLoad)
                            std::string target = homebrewPath + homebrewList[selectedHomebrewIndex].name;
                            if (homebrewList[selectedHomebrewIndex].isDir) {
                                target += "/" + homebrewList[selectedHomebrewIndex].name + ".nro";
                            }
                            envSetNextLoad(target.c_str(), target.c_str());
                            done = true; // Salir de la app para que hbmenu cargue el nextLoad
                        } else if (homebrewPopupSelection == 1) { // Forwarder
                            popupMessage = "Creando Forwarder... Espere.";
                            
                            std::string target = homebrewPath + homebrewList[selectedHomebrewIndex].name;
                            if (homebrewList[selectedHomebrewIndex].isDir) {
                                target += "/" + homebrewList[selectedHomebrewIndex].name + ".nro";
                            }
                            
                            GooniesInstaller::OwoConfig owoCfg{};
                            std::vector<u8> icon;
                            std::string name = homebrewList[selectedHomebrewIndex].name;
                            std::string author = "Unknown";
                            GooniesInstaller::ReadNroAsset(target, icon, name, author, owoCfg.nacp);
                            if (name.empty()) name = homebrewList[selectedHomebrewIndex].name;
                            
                            if (icon.empty()) {
                                std::ifstream logoFile("romfs:/icon.jpg", std::ios::binary);
                                if (logoFile.is_open()) {
                                    logoFile.seekg(0, std::ios::end);
                                    icon.resize(logoFile.tellg());
                                    logoFile.seekg(0, std::ios::beg);
                                    logoFile.read(reinterpret_cast<char*>(icon.data()), icon.size());
                                }
                            }

                            
                            owoCfg.name = name;
                            owoCfg.author = author;
                            
                            owoCfg.icon = icon;
                            owoCfg.nro_path = target;
                            owoCfg.args = target;
                            
                            Result res = GooniesInstaller::install_forwarder(owoCfg, NcmStorageId_SdCard);
                            if (R_SUCCEEDED(res)) {
                                popupMessage = "Forwarder instalado con éxito";
                            } else {
                                popupMessage = "Error al instalar forwarder: " + std::to_string(res);
                            }
                        } else if (homebrewPopupSelection == 2) { // Cancelar
                            isHomebrewPopupOpen = false;
                        }
                    }
                }
            } else {
                if (kDown & HidNpadButton_B) {
                    currentState = STATE_HOME;
                }
                if (kDown & HidNpadButton_AnyDown) selectedHomebrewIndex++;
                if (kDown & HidNpadButton_AnyUp) selectedHomebrewIndex--;
                if (selectedHomebrewIndex < 0) selectedHomebrewIndex = homebrewList.size() - 1;
                if (selectedHomebrewIndex >= (int)homebrewList.size()) selectedHomebrewIndex = 0;
                
                if (kDown & HidNpadButton_A && !homebrewList.empty()) {
                    isHomebrewPopupOpen = true;
                    homebrewPopupSelection = 0;
                    popupMessage = "";
                }
            }
        }
        else if (currentState == STATE_EXPLORER || currentState == STATE_USB) {
            if (currentState == STATE_EXPLORER && (kDown & HidNpadButton_X)) {
                if (explorerPath.find("sdmc") == 0) {
                    std::string usbPath = GetUsbMountPath();
                    if (!usbPath.empty()) {
                        explorerPath = usbPath;
                        LoadDirectory(explorerPath);
                        selectedFileIndex = 0;
                    } else {
                        char errbuf[256];
                        snprintf(errbuf, sizeof(errbuf), "No se detectó USB (Count: %d).\nusbHsFsInitialize devolvió: 0x%x", 
                            (int)usbHsFsGetMountedDeviceCount(), g_usbRes);
                        popupMessage = errbuf;
                        isDetailsPopupOpen = true;
                    }
                } else {
                    explorerPath = "sdmc:/";
                    LoadDirectory(explorerPath);
                    selectedFileIndex = 0;
                }
            }
            if (kDown & HidNpadButton_B) {
                std::string usbPath = GetUsbMountPath();
                std::string usbPathNoSlash = usbPath;
                if (!usbPathNoSlash.empty() && usbPathNoSlash.back() == '/') usbPathNoSlash.pop_back();

                if (explorerPath == "sdmc:/" || explorerPath == "sdmc:" || (!usbPath.empty() && (explorerPath == usbPath || explorerPath == usbPathNoSlash))) {
                    currentState = STATE_HOME;
                } else {
                    size_t lastSlash = explorerPath.find_last_of('/');
                    if (lastSlash != std::string::npos && lastSlash > 5) {
                        explorerPath = explorerPath.substr(0, lastSlash);
                        if (explorerPath == "sdmc:") explorerPath = "sdmc:/";
                        if (!usbPathNoSlash.empty() && explorerPath == usbPathNoSlash) explorerPath = usbPath;
                        LoadDirectory(explorerPath);
                    } else {
                        if (explorerPath.find("ums") == 0 && !usbPath.empty()) explorerPath = usbPath;
                        else explorerPath = "sdmc:/";
                        LoadDirectory(explorerPath);
                    }
                }
            }
            if (kDown & HidNpadButton_AnyDown) selectedFileIndex++;
            if (kDown & HidNpadButton_AnyUp) selectedFileIndex--;
            if (selectedFileIndex < 0) selectedFileIndex = explorerList.size() - 1;
            if (selectedFileIndex >= (int)explorerList.size()) selectedFileIndex = 0;

            if (currentState == STATE_USB && (kDown & HidNpadButton_Y) && !explorerList.empty()) {
                if (!explorerList[selectedFileIndex].isDir && explorerList[selectedFileIndex].name != "..") {
                    explorerList[selectedFileIndex].isSelected = !explorerList[selectedFileIndex].isSelected;
                }
            }

            if (kDown & HidNpadButton_A && !explorerList.empty()) {
                if (explorerList[selectedFileIndex].isDir) {
                    if (explorerList[selectedFileIndex].name == "..") {
                        // Go up logic
                        std::string usbPath = GetUsbMountPath();
                        std::string usbPathNoSlash = usbPath;
                        if (!usbPathNoSlash.empty() && usbPathNoSlash.back() == '/') usbPathNoSlash.pop_back();

                        size_t lastSlash = explorerPath.find_last_of('/');
                        if (lastSlash != std::string::npos && lastSlash > 5) { // Protect "sdmc:/"
                            explorerPath = explorerPath.substr(0, lastSlash);
                            if (explorerPath == "sdmc:") explorerPath = "sdmc:/";
                            if (!usbPathNoSlash.empty() && explorerPath == usbPathNoSlash) explorerPath = usbPath;
                            LoadDirectory(explorerPath);
                        }
                    } else {
                        // Go down logic
                        if (explorerPath.back() != '/') explorerPath += "/";
                        explorerPath += explorerList[selectedFileIndex].name;
                        LoadDirectory(explorerPath);
                    }
                } else {
                    std::string filename = explorerList[selectedFileIndex].name;
                    std::string ext = filename.substr(filename.find_last_of(".") + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == "nsp" || ext == "nsz" || ext == "xci") {
                        if (currentState == STATE_USB) {
                            // Construir cola de instalación
                            usbInstallQueue.clear();
                            for (auto& item : explorerList) {
                                if (item.isSelected) {
                                    std::string p = explorerPath;
                                    if (p.back() != '/') p += "/";
                                    usbInstallQueue.push_back(p + item.name);
                                }
                            }
                            if (usbInstallQueue.empty()) {
                                std::string fullPath = explorerPath;
                                if (fullPath.back() != '/') fullPath += "/";
                                usbInstallQueue.push_back(fullPath + filename);
                            }
                            
                            // Poblar historial
                            mtpHistory.clear();
                            for (auto& p : usbInstallQueue) {
                                MtpHistoryItem h;
                                size_t ls = p.find_last_of('/');
                                h.name = (ls != std::string::npos) ? p.substr(ls + 1) : p;
                                h.size = 0; // Se calculará al iniciar
                                h.status = -1; // Pendiente
                                mtpHistory.push_back(h);
                            }
                            mtpHistoryScroll = 0;
                            usbInstallCurrentIndex = 0;
                            currentState = STATE_USB_INSTALL;
                        } else {
                            std::string fullPath = explorerPath;
                            if (fullPath.back() != '/') fullPath += "/";
                            fullPath += filename;
                            
                            g_installerFileName = fullPath;
                            
                            // get total size
                            struct stat st;
                            if (stat(fullPath.c_str(), &st) == 0) g_installerTotalSize = st.st_size;
                            else g_installerTotalSize = 0;
                            
                            // Start Installer
                            if (!g_installerCore) g_installerCore = new Installer::Core();
                            
                            std::string basename = g_installerFileName;
                            size_t slashPos = basename.find_last_of('/');
                            if (slashPos != std::string::npos) basename = basename.substr(slashPos + 1);
                            
                            if (g_installerCore->StartInstallation(basename)) {
                                g_installerReadThreadActive = true;
                                if (g_installerReadThread.joinable()) g_installerReadThread.join();
                                g_installerReadThread = std::thread(InstallerReadThreadFunc, g_installerFileName);
                                currentState = STATE_INSTALLING;
                            } else {
                                popupMessage = "Error al iniciar instalación";
                                isDetailsPopupOpen = true; // Use existing details popup to show error
                            }
                        }
                    }
                }
            }
        }
        else if (currentState == STATE_INSTALLING) {
            if (kDown & HidNpadButton_B) {
                // Cancel installation
                g_installerReadThreadActive = false; // Stop reading
                if (g_installerCore) {
                    // Let it finish and error out
                    // Ideally we'd have an abort function
                }
            }
            if (g_installerCore && (g_installerCore->IsFinished() || g_installerCore->HasError())) {
                if (kDown & HidNpadButton_A || kDown & HidNpadButton_B) {
                    if (g_installerReadThread.joinable()) g_installerReadThread.join();
                    delete g_installerCore;
                    g_installerCore = nullptr;
                    currentState = STATE_EXPLORER;
                    LoadGamesList(); // Refresh games
                }
            }
        }
        else if (currentState == STATE_GAMES || currentState == STATE_SAVES) {
            if (isSavesPopupOpen) {
                if (kDown & HidNpadButton_AnyDown) savesPopupSelection++;
                if (kDown & HidNpadButton_AnyUp) savesPopupSelection--;
                if (savesPopupSelection < 0) savesPopupSelection = 2;
                if (savesPopupSelection > 2) savesPopupSelection = 0;
                
                if (kDown & HidNpadButton_B) {
                    isSavesPopupOpen = false;
                    popupMessage = "";
                }
                
                if (kDown & HidNpadButton_A) {
                    if (savesPopupSelection == 2 || popupMessage.find("Completado") != std::string::npos || popupMessage.find("Exito") != std::string::npos) {
                        isSavesPopupOpen = false;
                        popupMessage = "";
                    } else if (savesPopupSelection == 0) {
                        // BACKUP LOGIC
                        popupMessage = GetText("msg_backup_wait");
                        AccountUid uid;
                        bool userFound = false;
                        if (R_SUCCEEDED(accountGetPreselectedUser(&uid))) userFound = true;
                        else {
                            AccountUid uids[8];
                            s32 num_uids = 0;
                            if (R_SUCCEEDED(accountListAllUsers(uids, 8, &num_uids)) && num_uids > 0) {
                                uid = uids[0];
                                userFound = true;
                            }
                        }

                        if (userFound) {
                            u64 titleId = installedGamesList[selectedGameIndex].titleId;
                            if (R_SUCCEEDED(fsdevMountSaveData("save", titleId, uid))) {
                                std::string destPath = "sdmc:/switch/thegoonies_installer/saves/" + GetTitleIdHex(titleId);
                                std::error_code ec;
                                std::filesystem::create_directories(destPath, ec);
                                std::filesystem::copy("save:/", destPath, std::filesystem::copy_options::recursive | std::filesystem::copy_options::update_existing, ec);
                                fsdevCommitDevice("save");
                                fsdevUnmountDevice("save");
                                popupMessage = GetText("msg_backup_ok");
                            } else {
                                popupMessage = GetText("msg_backup_err_mount");
                            }
                        } else {
                            popupMessage = GetText("msg_err_profile");
                        }
                    } else if (savesPopupSelection == 1) {
                        // IMPORT LOGIC
                        popupMessage = GetText("msg_restore_wait");
                        AccountUid uid;
                        bool userFound = false;
                        if (R_SUCCEEDED(accountGetPreselectedUser(&uid))) userFound = true;
                        else {
                            AccountUid uids[8];
                            s32 num_uids = 0;
                            if (R_SUCCEEDED(accountListAllUsers(uids, 8, &num_uids)) && num_uids > 0) {
                                uid = uids[0];
                                userFound = true;
                            }
                        }

                        if (userFound) {
                            u64 titleId = installedGamesList[selectedGameIndex].titleId;
                            std::string srcPath = "sdmc:/switch/thegoonies_installer/saves/" + GetTitleIdHex(titleId);
                            if (std::filesystem::exists(srcPath)) {
                                if (R_SUCCEEDED(fsdevMountSaveData("save", titleId, uid))) {
                                    std::error_code ec;
                                    std::filesystem::copy(srcPath, "save:/", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                                    fsdevCommitDevice("save");
                                    fsdevUnmountDevice("save");
                                    popupMessage = GetText("msg_restore_ok");
                                } else {
                                    popupMessage = GetText("msg_restore_err_mount");
                                }
                            } else {
                                popupMessage = GetText("msg_restore_err_notfound");
                            }
                        } else {
                            popupMessage = GetText("msg_err_profile");
                        }
                    }
                }
            } else {
                if (kDown & HidNpadButton_B) {
                    currentState = STATE_HOME;
                }
                if (kDown & HidNpadButton_X) {
                    isGridView = !isGridView;
                    SaveConfig();
                }
                if (kDown & HidNpadButton_AnyRight) {
                    if (isGridView) {
                        if (selectedGameIndex < (int)installedGamesList.size() - 1) selectedGameIndex++;
                    } else {
                        selectedGameIndex++;
                    }
                }
                if (kDown & HidNpadButton_AnyLeft) {
                    if (isGridView) {
                        if (selectedGameIndex > 0) selectedGameIndex--;
                    } else {
                        selectedGameIndex--;
                    }
                }
                if (isGridView) {
                    if (kDown & HidNpadButton_AnyDown) {
                        if (selectedGameIndex + 4 < (int)installedGamesList.size()) selectedGameIndex += 4;
                        else selectedGameIndex = installedGamesList.size() - 1;
                    }
                    if (kDown & HidNpadButton_AnyUp) {
                        if (selectedGameIndex - 4 >= 0) selectedGameIndex -= 4;
                        else selectedGameIndex = 0;
                    }
                }
                else {
                    if (kDown & HidNpadButton_AnyDown) selectedGameIndex++;
                    if (kDown & HidNpadButton_AnyUp) selectedGameIndex--;
                }

                if (selectedGameIndex < 0) selectedGameIndex = 0;
                if (selectedGameIndex >= (int)installedGamesList.size()) {
                    if (installedGamesList.empty()) selectedGameIndex = 0;
                    else selectedGameIndex = installedGamesList.size() - 1;
                }
                
                if (currentState == STATE_GAMES && (kDown & HidNpadButton_A) && !installedGamesList.empty()) {
                    currentState = STATE_GAME_DETAILS;
                    selectedGameDetailsIndex = 0;
                }

                if (currentState == STATE_SAVES && (kDown & HidNpadButton_A) && !installedGamesList.empty()) {
                    isSavesPopupOpen = true;
                    savesPopupSelection = 0;
                    popupMessage = "";
                }
            }
        }
        else if (currentState == STATE_GAME_DETAILS) {
            std::vector<ContentItem> gameContents = GetGameContents(installedGamesList[selectedGameIndex].titleId);
            
            if (isOptionsPopupOpen) {
                if (kDown & HidNpadButton_AnyDown) optionsPopupSelection++;
                if (kDown & HidNpadButton_AnyUp) optionsPopupSelection--;
                if (optionsPopupSelection < 0) optionsPopupSelection = 0;
                if (optionsPopupSelection > 1) optionsPopupSelection = 1;
                
                if (kDown & HidNpadButton_B) {
                    isOptionsPopupOpen = false;
                } else if (kDown & HidNpadButton_A) {
                    if (optionsPopupSelection == 1) {
                        isOptionsPopupOpen = false;
                    } else if (optionsPopupSelection == 0) {
                        isOptionsPopupOpen = false;
                        isDeleteConfirmOpen = true;
                        deleteConfirmSelection = 0;
                    }
                }
            } else if (isDeleteConfirmOpen) {
                if (kDown & HidNpadButton_AnyRight) deleteConfirmSelection++;
                if (kDown & HidNpadButton_AnyLeft) deleteConfirmSelection--;
                if (deleteConfirmSelection < 0) deleteConfirmSelection = 0;
                if (deleteConfirmSelection > 1) deleteConfirmSelection = 1;
                
                if (kDown & HidNpadButton_B) {
                    isDeleteConfirmOpen = false;
                } else if (kDown & HidNpadButton_A) {
                    if (deleteConfirmSelection == 0) {
                        isDeleteConfirmOpen = false;
                    } else {
                        // Proceder con el borrado
                        if (selectedGameDetailsIndex < gameContents.size()) {
                            ContentItem& item = gameContents[selectedGameDetailsIndex];
                            if (item.type == "Aplicación") {
                                nsDeleteApplicationCompletely(item.titleId);
                                LoadGamesList();
                                isDeleteConfirmOpen = false;
                                currentState = STATE_GAMES; // Salir porque el juego ya no existe
                            } else {
                                DeleteContent(item.titleId, item.storage);
                                isDeleteConfirmOpen = false;
                            }
                        }
                    }
                }
            } else if (isDetailsPopupOpen) {
                if (kDown & (HidNpadButton_A | HidNpadButton_B)) {
                    isDetailsPopupOpen = false;
                    popupMessage = "";
                }
            } else {
                if (kDown & HidNpadButton_B) {
                    currentState = STATE_GAMES;
                }
                if (kDown & HidNpadButton_AnyDown) selectedGameDetailsIndex++;
                if (kDown & HidNpadButton_AnyUp) selectedGameDetailsIndex--;
                if (selectedGameDetailsIndex < 0) selectedGameDetailsIndex = 0;
                
                if (kDown & HidNpadButton_A) {
                    if (selectedGameDetailsIndex < (int)gameContents.size()) {
                        ContentItem& item = gameContents[selectedGameDetailsIndex];
                        if (item.type == "Aplicación") {
                            Result rc = appletRequestLaunchApplication(item.titleId, NULL);
                            if (R_FAILED(rc)) {
                                popupMessage = GetText("msg_launch_err");
                                isDetailsPopupOpen = true;
                            }
                        } else {
                            popupMessage = GetText("msg_launch_base");
                            isDetailsPopupOpen = true;
                        }
                    }
                }
                if (kDown & HidNpadButton_X) {
                    isOptionsPopupOpen = true;
                    optionsPopupSelection = 0;
                }
            }
        }
        else if (currentState == STATE_SETTINGS) {
            if (isDetailsPopupOpen) {
                if (kDown & (HidNpadButton_A | HidNpadButton_B)) {
                    isDetailsPopupOpen = false;
                    popupMessage = "";
                }
            } else {
                if (kDown & HidNpadButton_B) {
                SaveConfig();
                currentState = STATE_HOME;
                // Actualizar textos del menú
                mainButtons[0].text = GetText("menu_install");
                mainButtons[1].text = GetText("menu_download");
                mainButtons[2].text = GetText("menu_games");
                mainButtons[3].text = GetText("menu_saves");
                mainButtons[4].text = GetText("menu_homebrew");
                mainButtons[5].text = GetText("menu_explorer");
            }
            if (kDown & HidNpadButton_AnyDown) selectedSettingIndex++;
            if (kDown & HidNpadButton_AnyUp) selectedSettingIndex--;
            if (selectedSettingIndex < 0) selectedSettingIndex = 2;
            if (selectedSettingIndex > 2) selectedSettingIndex = 0;
            
            if (kDown & HidNpadButton_AnyRight && selectedSettingIndex == 1) {
                settingAccentColor++;
                if(settingAccentColor > 3) settingAccentColor = 0;
                SaveConfig();
            }
            if (kDown & HidNpadButton_AnyLeft && selectedSettingIndex == 1) {
                settingAccentColor--;
                if(settingAccentColor < 0) settingAccentColor = 3;
                SaveConfig();
            }
            
            if (kDown & HidNpadButton_A) {
                if (selectedSettingIndex == 2) {
                    u64 freed_bytes = 0;
                    int deleted = CleanupOrphanedFiles(freed_bytes);
                    
                    std::string mbStr = "";
                    if (freed_bytes > 0) {
                        mbStr = " y se han liberado " + FormatSize(freed_bytes) + " de espacio";
                    }

                    if (deleted > 0) {
                        popupMessage = "Limpieza completada! Se han eliminado archivos basura y " + std::to_string(deleted) + " tickets" + mbStr + ".";
                    } else {
                        popupMessage = "Limpieza completada! Se han eliminado los archivos basura (temporales y corruptos)" + mbStr + ".";
                    }
                    isDetailsPopupOpen = true;
                }
                if (selectedSettingIndex == 0) {
                    settingLanguage++; 
                    if(settingLanguage > 1) settingLanguage = 0;
                    SaveConfig(); 
                }
            }
            }
        }
        else {
            // Pantallas no implementadas aún, B para volver
            if (kDown & HidNpadButton_B) {
                currentState = STATE_HOME;
            }
        }

        // RENDERIZADO
        SDL_SetRenderDrawColor(renderer, colorBgDark.r, colorBgDark.g, colorBgDark.b, 255);
        SDL_RenderClear(renderer);

        if (currentState == STATE_LANGUAGE_SELECT) {
            DrawText(renderer, fontTitle, GetText("title_lang"), WINDOW_WIDTH/2, 100, colorAccent, true);
            
            std::vector<std::string> langs = {GetText("lang_es"), GetText("lang_en")};
            for (int i = 0; i < 2; i++) {
                int yPos = 250 + (i * 90);
                SDL_Color itemColor = colorTextMain;
                if (i == settingLanguage) {
                    roundedBoxRGBA(renderer, 300, yPos - 15, WINDOW_WIDTH - 300, yPos + 55, 12, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                    itemColor = {30, 30, 30, 255};
                } else {
                    roundedBoxRGBA(renderer, 300, yPos - 15, WINDOW_WIDTH - 300, yPos + 55, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
                    roundedRectangleRGBA(renderer, 300, yPos - 15, WINDOW_WIDTH - 300, yPos + 55, 12, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                }
                DrawText(renderer, fontButton, langs[i], WINDOW_WIDTH/2, yPos + 5, itemColor, true);
            }
            DrawText(renderer, fontSmall, GetText("set_help"), WINDOW_WIDTH/2, WINDOW_HEIGHT - 120, colorTextMain, true);
        }
        else if (currentState == STATE_HOME) {
            // Header
            int logoW = 130; int logoH = 130; int padding = 20;
            int titleW = 0, titleH = 0;
            if (fontTitle) TTF_SizeUTF8(fontTitle, "THE GOONIES INSTALLER", &titleW, &titleH);
            int totalHeaderWidth = (logoTexture ? logoW + padding : 0) + titleW;
            int startX = (WINDOW_WIDTH - totalHeaderWidth) / 2;
            int headerY = 50;

            if (logoTexture) {
                SDL_Rect logoRect = {startX, headerY, logoW, logoH};
                SDL_RenderCopy(renderer, logoTexture, NULL, &logoRect);
                startX += logoW + padding;
            }
            DrawText(renderer, fontTitle, "THE GOONIES OS", startX, headerY + 20, colorAccent);
            DrawText(renderer, fontButton, GetText("app_subtitle"), startX, headerY + 80, colorTextMain);
            
            // Top Right Version info
            int trX = WINDOW_WIDTH - 180;
            DrawText(renderer, fontButton, GetText("app_lang"), trX, 40, colorTextMain);
            filledCircleRGBA(renderer, trX + 60, 80, 5, 50, 255, 50, 255);
            DrawText(renderer, fontButton, "v1.3.0", trX + 75, 70, colorTextMain);
            
            DrawText(renderer, fontSmall, GetText("home_exit"), WINDOW_WIDTH - 150, WINDOW_HEIGHT - 40, colorTextMain);
            DrawText(renderer, fontSmall, "[-] " + GetText("menu_settings"), 50, WINDOW_HEIGHT - 40, colorTextMain);

            // Menu Buttons
            for (int i = 0; i < (int)mainButtons.size(); i++) {
                MenuButton b = mainButtons[i];
                SDL_Color itemColor = colorTextMain;
                
                if (i == selectedMenuButton) {
                    roundedBoxRGBA(renderer, b.rect.x, b.rect.y, b.rect.x + b.rect.w, b.rect.y + b.rect.h, 12, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                    itemColor = {30, 30, 30, 255}; // Hardcoded dark text for Accent background
                } else {
                    roundedBoxRGBA(renderer, b.rect.x, b.rect.y, b.rect.x + b.rect.w, b.rect.y + b.rect.h, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
                }
                
                if (b.icon) {
                    int iconY = b.rect.y + (b.rect.h - 40) / 2;
                    SDL_Rect iconRect = {b.rect.x + 20, iconY, 40, 40};
                    SDL_SetTextureColorMod(b.icon, itemColor.r, itemColor.g, itemColor.b);
                    SDL_RenderCopy(renderer, b.icon, NULL, &iconRect);
                }
                
                int textCenterX = b.rect.x + (b.rect.w / 2);
                DrawText(renderer, fontButton, b.text, textCenterX, b.rect.y + (b.rect.h / 2) - 15, itemColor, true);
            }
        } 
        else if (currentState == STATE_SUBMENU_INSTALL) {
            DrawText(renderer, fontTitle, GetText("menu_install"), 50, 40, colorAccent);
            std::vector<std::string> opts = {GetText("menu_submenu_mtp"), GetText("menu_submenu_usb"), GetText("menu_submenu_sd")};
            for (size_t i = 0; i < opts.size(); i++) {
                int startY = 150 + (i * 80);
                SDL_Color c = (i == selectedSubmenuInstallIndex) ? colorAccent : colorTextMain;
                if (i == selectedSubmenuInstallIndex) {
                    roundedBoxRGBA(renderer, 50, startY - 10, WINDOW_WIDTH - 50, startY + 50, 8, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 200);
                }
                DrawText(renderer, fontButton, opts[i], 80, startY, c);
            }
        }
        else if (currentState == STATE_SUBMENU_DOWNLOAD) {
            DrawText(renderer, fontTitle, GetText("menu_download"), 50, 40, colorAccent);
            std::vector<std::string> opts = {GetText("menu_store_goonies"), GetText("menu_store_torrent")};
            for (size_t i = 0; i < opts.size(); i++) {
                int startY = 150 + (i * 80);
                SDL_Color c = (i == selectedSubmenuDownloadIndex) ? colorAccent : colorTextMain;
                if (i == selectedSubmenuDownloadIndex) {
                    roundedBoxRGBA(renderer, 50, startY - 10, WINDOW_WIDTH - 50, startY + 50, 8, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 200);
                }
                DrawText(renderer, fontButton, opts[i], 80, startY, c);
            }
        }
        else if (currentState == STATE_STORE_GOONIES) {
            DrawText(renderer, fontTitle, GetText("menu_store_goonies"), 50, 40, colorAccent);
            if (isStoreLoading) {
                DrawText(renderer, fontButton, storeLoadingMessage, WINDOW_WIDTH/2, WINDOW_HEIGHT/2, colorTextMain, true);
            } else if (storeGamesList.empty()) {
                DrawText(renderer, fontButton, "No hay juegos en el catalogo.", WINDOW_WIDTH/2, WINDOW_HEIGHT/2, colorTextMain, true);
            } else {
                // Render Search/Filter Header
                std::string filterName = "Filtro: Todos";
                if (catalogFilterType == 1) filterName = "Filtro: NSP";
                else if (catalogFilterType == 2) filterName = "Filtro: NSZ";
                else if (catalogFilterType == 3) filterName = "Filtro: NRO";
                
                std::string sortName = "Orden: A-Z";
                if (catalogSortType == 1) sortName = "Orden: Z-A";
                else if (catalogSortType == 2) sortName = "Orden: Tamaño -";
                else if (catalogSortType == 3) sortName = "Orden: Tamaño +";
                else if (catalogSortType == 4) sortName = "Orden: Novedades";
                
                std::string searchName = catalogSearchQuery.empty() ? "" : "Busqueda: " + catalogSearchQuery;
                
                DrawText(renderer, fontSmall, filterName + "  |  " + sortName, 60, 95, colorAccent);
                if (!searchName.empty()) {
                    DrawText(renderer, fontSmall, searchName, 400, 95, colorTextMain);
                }
                
                if (filteredStoreGamesList.empty()) {
                    DrawText(renderer, fontButton, "Ningun juego coincide con el filtro/busqueda.", WINDOW_WIDTH/2 - 200, WINDOW_HEIGHT/2, colorTextMain, true);
                } else {
                    int listY = 135;
                    int maxVisible = 10;
                    int startIdx = selectedStoreGameIndex - (maxVisible/2);
                    if (startIdx < 0) startIdx = 0;
                    if (startIdx + maxVisible > (int)filteredStoreGamesList.size()) startIdx = filteredStoreGamesList.size() - maxVisible;
                    if (startIdx < 0) startIdx = 0;
                    
                    // Draw Left Side: Game List (Width: 650)
                    for (int i = 0; i < maxVisible && (startIdx + i) < (int)filteredStoreGamesList.size(); i++) {
                        int idx = startIdx + i;
                        int yPos = listY + (i * 48);
                        SDL_Color c = (idx == selectedStoreGameIndex) ? colorAccent : colorTextMain;
                        if (idx == selectedStoreGameIndex) {
                            roundedBoxRGBA(renderer, 50, yPos - 5, 700, yPos + 38, 4, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 200);
                        }
                        std::string txt = filteredStoreGamesList[idx].title;
                        if (txt.length() > 45) txt = txt.substr(0, 42) + "...";
                        DrawText(renderer, fontButton, txt, 60, yPos - 2, c);
                    }
                    
                    // Draw Right Side: Game Details & Cover Art (X: 750)
                    CatalogGame selectedGame = filteredStoreGamesList[selectedStoreGameIndex];
                    int detailsX = 745;
                    
                    // Draw Icon Box (Reduced slightly to 180 to fit everything cleanly)
                    int iconW = 180;
                    int iconH = 180;
                    if (currentCatalogIcon) {
                        SDL_Rect dest = {detailsX, 130, iconW, iconH};
                        SDL_RenderCopy(renderer, currentCatalogIcon, NULL, &dest);
                        roundedRectangleRGBA(renderer, dest.x, dest.y, dest.x + dest.w, dest.y + dest.h, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                    } else {
                        roundedBoxRGBA(renderer, detailsX, 130, detailsX + iconW, 130 + iconH, 8, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
                        DrawText(renderer, fontSmall, isCatalogIconLoading ? "Cargando..." : "SIN PORTADA", detailsX + iconW/2, 130 + iconH/2 - 10, colorTextMain, true);
                    }
                    
                    // Title and Metadata
                    int infoY = 325;
                    std::string fullTitle = selectedGame.title;
                    if (fullTitle.length() > 30) {
                        DrawText(renderer, fontButton, fullTitle.substr(0, 30), detailsX, infoY, colorAccent);
                        DrawText(renderer, fontButton, fullTitle.substr(30, 30) + (fullTitle.length() > 60 ? "..." : ""), detailsX, infoY + 28, colorAccent);
                        infoY += 28;
                    } else {
                        DrawText(renderer, fontButton, fullTitle, detailsX, infoY, colorAccent);
                    }
                    
                    int textGap = 24;
                    DrawText(renderer, fontSmall, "Tamaño: " + (selectedGame.size.empty() ? "Desconocido" : selectedGame.size), detailsX, infoY + 36, colorTextMain);
                    
                    if (!selectedGame.developer.empty()) {
                        DrawText(renderer, fontSmall, "Desarrollador: " + selectedGame.developer, detailsX, infoY + 36 + textGap, colorTextMain);
                        infoY += textGap;
                    }
                    
                    if (!selectedGame.genre.empty()) {
                        std::string g = selectedGame.genre;
                        if (g.length() > 35) g = g.substr(0, 32) + "...";
                        DrawText(renderer, fontSmall, "Genero: " + g, detailsX, infoY + 36 + textGap, colorTextMain);
                        infoY += textGap;
                    }
                    
                    if (!selectedGame.year.empty()) {
                        DrawText(renderer, fontSmall, "Año: " + selectedGame.year, detailsX, infoY + 36 + textGap, colorTextMain);
                        infoY += textGap;
                    }

                    if (!selectedGame.interface_lang.empty()) {
                        std::string lang = selectedGame.interface_lang;
                        if (lang.length() > 35) lang = lang.substr(0, 32) + "...";
                        DrawText(renderer, fontSmall, "Idioma: " + lang, detailsX, infoY + 36 + textGap, colorTextMain);
                        infoY += textGap;
                    }
                    
                    // Description
                    if (!selectedGame.description.empty()) {
                        std::string desc = selectedGame.description;
                        if (desc.length() > 100) desc = desc.substr(0, 97) + "...";
                        DrawText(renderer, fontSmall, "Sinopsis:", detailsX, infoY + 42 + textGap, colorAccent);
                        int descY = infoY + 42 + textGap + 24;
                        size_t lineLength = 36;
                        for (size_t offset = 0; offset < desc.length(); offset += lineLength) {
                            std::string line = desc.substr(offset, lineLength);
                            DrawText(renderer, fontSmall, line, detailsX, descY, colorTextMain);
                            descY += 20;
                        }
                    }
                }
            }
        }
        else if (currentState == STATE_TORRENT_DOWNLOAD) {
            DrawText(renderer, fontTitle, "Descargando", 50, 40, colorAccent);
            DrawText(renderer, fontSmall, currentTorrentName, 50, 110, colorTextMain); // Fixed overlapping (90 -> 110)
            
            if (currentTorrentId == -1) {
                DrawText(renderer, fontButton, "Iniciando descarga...", WINDOW_WIDTH/2, WINDOW_HEIGHT/2, colorTextMain, true);
            } else if (currentTorrentId == -2) {
                DrawText(renderer, fontButton, "Error al iniciar descarga.", WINDOW_WIDTH/2, WINDOW_HEIGHT/2, {255, 100, 100, 255}, true);
                DrawText(renderer, fontSmall, "Pulsa B para volver.", WINDOW_WIDTH/2, WINDOW_HEIGHT/2 + 40, colorTextMain, true);
            } else {
                TorrentStatus ts = TorrentEngine::GetStatus(currentTorrentId);
                
                // Stage text
                DrawText(renderer, fontButton, ts.stage, WINDOW_WIDTH/2, WINDOW_HEIGHT/2 - 80, colorTextMain, true);
                
                // Progress bar
                int barW = 600;
                int barH = 30;
                int barX = (WINDOW_WIDTH - barW) / 2;
                int barY = WINDOW_HEIGHT / 2 - barH / 2;
                
                roundedRectangleRGBA(renderer, barX, barY, barX + barW, barY + barH, 8, colorTextMain.r, colorTextMain.g, colorTextMain.b, 255);
                int progW = (int)(barW * ts.progress);
                if (progW > 0) {
                    roundedBoxRGBA(renderer, barX, barY, barX + progW, barY + barH, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                }
                
                // Percentage
                std::string progStr = std::to_string((int)(ts.progress * 100)) + "%";
                DrawText(renderer, fontButton, progStr, WINDOW_WIDTH/2, barY + 45, colorTextMain, true);
                
                // Speed + Peers info
                {
                    float speedMB = (float)ts.speedBps / (1024.0f * 1024.0f);
                    float dlMB = (float)ts.downloadedBytes / (1024.0f * 1024.0f);
                    
                    std::stringstream infoSS;
                    infoSS << std::fixed << std::setprecision(1) << dlMB << " / ";
                    if (ts.totalBytes > 0) {
                        float totalMB = (float)ts.totalBytes / (1024.0f * 1024.0f);
                        infoSS << totalMB << " MB";
                    } else {
                        infoSS << (currentTorrentCatalogSize.empty() ? "..." : currentTorrentCatalogSize);
                    }
                    infoSS << "  |  " << std::setprecision(2) << speedMB << " MB/s  |  Peers: " << ts.numActivePeers << "/" << ts.numPeers;
                    
                    // Display queue size if any games are queued
                    if (!torrentQueue.empty()) {
                        infoSS << "  |  Cola: " << torrentQueue.size();
                    }
                    
                    DrawText(renderer, fontSmall, infoSS.str(), WINDOW_WIDTH/2, barY + 80, colorTextMain, true);
                }
                
                if (ts.finished && !ts.error) {
                    DrawText(renderer, fontButton, "Descarga completada! Pulsa X para ver archivos.", WINDOW_WIDTH/2, barY + 120, colorAccent, true);
                } else if (ts.error) {
                    DrawText(renderer, fontButton, "Error: " + ts.errorMessage, WINDOW_WIDTH/2, barY + 120, {255, 100, 100, 255}, true);
                }
            }
        }
        else if (currentState == STATE_GAME_DETAILS) {
            InstalledGame& game = installedGamesList[selectedGameIndex];
            
            // Title & Dev
            DrawText(renderer, fontTitle, game.name, 50, 40, colorTextMain);
            DrawText(renderer, fontSmall, "by Qu", WINDOW_WIDTH - 200, 50, colorTextMain);
            
            // Left Panel (Icon + Metadatos)
            if (game.icon) {
                SDL_Rect dest = {50, 130, 256, 256};
                SDL_RenderCopy(renderer, game.icon, NULL, &dest);
                roundedRectangleRGBA(renderer, dest.x, dest.y, dest.x + dest.w, dest.y + dest.h, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
            } else {
                roundedBoxRGBA(renderer, 50, 130, 306, 386, 8, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
                DrawText(renderer, fontButton, "NO ICON", 178, 248, colorTextMain, true);
            }
            
            std::vector<ContentItem> gameContents = GetGameContents(game.titleId);
            
            int metaY = 410;
            DrawText(renderer, fontButton, "Contenido: " + std::to_string(gameContents.size()), 50, metaY, colorTextMain);
            // Ticket and Key Generation removed
            
            // Try to get required system version
            std::string reqSysVer = "0.0.0";
            NcmContentMetaDatabase db;
            if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, NcmStorageId_BuiltInUser))) {
                NcmContentMetaKey key;
                if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, game.titleId))) {
                    u32 reqV = 0;
                    if (R_SUCCEEDED(ncmContentMetaDatabaseGetRequiredSystemVersion(&db, &reqV, &key))) {
                        reqSysVer = std::to_string((reqV >> 26) & 0x3F) + "." + std::to_string((reqV >> 20) & 0x3F) + "." + std::to_string((reqV >> 16) & 0xF);
                    }
                }
                ncmContentMetaDatabaseClose(&db);
            }
            if (reqSysVer == "0.0.0" || reqSysVer == "0.0") {
                if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard))) {
                    NcmContentMetaKey key;
                    if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, game.titleId))) {
                        u32 reqV = 0;
                        if (R_SUCCEEDED(ncmContentMetaDatabaseGetRequiredSystemVersion(&db, &reqV, &key))) {
                            reqSysVer = std::to_string((reqV >> 26) & 0x3F) + "." + std::to_string((reqV >> 20) & 0x3F) + "." + std::to_string((reqV >> 16) & 0xF);
                        }
                    }
                    ncmContentMetaDatabaseClose(&db);
                }
            }
            
            DrawText(renderer, fontButton, "Versión mínima del sistema: " + reqSysVer, 50, metaY + 30, colorTextMain);
            
            // Display Version and System Version via NACP
            std::string displayVer = "Unknown";
            NsApplicationControlData* controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
            if (controlData) {
                u64 actualSize = 0;
                if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, game.titleId, controlData, sizeof(NsApplicationControlData), &actualSize))) {
                    displayVer = controlData->nacp.display_version;
                    
                    // Actually getting real system version from bcat or fallback
                    if (reqSysVer == "0.0.0" || reqSysVer == "0.0") {
                        // Sometimes reqSysVer from ncm is empty, try to show at least something, 
                        // though displayVer is what users usually want.
                    }
                }
                free(controlData);
            }
            DrawText(renderer, fontButton, "Versión mostrada: " + displayVer, 50, metaY + 60, colorTextMain);
            
            // Right Panel (Contents List)
            int rX = 350;
            int rY = 130;
            int rowHeight = 70;
            
            if (gameContents.empty()) {
                roundedRectangleRGBA(renderer, rX, rY, WINDOW_WIDTH - 50, rY + 60, 4, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                DrawText(renderer, fontButton, "Aplicación", rX + 20, rY + 15, colorAccent);
                DrawText(renderer, fontButton, GetTitleIdHex(game.titleId), rX + 200, rY + 15, colorTextMain);
                DrawText(renderer, fontButton, "v0 (0)", rX + 450, rY + 15, colorTextMain);
                DrawText(renderer, fontSmall, "Unknown MB", WINDOW_WIDTH - 200, rY + 10, colorTextMain);
                DrawText(renderer, fontSmall, "Unknown Storage", WINDOW_WIDTH - 200, rY + 30, colorTextMain);
            } else {
                if (selectedGameDetailsIndex < 0) selectedGameDetailsIndex = 0;
                if (selectedGameDetailsIndex >= (int)gameContents.size()) selectedGameDetailsIndex = (int)gameContents.size() - 1;
                
                for (int i = 0; i < (int)gameContents.size(); i++) {
                    int cY = rY + (i * rowHeight);
                    
                    if (i == selectedGameDetailsIndex) {
                        roundedRectangleRGBA(renderer, rX, cY, WINDOW_WIDTH - 50, cY + 60, 4, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                        DrawText(renderer, fontButton, gameContents[i].type, rX + 20, cY + 15, colorAccent);
                    } else {
                        DrawText(renderer, fontButton, gameContents[i].type, rX + 20, cY + 15, colorTextMain);
                    }
                    
                    DrawText(renderer, fontButton, GetTitleIdHex(gameContents[i].titleId), rX + 200, cY + 15, colorTextMain);
                    
                    std::stringstream verStream;
                    verStream << "v" << (gameContents[i].version / 65536) << " (" << gameContents[i].version << ")";
                    DrawText(renderer, fontButton, verStream.str(), rX + 450, cY + 15, colorTextMain);
                    
                    DrawText(renderer, fontSmall, FormatSize(gameContents[i].size), WINDOW_WIDTH - 200, cY + 10, colorTextMain);
                    DrawText(renderer, fontSmall, gameContents[i].storage, WINDOW_WIDTH - 200, cY + 30, colorTextMain);
                    
                    if (i < gameContents.size() - 1) {
                        lineRGBA(renderer, rX, cY + 60, WINDOW_WIDTH - 50, cY + 60, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                    }
                }
            }
            
            // Popup de Detalles
            if (isDetailsPopupOpen) {
                int pW = 550;
                int pH = 250;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 150);
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                DrawText(renderer, fontTitle, GetText("popup_notice"), WINDOW_WIDTH/2, pY + 30, colorAccent, true);
                DrawText(renderer, fontButton, popupMessage, WINDOW_WIDTH/2, pY + 70, colorTextMain, true, pW - 40);
                roundedBoxRGBA(renderer, pX + 50, pY + 190, pX + pW - 50, pY + 230, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                DrawText(renderer, fontButton, GetText("popup_close"), WINDOW_WIDTH/2, pY + 200, {30,30,30,255}, true);
            }
            // Popup de Opciones
            if (isOptionsPopupOpen) {
                int pW = 400;
                int pH = 250;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 150);
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                DrawText(renderer, fontTitle, "Opciones", WINDOW_WIDTH/2, pY + 30, colorAccent, true);
                
                std::vector<std::string> opts = {"Desinstalar", "Cancelar"};
                for (int i = 0; i < 2; i++) {
                    int bY = pY + 100 + (i * 60);
                    if (i == optionsPopupSelection) {
                        roundedBoxRGBA(renderer, pX + 50, bY - 10, pX + pW - 50, bY + 40, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                        DrawText(renderer, fontButton, opts[i], WINDOW_WIDTH/2, bY, {30,30,30,255}, true);
                    } else {
                        roundedBoxRGBA(renderer, pX + 50, bY - 10, pX + pW - 50, bY + 40, 8, 80, 80, 80, 255);
                        DrawText(renderer, fontButton, opts[i], WINDOW_WIDTH/2, bY, colorTextMain, true);
                    }
                }
            }
            // Popup Confirmar Borrado
            if (isDeleteConfirmOpen) {
                int pW = 500;
                int pH = 250;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 200);
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, 255, 50, 50, 255); // Borde rojo
                
                DrawText(renderer, fontTitle, "¡Advertencia!", WINDOW_WIDTH/2, pY + 30, {255,50,50,255}, true);
                DrawText(renderer, fontButton, "¿Seguro que quieres borrarlo?", WINDOW_WIDTH/2, pY + 100, colorTextMain, true);
                DrawText(renderer, fontSmall, "Esta acción no se puede deshacer.", WINDOW_WIDTH/2, pY + 130, colorTextMain, true);
                
                std::vector<std::string> opts = {"No", "Sí, borrar"};
                for (int i = 0; i < 2; i++) {
                    int bX = pX + 50 + (i * 200);
                    if (i == deleteConfirmSelection) {
                        roundedBoxRGBA(renderer, bX, pY + 170, bX + 180, pY + 220, 8, 255, 50, 50, 255);
                        DrawText(renderer, fontButton, opts[i], bX + 90, pY + 180, {255,255,255,255}, true);
                    } else {
                        roundedBoxRGBA(renderer, bX, pY + 170, bX + 180, pY + 220, 8, 80, 80, 80, 255);
                        DrawText(renderer, fontButton, opts[i], bX + 90, pY + 180, colorTextMain, true);
                    }
                }
            }
        } 
        else if (currentState == STATE_EXPLORER || currentState == STATE_USB) {
            DrawText(renderer, fontTitle, currentState == STATE_USB ? GetText("menu_usb") : GetText("menu_explorer"), 50, 40, colorAccent);
            DrawText(renderer, fontButton, "Ruta: " + explorerPath, 50, 120, colorTextMain);
            if (currentState == STATE_EXPLORER) {
                std::string switchText = (explorerPath.find("sdmc") == 0) ? "[X] Cambiar a USB" : "[X] Cambiar a SD";
                DrawText(renderer, fontSmall, switchText, WINDOW_WIDTH - 200, 120, colorAccent);
            }

            int startIdx = selectedFileIndex - 5;
            if (startIdx < 0) startIdx = 0;
            
            for (int i = 0; i < 10 && (startIdx + i) < (int)explorerList.size(); i++) {
                int itemIdx = startIdx + i;
                int yPos = 180 + (i * 45);
                
                SDL_Color itemColor = colorTextMain;
                if (itemIdx == selectedFileIndex) {
                    roundedBoxRGBA(renderer, 40, yPos - 5, WINDOW_WIDTH - 40, yPos + 35, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                    itemColor = {30, 30, 30, 255};
                }
                
                std::string prefix = explorerList[itemIdx].isDir ? "[DIR] " : "[FILE] ";
                std::string checkbox = "";
                if (currentState == STATE_USB && !explorerList[itemIdx].isDir && explorerList[itemIdx].name != "..") {
                    checkbox = explorerList[itemIdx].isSelected ? "[X] " : "[ ] ";
                }
                DrawText(renderer, fontButton, checkbox + prefix + explorerList[itemIdx].name, 60, yPos, itemColor);
            }
            
            // Si hay un error al iniciar desde explorer
            if (isDetailsPopupOpen && (currentState == STATE_EXPLORER || currentState == STATE_USB)) {
                int pW = 400; int pH = 200;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 150);
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                DrawText(renderer, fontTitle, "Aviso", WINDOW_WIDTH/2, pY + 30, colorAccent, true);
                DrawText(renderer, fontButton, popupMessage, WINDOW_WIDTH/2, pY + 100, colorTextMain, true, pW - 40);
                roundedBoxRGBA(renderer, pX + 50, pY + 140, pX + pW - 50, pY + 180, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                DrawText(renderer, fontButton, "[A/B] Cerrar", WINDOW_WIDTH/2, pY + 150, {30,30,30,255}, true);
            }
        }
        else if (currentState == STATE_HOMEBREW) {
            DrawText(renderer, fontTitle, GetText("menu_homebrew"), 50, 40, colorAccent);
            DrawText(renderer, fontButton, "Ruta: " + homebrewPath, 50, 120, colorTextMain);

            int startIdx = selectedHomebrewIndex - 5;
            if (startIdx < 0) startIdx = 0;
            
            for (int i = 0; i < 10 && (startIdx + i) < (int)homebrewList.size(); i++) {
                int itemIdx = startIdx + i;
                int yPos = 180 + (i * 45);
                
                SDL_Color itemColor = colorTextMain;
                if (itemIdx == selectedHomebrewIndex) {
                    roundedBoxRGBA(renderer, 40, yPos - 5, WINDOW_WIDTH - 40, yPos + 35, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                    itemColor = {30, 30, 30, 255};
                }
                
                std::string prefix = homebrewList[itemIdx].isDir ? "[DIR] " : "[NRO] ";
                DrawText(renderer, fontButton, prefix + homebrewList[itemIdx].name, 60, yPos, itemColor);
            }
            
            if (isHomebrewPopupOpen) {
                int pW = 500; int pH = 350;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 150);
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                
                if (popupMessage != "") {
                    DrawText(renderer, fontButton, popupMessage, WINDOW_WIDTH/2, pY + 140, colorAccent, true, pW - 40);
                    roundedBoxRGBA(renderer, pX + 150, pY + 220, pX + 350, pY + 270, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                    DrawText(renderer, fontButton, "Aceptar", WINDOW_WIDTH/2, pY + 230, {30, 30, 30, 255}, true);
                } else {
                    DrawText(renderer, fontButton, "OPCIONES DE HOMEBREW", WINDOW_WIDTH/2, pY + 30, colorAccent, true);
                    DrawText(renderer, fontSmall, homebrewList[selectedHomebrewIndex].name, WINDOW_WIDTH/2, pY + 70, colorTextMain, true);
                    
                    std::vector<std::string> opts = {"1. Lanzar Aplicacion", "2. Crear Forwarder", "3. Cancelar"};
                    for (int i = 0; i < 3; i++) {
                        SDL_Color bColor = (homebrewPopupSelection == i) ? colorAccent : colorBgDark;
                        SDL_Color bText = (homebrewPopupSelection == i) ? SDL_Color{30, 30, 30, 255} : colorTextMain;
                        int by = pY + 110 + (i * 70);
                        roundedBoxRGBA(renderer, pX + 100, by, pX + 400, by + 50, 8, bColor.r, bColor.g, bColor.b, 255);
                        roundedRectangleRGBA(renderer, pX + 100, by, pX + 400, by + 50, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                        DrawText(renderer, fontButton, opts[i], WINDOW_WIDTH/2, by + 10, bText, true);
                    }
                }
            }
        }
        else if (currentState == STATE_INSTALLING) {
            DrawText(renderer, fontTitle, "Instalando...", 50, 40, colorAccent);
            size_t lastSlash = g_installerFileName.find_last_of('/');
            std::string shortName = (lastSlash != std::string::npos) ? g_installerFileName.substr(lastSlash + 1) : g_installerFileName;
            DrawText(renderer, fontButton, shortName, 50, 100, colorTextMain);
            
            if (g_installerCore) {
                u64 written = g_installerCore->GetBytesWritten();
                u64 total = g_installerTotalSize;
                
                int percent = 0;
                if (total > 0) percent = (written * 100) / total;
                if (percent > 100) percent = 100;
                
                // Draw Progress Bar
                int barX = 50;
                int barY = 200;
                int barW = WINDOW_WIDTH - 100;
                int barH = 40;
                
                roundedBoxRGBA(renderer, barX, barY, barX + barW, barY + barH, 8, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, barX, barY, barX + barW, barY + barH, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                
                if (percent > 0) {
                    int fillW = (barW * percent) / 100;
                    roundedBoxRGBA(renderer, barX, barY, barX + fillW, barY + barH, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                }
                
                std::string progressTxt = FormatSize(written) + " / " + FormatSize(total) + " (" + std::to_string(percent) + "%)";
                DrawText(renderer, fontButton, progressTxt, WINDOW_WIDTH/2, barY - 40, colorTextMain, true);
                
                if (g_installerCore->HasError()) {
                    if (g_installerCore->GetErrorCode() == Result_YatiFirmwareTooLow) {
                        DrawText(renderer, fontTitle, "Tu firmware es antiguo. Debes actualizarlo.", WINDOW_WIDTH/2, barY + 100, {255, 50, 50, 255}, true);
                    } else {
                        DrawText(renderer, fontTitle, "Error de instalacion!", WINDOW_WIDTH/2, barY + 100, {255, 50, 50, 255}, true);
                    }
                    DrawText(renderer, fontButton, "Pulsa A o B para salir", WINDOW_WIDTH/2, barY + 160, colorTextMain, true);
                } else if (g_installerCore->IsFinished()) {
                    DrawText(renderer, fontTitle, "¡Instalación completada!", WINDOW_WIDTH/2, barY + 100, {50, 255, 50, 255}, true);
                    DrawText(renderer, fontButton, "Pulsa A o B para salir", WINDOW_WIDTH/2, barY + 160, colorTextMain, true);
                } else {
                    DrawText(renderer, fontSmall, "[B] Cancelar instalacion", 50, WINDOW_HEIGHT - 60, colorTextMain);
                }
            }
        }
        else if (currentState == STATE_GAMES || currentState == STATE_SAVES) {
            DrawText(renderer, fontTitle, currentState == STATE_GAMES ? GetText("menu_games") : GetText("title_saves"), 50, 40, colorAccent);
            
            if (installedGamesList.empty()) {
                DrawText(renderer, fontButton, GetText("no_games"), 50, 120, colorTextMain);
            } else {
                if (!isGridView) {
                    // MODO LISTA
                    int startIdx = selectedGameIndex - 3;
                    if (startIdx < 0) startIdx = 0;

                    for (int i = 0; i < 5 && (startIdx + i) < (int)installedGamesList.size(); i++) {
                        int idx = startIdx + i;
                        InstalledGame& game = installedGamesList[idx];
                        int yPos = 130 + (i * 100);

                        // Lazy load icon & name
                        if (game.icon == nullptr && game.name == GetText("unknown_game")) {
                            NsApplicationControlData* ctrlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
                            u64 outSize = 0;
                            if (ctrlData && R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, game.titleId, ctrlData, sizeof(NsApplicationControlData), &outSize))) {
                                NacpLanguageEntry* lang = nullptr;
                                nacpGetLanguageEntry(&ctrlData->nacp, &lang);
                                if (lang && lang->name[0] != '\0') game.name = lang->name;
                                
                                SDL_RWops* rw = SDL_RWFromMem(ctrlData->icon, sizeof(ctrlData->icon));
                                if (rw) {
                                    SDL_Surface* surf = IMG_Load_RW(rw, 1);
                                    if (surf) { game.icon = SDL_CreateTextureFromSurface(renderer, surf); SDL_FreeSurface(surf); }
                                }
                            }
                            if (ctrlData) free(ctrlData);
                            if (game.name == GetText("unknown_game")) game.name = "ID: " + GetTitleIdHex(game.titleId);
                        }

                        SDL_Color itemColor = colorTextMain;
                        if (idx == selectedGameIndex) {
                            roundedBoxRGBA(renderer, 40, yPos, WINDOW_WIDTH - 40, yPos + 90, 12, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                            itemColor = {30, 30, 30, 255};
                        } else {
                            roundedBoxRGBA(renderer, 40, yPos, WINDOW_WIDTH - 40, yPos + 90, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
                        }

                        if (game.icon) {
                            SDL_Rect iconRect = {50, yPos + 5, 80, 80};
                            SDL_RenderCopy(renderer, game.icon, NULL, &iconRect);
                        }
                        DrawText(renderer, fontButton, game.name, 150, yPos + 30, itemColor);
                    }
                } else {
                    // MODO CUADRÍCULA
                    int startRow = (selectedGameIndex / 5) - 1;
                    if (startRow < 0) startRow = 0;
                    
                    for (int i = 0; i < 10; i++) { // 2 filas de 5
                        int idx = (startRow * 5) + i;
                        if (idx >= (int)installedGamesList.size()) break;
                        
                        InstalledGame& game = installedGamesList[idx];
                        int col = i % 5;
                        int row = i / 5;
                        
                        int xPos = 46 + col * 237;
                        int yPos = 145 + row * 240;

                        // Lazy load icon & name
                        if (game.icon == nullptr && game.name == GetText("unknown_game")) {
                            NsApplicationControlData* ctrlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
                            u64 outSize = 0;
                            if (ctrlData && R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, game.titleId, ctrlData, sizeof(NsApplicationControlData), &outSize))) {
                                NacpLanguageEntry* lang = nullptr;
                                nacpGetLanguageEntry(&ctrlData->nacp, &lang);
                                if (lang && lang->name[0] != '\0') game.name = lang->name;
                                
                                SDL_RWops* rw = SDL_RWFromMem(ctrlData->icon, sizeof(ctrlData->icon));
                                if (rw) {
                                    SDL_Surface* surf = IMG_Load_RW(rw, 1);
                                    if (surf) { game.icon = SDL_CreateTextureFromSurface(renderer, surf); SDL_FreeSurface(surf); }
                                }
                            }
                            if (ctrlData) free(ctrlData);
                            if (game.name == GetText("unknown_game")) game.name = "ID: " + GetTitleIdHex(game.titleId);
                        }

                        if (idx == selectedGameIndex) {
                            roundedBoxRGBA(renderer, xPos - 5, yPos - 5, xPos + 195, yPos + 195, 12, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                        } else {
                            roundedBoxRGBA(renderer, xPos - 5, yPos - 5, xPos + 195, yPos + 195, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
                        }

                        if (game.icon) {
                            SDL_Rect iconRect = {xPos, yPos, 190, 190};
                            SDL_RenderCopy(renderer, game.icon, NULL, &iconRect);
                        }
                        
                        // Text trimming (opcional, para evitar solapamientos, dibujamos truncado o solo mostramos si está seleccionado)
                        if (idx == selectedGameIndex) {
                            std::string displayTxt = game.name;
                            if (displayTxt.length() > 20) displayTxt = displayTxt.substr(0, 18) + "..";
                            DrawText(renderer, fontSmall, displayTxt, xPos + 95, yPos + 210, colorTextMain, true);
                        }
                    }
                }
            }

            // Draw Popup if open
            if (isSavesPopupOpen) {
                // Dim background
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
                SDL_Rect fullRect = {0,0,WINDOW_WIDTH,WINDOW_HEIGHT};
                SDL_RenderFillRect(renderer, &fullRect);
                
                // Popup Box
                int pW = 500; int pH = 350;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                
                if (popupMessage != "") {
                    DrawText(renderer, fontButton, popupMessage, WINDOW_WIDTH/2, pY + 140, colorAccent, true, pW - 40);
                    
                    // Solo botón Aceptar
                    roundedBoxRGBA(renderer, pX + 150, pY + 220, pX + 350, pY + 270, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                    DrawText(renderer, fontButton, "Aceptar", WINDOW_WIDTH/2, pY + 230, {30, 30, 30, 255}, true);
                } else {
                    DrawText(renderer, fontButton, GetText("popup_title"), WINDOW_WIDTH/2, pY + 30, colorAccent, true);
                    std::string savePathText = GetText("popup_sd_path") + GetTitleIdHex(installedGamesList[selectedGameIndex].titleId);
                    DrawText(renderer, fontSmall, savePathText, WINDOW_WIDTH/2, pY + 70, colorTextMain, true);
                    
                    // Botones
                    SDL_Color bColor = (savesPopupSelection == 0) ? colorAccent : colorBgDark;
                    SDL_Color bText = (savesPopupSelection == 0) ? SDL_Color{30, 30, 30, 255} : colorTextMain;
                    roundedBoxRGBA(renderer, pX + 100, pY + 110, pX + 400, pY + 160, 8, bColor.r, bColor.g, bColor.b, 255);
                    roundedRectangleRGBA(renderer, pX + 100, pY + 110, pX + 400, pY + 160, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                    DrawText(renderer, fontButton, GetText("popup_opt_backup"), WINDOW_WIDTH/2, pY + 120, bText, true);

                    SDL_Color iColor = (savesPopupSelection == 1) ? colorAccent : colorBgDark;
                    SDL_Color iText = (savesPopupSelection == 1) ? SDL_Color{30, 30, 30, 255} : colorTextMain;
                    roundedBoxRGBA(renderer, pX + 100, pY + 180, pX + 400, pY + 230, 8, iColor.r, iColor.g, iColor.b, 255);
                    roundedRectangleRGBA(renderer, pX + 100, pY + 180, pX + 400, pY + 230, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                    DrawText(renderer, fontButton, GetText("popup_opt_restore"), WINDOW_WIDTH/2, pY + 190, iText, true);

                    SDL_Color cColor = (savesPopupSelection == 2) ? colorAccent : colorBgDark;
                    SDL_Color cText = (savesPopupSelection == 2) ? SDL_Color{30, 30, 30, 255} : colorTextMain;
                    roundedBoxRGBA(renderer, pX + 100, pY + 250, pX + 400, pY + 300, 8, cColor.r, cColor.g, cColor.b, 255);
                    roundedRectangleRGBA(renderer, pX + 100, pY + 250, pX + 400, pY + 300, 8, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                    DrawText(renderer, fontButton, GetText("popup_opt_cancel"), WINDOW_WIDTH/2, pY + 260, cText, true);
                }
            }
        }
        else if (currentState == STATE_SETTINGS) {
            DrawText(renderer, fontTitle, GetText("title_settings"), 50, 40, colorAccent);

            int pW = 570;
            int pH = 230;
            int leftX = 50;
            int rightX = 660;
            int topY = 120;
            
            // Left Panel
            roundedBoxRGBA(renderer, leftX, topY, leftX + pW, topY + pH, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
            roundedRectangleRGBA(renderer, leftX, topY, leftX + pW, topY + pH, 12, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
            DrawText(renderer, fontButton, GetText("sys_info"), leftX + 20, topY + 20, colorAccent);
            
            SetSysFirmwareVersion fw;
            std::string fwStr = "16.0.3";
            if (R_SUCCEEDED(setsysInitialize())) {
                if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
                    fwStr = std::string(fw.display_version);
                }
                setsysExit();
            }
            DrawText(renderer, fontSmall, GetText("sys_fw"), leftX + 20, topY + 65, colorTextMain);
            DrawText(renderer, fontSmall, fwStr, leftX + 250, topY + 65, {200,200,200,255});
            
            DrawText(renderer, fontSmall, GetText("sys_app_ver"), leftX + 20, topY + 105, colorTextMain);
            DrawText(renderer, fontSmall, "The Goonies OS v1.2.1", leftX + 250, topY + 105, {200,200,200,255});
            
            DrawText(renderer, fontSmall, GetText("sys_sd"), leftX + 20, topY + 145, colorTextMain);
            
            struct statvfs stat;
            if (statvfs("sdmc:/", &stat) == 0) {
                u64 total = (u64)stat.f_blocks * stat.f_frsize;
                u64 free = (u64)stat.f_bfree * stat.f_frsize;
                u64 used = total - free;
                float freeGB = (float)free / (1024*1024*1024);
                float totalGB = (float)total / (1024*1024*1024);
                
                int barY = topY + 175;
                int barW = pW - 40;
                roundedBoxRGBA(renderer, leftX + 20, barY, leftX + 20 + barW, barY + 10, 5, 80, 80, 80, 255);
                float pct = (float)used / (float)total;
                if (pct > 1.0f) pct = 1.0f;
                int fillW = (int)(barW * pct);
                roundedBoxRGBA(renderer, leftX + 20, barY, leftX + 20 + fillW, barY + 10, 5, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                
                std::stringstream ss;
                ss << std::fixed << std::setprecision(1) << (totalGB - freeGB) << " GB / " << totalGB << " GB (" << (int)(pct * 100) << GetText("sys_used");
                DrawText(renderer, fontSmall, ss.str(), leftX + 20, barY + 20, {150,150,150,255});
            }

            // Right Panel
            roundedBoxRGBA(renderer, rightX, topY, rightX + pW, topY + pH, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
            roundedRectangleRGBA(renderer, rightX, topY, rightX + pW, topY + pH, 12, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
            DrawText(renderer, fontButton, GetText("sys_prefs"), rightX + 20, topY + 20, colorAccent);
            
            DrawText(renderer, fontSmall, GetText("sys_lang"), rightX + 20, topY + 65, colorTextMain);
            
            if (selectedSettingIndex == 0) {
                roundedBoxRGBA(renderer, rightX + 20, topY + 95, rightX + pW - 20, topY + 125, 4, colorAccent.r, colorAccent.g, colorAccent.b, 255);
            } else {
                roundedBoxRGBA(renderer, rightX + 20, topY + 95, rightX + pW - 20, topY + 125, 4, 30, 30, 30, 255);
            }
            
            std::string langStr = settingLanguage == 0 ? "Castellano" : "English";
            DrawText(renderer, fontSmall, langStr, rightX + 30, topY + 100, selectedSettingIndex == 0 ? SDL_Color{30,30,30,255} : colorTextMain);
            
            DrawText(renderer, fontSmall, GetText("sys_color"), rightX + 20, topY + 145, colorTextMain);
            
            if (selectedSettingIndex == 1) {
                roundedBoxRGBA(renderer, rightX + 15, topY + 175, rightX + 225, topY + 220, 8, 80, 80, 80, 255);
            }
            
            SDL_Color c1 = {255, 153, 0, 255};
            SDL_Color c2 = {0, 191, 255, 255};
            SDL_Color c3 = {50, 205, 50, 255};
            SDL_Color c4 = {153, 50, 204, 255};
            
            filledCircleRGBA(renderer, rightX + 40, topY + 198, 18, c1.r, c1.g, c1.b, 255);
            filledCircleRGBA(renderer, rightX + 90, topY + 198, 18, c2.r, c2.g, c2.b, 255);
            filledCircleRGBA(renderer, rightX + 140, topY + 198, 18, c3.r, c3.g, c3.b, 255);
            filledCircleRGBA(renderer, rightX + 190, topY + 198, 18, c4.r, c4.g, c4.b, 255);
            
            int cx = rightX + 40 + (settingAccentColor * 50);
            circleRGBA(renderer, cx, topY + 198, 20, 255, 255, 255, 255);
            circleRGBA(renderer, cx, topY + 198, 21, 255, 255, 255, 255);
            
                        // Third Panel: Mantenimiento
            int topY2 = topY + pH + 20;
            int pH2 = 120;
            int pW2 = (pW * 2) + 40; // Spans across both panels
            
            roundedBoxRGBA(renderer, leftX, topY2, leftX + pW2, topY2 + pH2, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
            roundedRectangleRGBA(renderer, leftX, topY2, leftX + pW2, topY2 + pH2, 12, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
            DrawText(renderer, fontButton, "Mantenimiento", leftX + 20, topY2 + 20, colorAccent);
            
            if (selectedSettingIndex == 2) {
                roundedBoxRGBA(renderer, leftX + 20, topY2 + 60, leftX + pW2 - 20, topY2 + 100, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
            } else {
                roundedBoxRGBA(renderer, leftX + 20, topY2 + 60, leftX + pW2 - 20, topY2 + 100, 8, 80, 80, 80, 255);
            }
            DrawText(renderer, fontSmall, "[A] Limpiar Archivos Basura (Tickets huerfanos)", leftX + 40, topY2 + 70, selectedSettingIndex == 2 ? SDL_Color{30,30,30,255} : colorTextMain);
            
            if (appletGetAppletType() != AppletType_Application) {
                DrawText(renderer, fontSmall, "MODO APPLET DETECTADO: Lanza la app manteniendo [R] sobre un juego.", 50, WINDOW_HEIGHT - 95, {255, 100, 100, 255});
            }
            
            if (isDetailsPopupOpen) {
                int pW = 550;
                int pH = 250;
                int pX = (WINDOW_WIDTH - pW) / 2;
                int pY = (WINDOW_HEIGHT - pH) / 2;
                boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 150);
                roundedBoxRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, 255);
                roundedRectangleRGBA(renderer, pX, pY, pX + pW, pY + pH, 16, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, 255);
                DrawText(renderer, fontTitle, GetText("popup_notice"), WINDOW_WIDTH/2, pY + 30, colorAccent, true);
                DrawText(renderer, fontButton, popupMessage, WINDOW_WIDTH/2, pY + 70, colorTextMain, true, pW - 40);
                roundedBoxRGBA(renderer, pX + 50, pY + 190, pX + pW - 50, pY + 230, 8, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                DrawText(renderer, fontButton, GetText("popup_close"), WINDOW_WIDTH/2, pY + 200, {30,30,30,255}, true);
            }
        }
        else if (currentState == STATE_MTP || currentState == STATE_USB_INSTALL) {
            DrawText(renderer, fontTitle, currentState == STATE_MTP ? GetText("mtp_title") : GetText("menu_usb"), 50, 40, colorAccent);
            
            // Speed calculation
            u32 currentTicks = SDL_GetTicks();
            if (currentTicks - mtpLastTicks >= 1000) {
                u64 bytesNow = g_installerCore ? g_installerCore->GetBytesWritten() : 0;
                mtpSpeed = (float)(bytesNow - mtpLastBytes) / 1024.0f / 1024.0f; // MB/s
                mtpLastBytes = bytesNow;
                mtpLastTicks = currentTicks;
            }

            // Process newly finished files
            if (currentState == STATE_MTP && g_installerCore) {
                while (true) {
                    std::string finishedFn = g_installerCore->GetFinishedFilename();
                    if (finishedFn.empty()) break;
                    
                    // Strip path
                    size_t lastSlash = finishedFn.find_last_of('/');
                    if (lastSlash != std::string::npos) finishedFn = finishedFn.substr(lastSlash + 1);
                    
                    for (auto& item : mtpHistory) {
                        if (item.name == finishedFn && item.status == 2) {
                            item.status = g_installerCore->HasError() ? 1 : 0;
                            item.errorCode = g_installerCore->GetErrorCode();
                            item.freedBytes = g_installerCore->GetFreedBytes();
                            break;
                        }
                    }
                }
            }

            int startY = 120;
            
            // Panel Superior: Progreso
            int panelW = WINDOW_WIDTH - 100;
            roundedBoxRGBA(renderer, 50, startY, 50 + panelW, startY + 140, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
            roundedRectangleRGBA(renderer, 50, startY, 50 + panelW, startY + 140, 12, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, colorPanelBorder.a);
            
            bool isInstalling = g_installerCore && !g_installerCore->IsFinished() && !g_installerCore->HasError() && g_installerCore->GetBytesWritten() > 0;
            bool isWaiting = !isInstalling && mtpHistory.empty();
            if (currentState == STATE_USB_INSTALL) isWaiting = (usbInstallCurrentIndex >= (int)usbInstallQueue.size()) && !g_installerReadThreadActive;
            
            if (isWaiting) {
                if (currentState == STATE_MTP) {
                    DrawText(renderer, fontButton, GetText("mtp_wait1"), WINDOW_WIDTH/2, startY + 40, colorTextMain, true);
                    DrawText(renderer, fontSmall, GetText("mtp_wait2"), WINDOW_WIDTH/2, startY + 70, {200, 200, 200, 255}, true);
                    DrawText(renderer, fontSmall, GetText("mtp_wait3"), WINDOW_WIDTH/2, startY + 100, {200, 200, 200, 255}, true);
                } else {
                    DrawText(renderer, fontButton, "Instalación completada.", WINDOW_WIDTH/2, startY + 40, colorTextMain, true);
                    DrawText(renderer, fontSmall, "[B] Volver", WINDOW_WIDTH/2, startY + 80, {200, 200, 200, 255}, true);
                }
            } else {
                std::string titleStr = isInstalling ? (GetText("mtp_inst_title") + mtpCurrentFile) : (GetText("mtp_last_title") + mtpCurrentFile);
                DrawText(renderer, fontButton, titleStr, 70, startY + 20, colorTextMain);
                
                if (isInstalling) {
                    char speedBuf[32];
                    snprintf(speedBuf, sizeof(speedBuf), "%.1f MB/s", mtpSpeed);
                    DrawText(renderer, fontButton, speedBuf, 50 + panelW - 150, startY + 20, colorAccent);
                }
                
                int barX = 70;
                int barY = startY + 70;
                int barW = panelW - 40;
                int barH = 20;
                
                roundedBoxRGBA(renderer, barX, barY, barX + barW, barY + barH, 10, 0, 0, 0, 100);
                u64 written = g_installerCore ? g_installerCore->GetBytesWritten() : 0;
                
                if (isInstalling) {
                    float pct = g_installerTotalSize > 0 ? ((float)written / (float)g_installerTotalSize) : 0.0f;
                    if (pct > 1.0f) pct = 1.0f;
                    int fillW = (int)(barW * pct);
                    if (fillW < 10) fillW = 10;
                    roundedBoxRGBA(renderer, barX, barY, barX + fillW, barY + barH, 10, colorAccent.r, colorAccent.g, colorAccent.b, 255);
                } else {
                    if (g_installerCore && g_installerCore->HasError()) {
                        roundedBoxRGBA(renderer, barX, barY, barX + barW, barY + barH, 10, 255, 50, 50, 255);
                    } else {
                        roundedBoxRGBA(renderer, barX, barY, barX + barW, barY + barH, 10, 50, 255, 50, 255);
                    }
                }
                
                std::string progressTxt;
                if (g_installerTotalSize > 0) {
                    float pct = ((float)written / (float)g_installerTotalSize) * 100.0f;
                    if (pct > 100.0f) pct = 100.0f;
                    char pctBuf[32];
                    snprintf(pctBuf, sizeof(pctBuf), " (%.1f%%)", pct);
                    progressTxt = FormatSize(written) + " / " + FormatSize(g_installerTotalSize) + pctBuf;
                } else {
                    progressTxt = FormatSize(written) + GetText("mtp_transferred");
                }
                DrawText(renderer, fontSmall, progressTxt, 70, barY + 30, {200, 200, 200, 255});
                
                if (g_installerCore && g_installerCore->HasError()) {
                    char errStr[128];
                    sprintf(errStr, "%s (0x%X)", GetText("mtp_err").c_str(), g_installerCore->GetErrorCode());
                    DrawText(renderer, fontSmall, errStr, 50 + panelW - 350, barY + 30, {255, 50, 50, 255});
                } else if (!isInstalling && !mtpHistory.empty() && mtpHistory.front().status == 0) {
                    DrawText(renderer, fontSmall, GetText("mtp_ok"), 50 + panelW - 150, barY + 30, {50, 255, 50, 255});
                }
            }
            
            // Panel Inferior: Historial
            int histY = startY + 160;
            int histH = WINDOW_HEIGHT - histY - 100;
            roundedBoxRGBA(renderer, 50, histY, 50 + panelW, histY + histH, 12, colorPanelBg.r, colorPanelBg.g, colorPanelBg.b, colorPanelBg.a);
            roundedRectangleRGBA(renderer, 50, histY, 50 + panelW, histY + histH, 12, colorPanelBorder.r, colorPanelBorder.g, colorPanelBorder.b, colorPanelBorder.a);
            
            std::string titleHist = mtpHistory.size() > (size_t)((histH - 80) / 40) ? GetText("mtp_hist_nav") : GetText("mtp_hist_title");
            titleHist += " (" + std::to_string(mtpHistory.size()) + ")";
            DrawText(renderer, fontButton, titleHist, 70, histY + 20, colorTextMain);
            
            int itemY = histY + 70;
            int maxItems = (histH - 80) / 40;
            for (size_t i = mtpHistoryScroll; i < mtpHistory.size() && i < (size_t)(mtpHistoryScroll + maxItems); i++) {
                const auto& item = mtpHistory[i];
                DrawText(renderer, fontSmall, item.name, 70, itemY, colorTextMain);
                DrawText(renderer, fontSmall, FormatSize(item.size), 50 + panelW - 350, itemY, {200, 200, 200, 255});
                
                SDL_Color statusColor = {200, 200, 200, 255};
                std::string statusStr = "";
                if (item.status == 0) { 
                    if (item.freedBytes > 0) {
                        char cleanStr[128];
                        sprintf(cleanStr, "%s (-%s)", GetText("mtp_ok").c_str(), FormatSize(item.freedBytes).c_str());
                        statusStr = cleanStr;
                    } else {
                        statusStr = GetText("mtp_ok"); 
                    }
                    statusColor = {50, 255, 50, 255}; 
                }
                else if (item.status == 1) { 
                    char errStr[128];
                    sprintf(errStr, "%s (0x%X)", GetText("mtp_err").c_str(), item.errorCode);
                    statusStr = errStr; 
                    statusColor = {255, 50, 50, 255}; 
                }
                else if (item.status == 2) { 
                    u64 current_written = g_installerCore ? g_installerCore->GetBytesWritten() : 0;
                    bool is_saving = (i > 0) || (item.size > 0 && current_written >= item.size);
                    statusStr = is_saving ? "Guardando en SD..." : GetText("mtp_installing"); 
                    statusColor = colorAccent; 
                }
                
                DrawText(renderer, fontSmall, statusStr, 50 + panelW - 200, itemY, statusColor);
                itemY += 40;
            }
            
            // Scrollbar
            if (mtpHistory.size() > (size_t)maxItems) {
                int scrollX = 50 + panelW - 20;
                int scrollY = histY + 70;
                int scrollH = maxItems * 40;
                
                // Track
                roundedBoxRGBA(renderer, scrollX, scrollY, scrollX + 8, scrollY + scrollH, 4, 0, 0, 0, 100);
                
                // Thumb
                float contentRatio = (float)maxItems / (float)mtpHistory.size();
                int thumbH = (int)(scrollH * contentRatio);
                if (thumbH < 20) thumbH = 20; // min height
                
                float scrollRatio = (float)mtpHistoryScroll / (float)(mtpHistory.size() - maxItems);
                int thumbY = scrollY + (int)((scrollH - thumbH) * scrollRatio);
                
                roundedBoxRGBA(renderer, scrollX, thumbY, scrollX + 8, thumbY + thumbH, 4, colorAccent.r, colorAccent.g, colorAccent.b, 255);
            }
        }

        // Draw Footer Controls (A and B buttons)
        int cy = WINDOW_HEIGHT - 65;
        
        if (currentState == STATE_GAME_DETAILS) {
            // ZL Seleccionar, X Opciones, B Atrás, A Ver Contenido
            int btnY = WINDOW_HEIGHT - 65;
            
            int xCx = 550;
            filledCircleRGBA(renderer, xCx, btnY, 18, 60, 60, 60, 255);
            DrawText(renderer, fontButton, "X", xCx, btnY - 12, {240, 240, 240, 255}, true);
            DrawText(renderer, fontButton, "Opciones", xCx + 25, btnY - 12, colorTextMain, false);

            int bCx = 750;
            filledCircleRGBA(renderer, bCx, btnY, 18, 60, 60, 60, 255);
            DrawText(renderer, fontButton, "B", bCx, btnY - 12, {240, 240, 240, 255}, true);
            DrawText(renderer, fontButton, "Atrás", bCx + 25, btnY - 12, colorTextMain, false);

            int aCx = 920;
            filledCircleRGBA(renderer, aCx, btnY, 18, 60, 60, 60, 255);
            DrawText(renderer, fontButton, "A", aCx, btnY - 12, {240, 240, 240, 255}, true);
            DrawText(renderer, fontButton, "Ver Contenido", aCx + 25, btnY - 12, colorTextMain, false);

        } else {
            if (currentState == STATE_GAMES || currentState == STATE_SAVES) {
                int xButtonCx = WINDOW_WIDTH - 380;
                filledCircleRGBA(renderer, xButtonCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, GetText("btn_x"), xButtonCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, GetText("btn_view"), xButtonCx + 25, cy - 12, colorTextMain, false);
            }

            if (currentState == STATE_USB) {
                int yCx = WINDOW_WIDTH - 420;
                filledCircleRGBA(renderer, yCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "Y", yCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Marcar", yCx + 25, cy - 12, colorTextMain, false);
                
                int aCx = WINDOW_WIDTH - 280;
                filledCircleRGBA(renderer, aCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, GetText("btn_a"), aCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Instalar", aCx + 25, cy - 12, colorTextMain, false);
            } else if (currentState == STATE_TORRENT_DOWNLOAD) {
                // Torrent progress custom buttons: A=Pause/Resume, B=Cancel, X=Install (when finished)
                TorrentStatus ts = TorrentEngine::GetStatus(currentTorrentId);
                
                int aCx = WINDOW_WIDTH - 420;
                filledCircleRGBA(renderer, aCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "A", aCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, ts.paused ? "Reanudar" : "Pausar", aCx + 25, cy - 12, colorTextMain, false);

                if (ts.finished && !ts.error) {
                    int xCx = WINDOW_WIDTH - 280;
                    filledCircleRGBA(renderer, xCx, cy, 18, 60, 60, 60, 255);
                    DrawText(renderer, fontButton, "X", xCx, cy - 12, {240, 240, 240, 255}, true);
                    DrawText(renderer, fontButton, "Instalar", xCx + 25, cy - 12, colorTextMain, false);
                }

                int bCx = WINDOW_WIDTH - 140;
                filledCircleRGBA(renderer, bCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "B", bCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Cancelar", bCx + 25, cy - 12, colorTextMain, false);
                
            } else if (currentState == STATE_STORE_GOONIES) {
                // Catalog actions: Y=Search, X=Filter, L/R=Sort, A=Select, B=Back
                int yCx = 70;
                filledCircleRGBA(renderer, yCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "Y", yCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Buscar", yCx + 25, cy - 12, colorTextMain, false);

                int xCx = 210;
                filledCircleRGBA(renderer, xCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "X", xCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Filtrar", xCx + 25, cy - 12, colorTextMain, false);

                int lrCx = 350;
                filledCircleRGBA(renderer, lrCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "L/R", lrCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Ordenar", lrCx + 25, cy - 12, colorTextMain, false);

                int aCx = WINDOW_WIDTH - 280;
                filledCircleRGBA(renderer, aCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "A", aCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, "Descargar", aCx + 25, cy - 12, colorTextMain, false);

                int bCx = WINDOW_WIDTH - 140;
                filledCircleRGBA(renderer, bCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, "B", bCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, GetText("btn_back"), bCx + 25, cy - 12, colorTextMain, false);

            } else if (currentState != STATE_HOME && currentState != STATE_SETTINGS && currentState != STATE_MTP && currentState != STATE_USB_INSTALL) {
                int aCx = WINDOW_WIDTH - 280;
                filledCircleRGBA(renderer, aCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, GetText("btn_a"), aCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, GetText("btn_select"), aCx + 25, cy - 12, colorTextMain, false);
            }

            // Draw generic back button if not home, settings, mtp, usb_install, torrent_download, or store_goonies (already custom rendered)
            if (currentState != STATE_HOME && currentState != STATE_SETTINGS && currentState != STATE_MTP && currentState != STATE_USB_INSTALL && currentState != STATE_TORRENT_DOWNLOAD && currentState != STATE_STORE_GOONIES) {
                int bCx = WINDOW_WIDTH - 140;
                filledCircleRGBA(renderer, bCx, cy, 18, 60, 60, 60, 255);
                DrawText(renderer, fontButton, GetText("btn_b"), bCx, cy - 12, {240, 240, 240, 255}, true);
                DrawText(renderer, fontButton, GetText("btn_back"), bCx + 25, cy - 12, colorTextMain, false);
            }
        }

        DrawText(renderer, fontSmall, GetText("credits"), WINDOW_WIDTH/2, WINDOW_HEIGHT - 25, {150, 150, 150, 255}, true);

        // Dim screen if downloading and inactive for > 60 seconds
        if (isDownloading && (armGetSystemTick() - lastInputTime) > (19200000ULL * 60)) {
            boxRGBA(renderer, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 0, 180);
        }

        SDL_RenderPresent(renderer);
    }

    for (auto& game : installedGamesList) {
        if (game.icon) SDL_DestroyTexture(game.icon);
    }
    if (logoTexture) SDL_DestroyTexture(logoTexture);
    if (fontTitle) TTF_CloseFont(fontTitle);
    if (fontButton) TTF_CloseFont(fontButton);
    if (fontSmall) TTF_CloseFont(fontSmall);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    usbHsFsExit();
    accountExit();
    ncmExit();
    nsExit();
    socketExit();
    romfsExit();
    
    TorrentEngine::Shutdown();

    return 0;
}
