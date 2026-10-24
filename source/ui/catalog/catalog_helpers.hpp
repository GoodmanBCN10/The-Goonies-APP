#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/game_metadata_service.hpp"
#include "ui/common/async_image.hpp"

namespace goonies::ui {

// ---------------------------------------------------------------------------
// Shared catalog helpers (used by both the list and the detail page)
// ---------------------------------------------------------------------------

inline std::string catalogLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

inline std::string classifyResolveFailure(const std::string& error) {
    std::string lower = catalogLower(error);
    if (lower.find("not registered") != std::string::npos ||
        lower.find("stale") != std::string::npos)
        return "Stale";
    if (lower.find("metadata") != std::string::npos)
        return "No metadata";
    if (lower.find("no usable peers") != std::string::npos ||
        lower.find("no peers") != std::string::npos)
        return "No peers";
    return "Resolve failed";
}

inline std::string badgeForCatalogHealth(const CatalogEntry& entry) {
    switch (entry.health) {
        case goonies::CatalogHealth::Ok:
            return entry.metadataOk ? "Fresh" : "Checked";
        case goonies::CatalogHealth::NoPeers:
            return "No peers";
        case goonies::CatalogHealth::MetadataTimeout:
            return "No metadata";
        case goonies::CatalogHealth::TrackerNotRegistered:
        case goonies::CatalogHealth::Dead:
            return "Dead";
        case goonies::CatalogHealth::Replaced:
            return "Replaced";
        case goonies::CatalogHealth::Unknown:
            break;
    }
    return entry.catalogGeneratedAt || entry.sourceUpdatedAt
         ? "Unchecked" : std::string();
}

inline std::string joinStrings(const std::vector<std::string>& values,
                        const char* separator) {
    std::string out;
    for (const std::string& value : values) {
        if (value.empty())
            continue;
        if (!out.empty())
            out += separator;
        out += value;
    }
    return out;
}

inline std::string shortDescription(const std::string& value) {
    if (value.size() <= 900)
        return value;
    return value.substr(0, 900) + "...";
}

// Append a freshly created async image to a box (banner / screenshot on the
// detail page). Reuses loadImageInto for the disk-cached fetch.
inline void appendAsyncImage(brls::Box* parent, GameMetadataService* service,
                      const std::string& url, float height) {
    if (!service || url.empty())
        return;
    auto* image = new AsyncRgbaImage();
    image->setHeight(height);
    image->setMarginBottom(12);
    image->setAlignSelf(brls::AlignSelf::CENTER);
    image->setScalingType(brls::ImageScalingType::FIT);
    image->setClipsToBounds(false);  // no letterbox edge bands
    loadImageInto(image, service, url);
    parent->addView(image);
}
}  // namespace goonies::ui
