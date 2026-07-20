#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace pipensx {

struct HomebrewTitle {
    std::string path;
    std::string name;
    std::string author;
    std::vector<u8> icon;
    NacpStruct nacp;
};

class HomebrewService {
public:
    HomebrewService();

    bool refresh(std::string& error);
    std::vector<HomebrewTitle> titles() const;

private:
    std::vector<HomebrewTitle> titles_;
};

} // namespace pipensx
