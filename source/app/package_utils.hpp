#pragma once

#include <cstdint>
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>

namespace goonies {

enum class PackageKind {
    BaseGame,
    Update,
    DLC,
    Unknown
};

struct PackageInfo {
    PackageKind kind = PackageKind::Unknown;
    uint64_t titleId = 0;       // exact title ID from filename if present
    uint64_t baseTitleId = 0;   // titleId & ~0x1FFFULL (or knownAppId)
    uint32_t version = 0;       // parsed integer version, e.g. 65536
    bool isPackage = false;
};

// Parses 16-hex Title ID from filename, inside [] or (), or standalone
inline uint64_t parseTitleIdFromFileName(const std::string& name) {
    if (name.empty()) return 0;

    auto is16Hex = [](const char* s) -> bool {
        for (int i = 0; i < 16; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    };

    auto toHex64 = [](const char* s) -> uint64_t {
        uint64_t val = 0;
        for (int i = 0; i < 16; ++i) {
            char c = s[i];
            uint8_t d = 0;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return 0;
            val = (val << 4) | d;
        }
        return val;
    };

    // First, look inside [] or ()
    for (size_t i = 0; i < name.size(); ++i) {
        char openChar = name[i];
        if (openChar != '[' && openChar != '(') continue;
        char closeChar = (openChar == '[') ? ']' : ')';

        size_t closePos = name.find(closeChar, i + 1);
        if (closePos == std::string::npos) continue;

        std::string inside = name.substr(i + 1, closePos - i - 1);
        if (inside.size() >= 16) {
            for (size_t j = 0; j + 16 <= inside.size(); ++j) {
                if (is16Hex(&inside[j])) {
                    bool leftBoundary = (j == 0) || !std::isxdigit(static_cast<unsigned char>(inside[j - 1]));
                    bool rightBoundary = (j + 16 == inside.size()) || !std::isxdigit(static_cast<unsigned char>(inside[j + 16]));
                    if (leftBoundary && rightBoundary) {
                        uint64_t tid = toHex64(&inside[j]);
                        if (tid >= 0x0100000000000000ULL && tid <= 0x01FFFFFFFFFFFFFFULL) {
                            return tid;
                        }
                    }
                }
            }
        }
    }

    // Fallback: look anywhere in the filename for a 16-hex string starting with 0100
    for (size_t i = 0; i + 16 <= name.size(); ++i) {
        if (name[i] == '0' && name[i+1] == '1' && name[i+2] == '0' && name[i+3] == '0') {
            bool leftBoundary = (i == 0) || !std::isxdigit(static_cast<unsigned char>(name[i - 1]));
            bool rightBoundary = (i + 16 == name.size()) || !std::isxdigit(static_cast<unsigned char>(name[i + 16]));
            if (leftBoundary && rightBoundary && is16Hex(&name[i])) {
                uint64_t tid = toHex64(&name[i]);
                if (tid >= 0x0100000000000000ULL && tid <= 0x01FFFFFFFFFFFFFFULL) {
                    return tid;
                }
            }
        }
    }

    return 0;
}

// Parses version like [v65536], (v131072), _v0 from filename
inline uint32_t parseVersionFromFileName(const std::string& name) {
    if (name.empty()) return 0;

    for (size_t i = 0; i < name.size(); ++i) {
        char delim = name[i];
        if (delim == '[' || delim == '(' || delim == '_' || delim == ' ') {
            if (i + 2 < name.size() && (name[i + 1] == 'v' || name[i + 1] == 'V') &&
                std::isdigit(static_cast<unsigned char>(name[i + 2]))) {
                size_t start = i + 2;
                size_t end = start;
                while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) {
                    end++;
                }
                if (end > start) {
                    try {
                        std::string num_str = name.substr(start, end - start);
                        return static_cast<uint32_t>(std::stoul(num_str));
                    } catch (...) {}
                }
            }
        }
    }
    return 0;
}

inline PackageInfo classifyPackage(const std::string& path, uint64_t knownAppId = 0) {
    PackageInfo info;
    std::string filename = path;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool isNsp = lower.ends_with(".nsp") || lower.ends_with(".nsz");
    bool isXci = lower.ends_with(".xci") || lower.ends_with(".xcz");
    if (!isNsp && !isXci) {
        info.isPackage = false;
        return info;
    }
    info.isPackage = true;

    info.titleId = parseTitleIdFromFileName(filename);
    info.version = parseVersionFromFileName(filename);

    if (info.titleId != 0) {
        info.baseTitleId = info.titleId & ~0x1FFFULL;
        uint64_t subId = info.titleId & 0x1FFFULL;
        if (subId == 0) {
            info.kind = PackageKind::BaseGame;
        } else if (subId == 0x800) {
            info.kind = PackageKind::Update;
        } else {
            info.kind = PackageKind::DLC;
        }
    } else {
        if (knownAppId != 0) {
            info.baseTitleId = knownAppId & ~0x1FFFULL;
        }
        bool isUpd = (lower.find("upd") != std::string::npos ||
                      lower.find("patch") != std::string::npos ||
                      lower.find("800]") != std::string::npos ||
                      lower.find("800)") != std::string::npos ||
                      (info.version > 0));
        bool isDlc = (lower.find("dlc") != std::string::npos ||
                      lower.find("addon") != std::string::npos);

        if (isUpd) {
            info.kind = PackageKind::Update;
            if (info.baseTitleId != 0) info.titleId = info.baseTitleId | 0x800ULL;
        } else if (isDlc) {
            info.kind = PackageKind::DLC;
        } else {
            info.kind = PackageKind::BaseGame;
            if (info.baseTitleId != 0) info.titleId = info.baseTitleId;
        }
    }

    return info;
}

inline std::string cleanTitle(const std::string& title) {
    size_t pos = title.find('[');
    if (pos == std::string::npos) return title;
    std::string cleaned = title.substr(0, pos);
    while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.back()))) {
        cleaned.pop_back();
    }
    return cleaned.empty() ? title : cleaned;
}

inline std::vector<std::string> extractWords(const std::string& text) {
    std::vector<std::string> words;
    std::string cur;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c >= 0x80) {
            cur += static_cast<char>(std::tolower(c));
        } else if (!cur.empty()) {
            if (cur.size() >= 3) words.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 3) words.push_back(cur);
    return words;
}

inline bool releaseNameMatches(const std::string& installedName, const std::string& releaseTitle) {
    auto a = extractWords(cleanTitle(installedName));
    auto b = extractWords(cleanTitle(releaseTitle));
    if (a.empty() || b.empty()) return false;
    size_t matchCount = 0;
    for (const auto& w1 : a) {
        for (const auto& w2 : b) {
            if (w1 == w2) {
                matchCount++;
                break;
            }
        }
    }
    if (a.size() == 1) return matchCount >= 1;
    return matchCount >= std::min<size_t>(2, a.size());
}

} // namespace goonies
