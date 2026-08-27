#pragma once
#include <string>

namespace goonies::ui {

struct PortEntry {
    std::string id;
    std::string name;
    std::string category;
    std::string author;
    std::string version;
    std::string url;
    std::string icon;
    std::string description;
};

} // namespace goonies::ui
