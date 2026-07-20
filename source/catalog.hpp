#pragma once
#include <string>
#include <vector>

struct CatalogGame {
    std::string id;
    std::string title;
    std::string size;
    std::string torrentUrl;
    std::string iconUrl;
    std::string description;
    std::string developer;
    std::string publisher;
    std::string genre;
    std::string year;
    std::string interface_lang;
    std::string voice_lang;
    int originalIndex;
};

class Catalog {
public:
    static bool DownloadString(const std::string& url, std::string& outString);
    static bool DownloadFile(const std::string& url, const std::string& outPath);
    static bool ParseCatalog(const std::string& jsonString, std::vector<CatalogGame>& outGames);
};
