#pragma once
#include <string>
#include <vector>
#include <switch.h>

namespace GooniesInstaller {
    bool ReadNroAsset(const std::string& path, std::vector<u8>& icon, std::string& name, std::string& author, NacpStruct& out_nacp);
}
