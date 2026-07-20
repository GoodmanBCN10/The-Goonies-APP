#include "catalog.hpp"
#include <curl/curl.h>
#include <json.hpp>
#include <iostream>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = (std::string*)userp;
    mem->append((char*)contents, realsize);
    return realsize;
}

static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

#include <algorithm>
#include <sstream>

#ifdef __SWITCH__
#include <switch.h>
#endif

// Helpers for percent encoding and image relays
static std::string percentEncode(const std::string& value) {
    static const char digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char byte : value) {
        if ((byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
            byte == '.' || byte == '~') {
            encoded.push_back(static_cast<char>(byte));
        } else {
            encoded.push_back('%');
            encoded.push_back(digits[byte >> 4]);
            encoded.push_back(digits[byte & 15]);
        }
    }
    return encoded;
}

static bool isNintendoImageUrl(const std::string& url) {
    static const std::string prefix = "https://img-eshop.cdn.nintendo.net/";
    return url.compare(0, prefix.size(), prefix) == 0;
}

static std::string weservRelayUrl(const std::string& sourceUrl, bool secure) {
    return std::string(secure ? "https://images.weserv.nl/?url=" : "http://images.weserv.nl/?url=") +
           percentEncode(sourceUrl) + "&output=jpg";
}

static std::string photonRelayUrl(const std::string& sourceUrl) {
    static const std::string https = "https://";
    std::string rest = sourceUrl.compare(0, https.size(), https) == 0 ? sourceUrl.substr(https.size()) : sourceUrl;
    return "https://i0.wp.com/" + rest + "?ssl=1";
}

static std::string ddgRelayUrl(const std::string& sourceUrl) {
    return "https://external-content.duckduckgo.com/iu/?u=" + percentEncode(sourceUrl);
}

bool Catalog::DownloadString(const std::string& url, std::string& outString) {
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outString);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo Switch; WifiWebAuthApplet) AppleWebKit/606.4 (KHTML, like Gecko) NF/6.0.1.15.4 NintendoBrowser/5.1.0.22443");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Switch often lacks CA bundles out of the box
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        return res == CURLE_OK;
    }
    return false;
}

bool Catalog::DownloadFile(const std::string& url, const std::string& outPath) {
    auto attempt = [&](const std::string& requestUrl) {
        CURL* curl;
        CURLcode res;
        FILE* fp = fopen(outPath.c_str(), "wb");
        if (!fp) return false;
        
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo Switch; WifiWebAuthApplet) AppleWebKit/606.4 (KHTML, like Gecko) NF/6.0.1.15.4 NintendoBrowser/5.1.0.22443");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L); // Bounded timeout for relays
            
            res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            fclose(fp);
            return res == CURLE_OK;
        }
        fclose(fp);
        return false;
    };

    bool isHttp = (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0);

    if (attempt(url)) {
        return true;
    }

    if (isHttp) {
        // Try various image relays if direct fetch failed (due to geo-blocking, cert issues, or host limitations)
        std::vector<std::string> relays = {
            photonRelayUrl(url),          // Automattic
            ddgRelayUrl(url),             // DuckDuckGo / Bing
            weservRelayUrl(url, false),   // Cloudflare, http
            weservRelayUrl(url, true)     // Cloudflare, https
        };
        for (const auto& relayUrl : relays) {
            if (attempt(relayUrl)) {
                return true;
            }
        }
    }

    return false;
}

bool Catalog::ParseCatalog(const std::string& jsonString, std::vector<CatalogGame>& outGames) {
    try {
        json j = json::parse(jsonString);
        
        for (auto& item : j) {
            CatalogGame game;
            game.title = item.value("title", "Desconocido");
            game.id = item.value("id", "");
            game.size = item.value("size", "");
            game.torrentUrl = item.value("magnet", "");
            if (game.torrentUrl.empty()) game.torrentUrl = item.value("torrent", "");
            game.iconUrl = item.value("cover", ""); // In Rutracker catalog, cover image URL is under "cover" field instead of "icon"
            if (game.iconUrl.empty()) game.iconUrl = item.value("icon", "");
            game.description = item.value("description", "");
            game.developer = item.value("developer", "");
            game.publisher = item.value("publisher", "");
            game.genre = item.value("genre", "");
            game.year = item.value("year", "");
            game.interface_lang = item.value("interface_lang", "");
            game.voice_lang = item.value("voice_lang", "");
            game.originalIndex = (int)outGames.size();
            
            if (!game.title.empty() && !game.torrentUrl.empty()) {
                outGames.push_back(game);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}
