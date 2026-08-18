#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <algorithm>
#include <nlohmann/json.hpp>

constexpr const char* kReleaseAssetPrefix =
    "https://github.com/GoodmanBCN10/The-Goonies-APP/releases/download/";

bool trustedAssetUrl(const std::string& url) {
    return url.compare(0, std::strlen(kReleaseAssetPrefix),
                       kReleaseAssetPrefix) == 0;
}

bool parseVersion(const std::string& text, std::array<uint64_t, 3>& version) {
    std::string value = text;
    if (!value.empty() && value.front() == 'v')
        value.erase(value.begin());
    size_t start = 0;
    for (size_t index = 0; index < version.size(); ++index) {
        size_t end = value.find('.', start);
        if ((index + 1 == version.size()) != (end == std::string::npos))
            return false;
        const std::string part = value.substr(start, end - start);
        if (part.empty() || !std::all_of(part.begin(), part.end(),
            [](unsigned char c) { return std::isdigit(c); }))
            return false;
        try {
            version[index] = std::stoull(part);
        } catch (...) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool isNewerVersion(const std::string& candidate,
                                   const std::string& current) {
    std::array<uint64_t, 3> candidateParts{};
    std::array<uint64_t, 3> currentParts{};
    return parseVersion(candidate, candidateParts) &&
           parseVersion(current, currentParts) && candidateParts > currentParts;
}

struct ReleaseInfo {
    std::string version;
    std::string nroUrl;
    std::string checksumUrl;
};

bool parseRelease(const std::string& json, ReleaseInfo& release,
                                 std::string& error) {
    release = {};
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "GitHub returned an invalid release.";
        return false;
    }
    if (root.value("draft", true) || root.value("prerelease", true)) {
        error = "GitHub latest release is not published and stable.";
        return false;
    }
    release.version = root.value("tag_name", "");
    if (!isNewerVersion(release.version, "0.0.0")) {
        error = "GitHub release has an invalid version tag.";
        return false;
    }
    if (!root.contains("assets") || !root["assets"].is_array()) {
        error = "GitHub release has no assets.";
        return false;
    }
    for (const auto& asset : root["assets"]) {
        if (!asset.is_object())
            continue;
        const std::string name = asset.value("name", "");
        const std::string url = asset.value("browser_download_url", "");
        if (!trustedAssetUrl(url))
            continue;
        if (name == "TheGooniesInstaller.nro")
            release.nroUrl = url;
        else if (name == "TheGooniesInstaller.nro.sha256")
            release.checksumUrl = url;
    }
    if (release.nroUrl.empty() || release.checksumUrl.empty()) {
        error = "GitHub release must include TheGooniesInstaller.nro and TheGooniesInstaller.nro.sha256.";
        return false;
    }
    return true;
}

int main() {
    std::ifstream t("release.json");
    std::string str((std::istreambuf_iterator<char>(t)),
                     std::istreambuf_iterator<char>());
    ReleaseInfo rel;
    std::string err;
    bool res = parseRelease(str, rel, err);
    std::cout << "res=" << res << " err=" << err << std::endl;
    return 0;
}
