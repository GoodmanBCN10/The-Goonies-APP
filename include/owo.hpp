#pragma once

#include <switch.h>
#include <string>
#include <vector>
#include "frontend/progress_box.hpp"

namespace GooniesInstaller {

struct OwoConfig {
    std::string nro_path;
    std::string args{};
    std::string name{};
    std::string author{};
    NacpStruct nacp;
    std::vector<u8> icon;
    std::vector<u8> logo;
    std::vector<u8> gif;

    std::vector<u8> program_nca{};
};

auto install_forwarder(OwoConfig& config, NcmStorageId storage_id) -> Result;
auto install_forwarder(frontend::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result;

} // namespace GooniesInstaller
