#include "catalog_service.hpp"
#include "magnet_resolver.hpp"

extern "C" {
#include "../core/sha1.h"
#include "../core/util.h"
}

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "catalog_service.hpp"
#include "../app_state.hpp"
#include <curl/curl.h>
#include <fstream>
#include <borealis/extern/nlohmann/json.hpp>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <minizip/unzip.h>

namespace pipensx {
namespace {

constexpr size_t kMaxCatalogBytes = 32 * 1024 * 1024;
constexpr size_t kMaxCatalogEntries = 40000;
constexpr size_t kMaxInfoDictBytes = 16 * 1024 * 1024;

/* The live catalogue source: the Langegen switch-games repo publishes a single
   switch_games.json on GitHub's raw host. Fetched on demand by the refresh
   button and (when the catalogue is empty or the launch toggle is on) in the
   background at startup. Must satisfy isTrustedSource(). */
constexpr const char* kCatalogSourceUrl =
    "https://raw.githubusercontent.com/Langegen/switch-games/"
    "refs/heads/main/switch_games.json";

constexpr const char* kSwitchbruSourceUrl =
    "https://github.com/GoodmanBCN10/GooniesPorts-Data/releases/download/juegos/catalog.json";

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string lowerAscii(std::string_view s) {
    std::string result(s);
    for (char& c : result) if (c >= 'A' && c <= 'Z') c += 32;
    return result;
}

bool decodeBase64(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    if (text.empty() || text.size() % 4 != 0)
        return false;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int values[4];
        int padding = 0;
        for (size_t j = 0; j < 4; ++j) {
            char c = text[i + j];
            if (c == '=' && i + 4 == text.size() && j >= 2) {
                values[j] = 0;
                ++padding;
                continue;
            }
            if (padding || (values[j] = base64Value(c)) < 0)
                return false;
        }
        uint32_t block = (static_cast<uint32_t>(values[0]) << 18) |
                         (static_cast<uint32_t>(values[1]) << 12) |
                         (static_cast<uint32_t>(values[2]) << 6) |
                         static_cast<uint32_t>(values[3]);
        out.push_back(static_cast<uint8_t>(block >> 16));
        if (padding < 2)
            out.push_back(static_cast<uint8_t>(block >> 8));
        if (padding < 1)
            out.push_back(static_cast<uint8_t>(block));
    }
    return true;
}

struct HttpBuffer {
    std::string data;
    bool overflow = false;
};

size_t writeHttp(void* bytes, size_t size, size_t count, void* user) {
    HttpBuffer* buffer = static_cast<HttpBuffer*>(user);
    size_t total = size * count;
    if (buffer->data.size() + total > kMaxCatalogBytes) {
        buffer->overflow = true;
        return 0;
    }
    buffer->data.append(static_cast<const char*>(bytes), total);
    return total;
}

bool makeDirectories(const std::string& path) {
    char buffer[1024];
    if (path.empty() || path.size() >= sizeof(buffer))
        return false;
    std::snprintf(buffer, sizeof(buffer), "%s", path.c_str());
    char* cursor = buffer + 1;
    char* colon = strchr(buffer, ':');
    if (colon && *(colon + 1) == '/') {
        cursor = colon + 2;
    }
    for (; *cursor; ++cursor) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (mkdir(buffer, 0755) != 0 && errno != EEXIST)
            return false;
        *cursor = '/';
    }
    return mkdir(buffer, 0755) == 0 || errno == EEXIST;
}

bool readFile(const std::string& path, std::string& data,
              std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to open catalog file.";
        return false;
    }
    input.seekg(0, std::ios::end);
    std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size <= 0 || size > static_cast<std::streamoff>(kMaxCatalogBytes)) {
        error = "Catalog file is empty or too large.";
        return false;
    }
    data.resize(static_cast<size_t>(size));
    input.read(data.data(), size);
    if (!input) {
        error = "Unable to read catalog file.";
        return false;
    }
    return true;
}

bool httpGet(const std::string& url, std::string& body, std::string& error) {
    body.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Unable to initialize HTTP.";
        return false;
    }
    HttpBuffer buffer;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pipensx/0.4");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeHttp);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    
    // Support instant abort
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, global_curl_xferinfo);
    
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300 ||
        buffer.overflow) {
        if (buffer.overflow)
            error = "Catalog download exceeded the size limit.";
        else if (result != CURLE_OK)
            error = std::string("Catalog network error: ") +
                    curl_easy_strerror(result);
        else
            error = "Catalog server returned HTTP " + std::to_string(status) +
                    ".";
        return false;
    }
    body = std::move(buffer.data);
    return true;
}

bool writeAtomic(const std::string& path, const std::string& data,
                 std::string& error) {
    std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Unable to create catalog cache.";
            return false;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output.good()) {
            unlink(temporary.c_str());
            error = "Unable to write catalog cache.";
            return false;
        }
    }
    unlink(path.c_str());
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        error = "Unable to replace catalog cache.";
        return false;
    }
    return true;
}


uint64_t readUnsigned(const nlohmann::json& item, const char* key) {
    if (!item.contains(key))
        return 0;
    if (item[key].is_number_unsigned())
        return item[key].get<uint64_t>();
    if (item[key].is_number_integer()) {
        int64_t value = item[key].get<int64_t>();
        if (value > 0)
            return static_cast<uint64_t>(value);
    }
    return 0;
}

int64_t readSigned(const nlohmann::json& item, const char* key) {
    if (!item.contains(key) || !item[key].is_number_integer())
        return 0;
    return item[key].get<int64_t>();
}

// A plain string value, trimmed to `limit` bytes; empty when absent or not a
// string. Used for the Langegen inline metadata (year/genre/publisher/…).
std::string readString(const nlohmann::json& item, const char* key,
                       size_t limit) {
    if (!item.contains(key) || !item[key].is_string())
        return std::string();
    std::string value = item[key].get<std::string>();
    if (value.size() > limit)
        value.resize(limit);
    return value;
}

// Numeric id that the source may encode as a JSON number (bqio) or a decimal
// string (Langegen "topic_id":"6878751").
uint64_t readFlexibleUnsigned(const nlohmann::json& item, const char* key) {
    if (!item.contains(key))
        return 0;
    if (item[key].is_string()) {
        uint64_t value = 0;
        for (char c : item[key].get_ref<const std::string&>()) {
            if (c < '0' || c > '9')
                return value;  // stop at the first non-digit
            value = value * 10 + static_cast<uint64_t>(c - '0');
        }
        return value;
    }
    return readUnsigned(item, key);
}

// A human size string such as "5.19 GB" / "512 MB" / "700 KiB" → bytes. RuTracker
// labels are binary (1 GB == 1024 MiB), so scale on 1024. Returns 0 when the
// value is unparseable, which the detail card renders as "Unknown".
uint64_t parseSizeToBytes(const std::string& text) {
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    double value = 0.0;
    bool sawDigit = false;
    bool fraction = false;
    double scale = 0.1;
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            sawDigit = true;
            if (fraction) {
                value += (c - '0') * scale;
                scale *= 0.1;
            } else {
                value = value * 10 + (c - '0');
            }
        } else if ((c == '.' || c == ',') && !fraction) {
            fraction = true;
        } else {
            break;
        }
    }
    if (!sawDigit)
        return 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    char unit = i < text.size() ? static_cast<char>(std::toupper(
                                      static_cast<unsigned char>(text[i])))
                                : 'B';
    double multiplier = 1.0;
    switch (unit) {
        case 'T': multiplier = 1024.0 * 1024 * 1024 * 1024; break;
        case 'G': multiplier = 1024.0 * 1024 * 1024; break;
        case 'M': multiplier = 1024.0 * 1024; break;
        case 'K': multiplier = 1024.0; break;
        default: multiplier = 1.0; break;  // bytes or unknown
    }
    double bytes = value * multiplier;
    if (bytes < 0.0)
        return 0;
    return static_cast<uint64_t>(bytes + 0.5);
}

// The catalogue size field: a JSON number of bytes (bqio) or a human string
// (Langegen "size":"5.19 GB").
uint64_t readFlexibleSize(const nlohmann::json& item, const char* key) {
    if (!item.contains(key))
        return 0;
    if (item[key].is_string())
        return parseSizeToBytes(item[key].get_ref<const std::string&>());
    return readUnsigned(item, key);
}

} // namespace

CatalogService::CatalogService(std::string rootPath, std::string bundledPath)
    : rootPath_(std::move(rootPath)),
      cachePath_(rootPath_ + "/catalog.json"),
      bundledPath_(std::move(bundledPath)) {
    makeDirectories(rootPath_);
}


class GooniesSax : public nlohmann::json::json_sax_t {
public:
    std::vector<CatalogEntry>& entries;
    CatalogEntry current;
    std::string current_key;
    int object_depth = 0;
    bool in_array = false;

    GooniesSax(std::vector<CatalogEntry>& e) : entries(e) {}

    bool null() override { return true; }
    bool boolean(bool val) override { return true; }
    bool number_integer(number_integer_t val) override { 
        if (in_array && object_depth == 1 && current_key == "size") {
            current.size = static_cast<uint64_t>(val);
        }
        return true; 
    }
    bool number_unsigned(number_unsigned_t val) override { 
        if (in_array && object_depth == 1 && current_key == "size") {
            current.size = static_cast<uint64_t>(val);
        }
        return true; 
    }
    bool number_float(number_float_t val, const string_t& s) override { return true; }
    bool string(string_t& val) override { 
        if (in_array && object_depth == 1) {
            if (current_key == "title") current.title = val.substr(0, 1024);
            else if (current_key == "name") current.name = val.substr(0, 1024);
            else if (current_key == "category") current.category = val.substr(0, 256);
            else if (current_key == "version") current.version = val.substr(0, 256);
            else if (current_key == "author") current.developer = val.substr(0, 256);
            else if (current_key == "url") current.directDownloadUrl = val.substr(0, 1024);
            else if (current_key == "icon") current.posterUrl = val.substr(0, 1024);
            else if (current_key == "description") current.description = val.substr(0, 2048);
        }
        return true; 
    }
    bool start_object(std::size_t elements) override {
        object_depth++;
        if (in_array && object_depth == 1) {
            current = CatalogEntry{};
        }
        return true;
    }
    bool end_object() override {
        if (in_array && object_depth == 1) {
            if (!current.title.empty() && !current.directDownloadUrl.empty()) {
                entries.push_back(current);
            }
        }
        object_depth--;
        return true;
    }
    bool start_array(std::size_t elements) override {
        if (object_depth == 0) in_array = true;
        return true;
    }
    bool end_array() override {
        if (object_depth == 0) in_array = false;
        return true;
    }
    bool key(string_t& val) override {
        current_key = val;
        return true;
    }
    bool binary(nlohmann::json::binary_t& val) override { return true; }
    bool parse_error(std::size_t position, const std::string& last_token, const nlohmann::json::exception& ex) override { return false; }
};

bool CatalogService::parseJson(const std::string& json_str, std::vector<CatalogEntry>& entries, std::string& error) {
    entries.clear();
    try {
        auto j = nlohmann::json::parse(json_str);
        if (j.is_array()) {
            for (auto& item : j) {
                CatalogEntry entry;
                if (item.contains("title") && item["title"].is_string()) entry.title = item["title"];
                if (item.contains("name") && item["name"].is_string()) entry.name = item["name"];
                if (item.contains("category") && item["category"].is_string()) entry.category = item["category"];
                if (item.contains("version") && item["version"].is_string()) entry.version = item["version"];
                if (item.contains("developer") && item["developer"].is_string()) entry.developer = item["developer"];
                if (item.contains("directDownloadUrl") && item["directDownloadUrl"].is_string()) entry.directDownloadUrl = item["directDownloadUrl"];
                if (item.contains("posterUrl") && item["posterUrl"].is_string()) entry.posterUrl = item["posterUrl"];
                if (item.contains("description") && item["description"].is_string()) entry.description = item["description"];
                
                if (!entry.title.empty() && !entry.directDownloadUrl.empty()) {
                    entries.push_back(entry);
                }
            }
        } else {
            error = "JSON root is not an array";
            return false;
        }
    } catch (const std::exception& e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
    return true;
}

bool CatalogService::loadFile(const std::string& path,
                              const std::string& label,
                              std::string& error) {
    std::string body;
    if (!readFile(path, body, error))
        return false;
    std::vector<CatalogEntry> parsed;
    if (!parseJson(body, parsed, error))
        return false;
        
    adopt(std::move(parsed));
    sourceLabel_ = label;
    log_msg("[catalog] loaded %zu entries from %s\n", entries_.size(),
            path.c_str());
    return true;
}


bool CatalogService::load(std::string& error) {
    if (loadFile(cachePath_, "cached catalog", error))
        return true;
    return false;
}



struct ZipProgressData {
    std::atomic<bool>* cancelled;
    std::function<void(uint64_t, uint64_t)> progressCb;
};

static size_t zipWriteCb(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

static int zipProgressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    ZipProgressData* data = static_cast<ZipProgressData*>(clientp);
    if (data->cancelled && data->cancelled->load()) {
        return 1;
    }
    if (data->progressCb && dltotal > 0) {
        data->progressCb(dltotal, dlnow);
    }
    return 0;
}

bool CatalogService::downloadAndExtractZip(const std::string& url, const std::string& entryName,
                                  const std::string& targetDir,
                                  std::atomic<bool>* cancelled,
                                  std::function<void(uint64_t, uint64_t)> progressCb, std::function<void(uint64_t, uint64_t)> extractProgressCb,
                                  std::string& error) {
    
    bool isNro = (url.find(".nro") != std::string::npos);
    std::string tempZip = targetDir + (isNro ? ("/switch/" + entryName + "/" + entryName + ".nro") : "/temp_port.zip");
    
    if (isNro) {
        makeDirectories(targetDir + "/switch/" + entryName);
    }

    FILE* fp = fopen(tempZip.c_str(), "wb");
    if (!fp) {
        error = "Cannot create temp zip file.";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        error = "Cannot initialize curl.";
        return false;
    }

    ZipProgressData pdata{cancelled, progressCb};

    // Aceleracion extrema de lectura y escritura para Nintendo Switch
    setvbuf(fp, NULL, _IOFBF, 1024 * 1024); // 1MB de buffer de escritura en tarjeta SD

    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512 * 1024L); // 512KB de buffer de red curl
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, zipWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, zipProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pdata);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pipensx/0.4");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || status >= 400) {
        if (status >= 400) {
            error = "HTTP Error " + std::to_string(status);
        } else
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            error = "Download cancelled.";
        } else {
            error = curl_easy_strerror(res);
        }
        unlink(tempZip.c_str());
        return false;
    }

    if (isNro) {
        // Direct NRO download completed
        return true;
    }

    uint64_t zipSize = 0;
    struct stat st;
    if (stat(tempZip.c_str(), &st) == 0) zipSize = st.st_size;
    uint64_t extractedBytes = 0;
    unzFile uf = unzOpen64(tempZip.c_str());
    if (uf == NULL) {
        error = "Invalid zip file.";
        unlink(tempZip.c_str());
        return false;
    }

    unz_global_info64 global_info;
    if (unzGetGlobalInfo64(uf, &global_info) != UNZ_OK) {
        error = "Cannot read zip global info.";
        unzClose(uf);
        unlink(tempZip.c_str());
        return false;
    }

    for (uLong i = 0; i < global_info.number_entry; ++i) {
        if (cancelled && cancelled->load()) {
            error = "Extraction cancelled.";
            unzClose(uf);
            unlink(tempZip.c_str());
            return false;
        }

        char filename_inzip[256];
        unz_file_info64 file_info;
        if (unzGetCurrentFileInfo64(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) {
            unzClose(uf);
            unlink(tempZip.c_str());
            error = "Cannot read file info in zip.";
            return false;
        }

        std::string fname(filename_inzip);
        if (fname == "info.json" || fname == "manifest.install" || fname == "icon.png" || fname.find("screen") == 0) {
            if (i < global_info.number_entry - 1) {
                if (unzGoToNextFile(uf) != UNZ_OK) break;
            }
            continue;
        }

        std::string out_path = targetDir + "/" + filename_inzip;
        static std::string last_created_dir = ""; // Cache para no hacer mkdir miles de veces
        
        if (filename_inzip[strlen(filename_inzip) - 1] == '/') {
            if (last_created_dir != out_path) {
                makeDirectories(out_path);
                last_created_dir = out_path;
            }
        } else {
            size_t slash_pos = out_path.find_last_of('/');
            if (slash_pos != std::string::npos) {
                std::string dir_path = out_path.substr(0, slash_pos);
                if (last_created_dir != dir_path) {
                    makeDirectories(dir_path);
                    last_created_dir = dir_path;
                }
            }
            
            if (unzOpenCurrentFile(uf) == UNZ_OK) {
                FILE* out = fopen(out_path.c_str(), "wb");
                if (out) {
                    std::vector<char> buf(256 * 1024); // 256KB es el tama�o �ptimo de cluster FAT32/exFAT
                    int len;
                    while ((len = unzReadCurrentFile(uf, buf.data(), buf.size())) > 0) {
                        fwrite(buf.data(), 1, len, out);
                        extractedBytes += len;
                        if (extractProgressCb) extractProgressCb(extractedBytes, zipSize);
                    }
                    fclose(out);
                }
                unzCloseCurrentFile(uf);
            }
        }

        if (i < global_info.number_entry - 1) {
            if (unzGoToNextFile(uf) != UNZ_OK) break;
        }
    }
    unzClose(uf);
    unlink(tempZip.c_str());
    return true;
}

bool CatalogService::fetchLatest(std::vector<CatalogEntry>& parsed,
                                     std::string& error) {
        parsed.clear();
        std::string body;
        std::string fetchError;
        // The Goonies Ports official catalog
        std::string catalogUrl = "https://github.com/GoodmanBCN10/GooniesPorts-Data/releases/download/juegos/catalog.json";
        if (!httpGet(catalogUrl, body, fetchError)) {
            error = fetchError;
            return false;
        }
        if (!parseJson(body, parsed, fetchError)) {
            error = fetchError;
            return false;
        }
        writeAtomic(cachePath_, body, fetchError);
        return true;
    }

    void CatalogService::adopt(std::vector<CatalogEntry> parsed) {
        entries_ = std::move(parsed);
        sourceLabel_ = "The Goonies Ports";
        log_msg("[catalog] refreshed %zu entries from %s\n", entries_.size(),
                sourceLabel_.c_str());
    }

    } // namespace pipensx
