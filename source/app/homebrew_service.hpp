#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace goonies {

struct HomebrewTitle {
    std::string path;
    std::string name;
    std::string author;
    std::vector<u8> icon;
    NacpStruct nacp;
    bool is_folder = false;
};

class HomebrewService {
public:
    HomebrewService();

    bool refresh(const std::string& path, std::string& error);
    std::vector<HomebrewTitle> titles() const;

private:
    std::vector<HomebrewTitle> titles_;
};

} // namespace goonies
