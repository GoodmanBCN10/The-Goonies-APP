#pragma once

#include <vector>
#include <span>
#include <switch.h>
#include "fs.hpp"

namespace GooniesInstaller {

struct ImageResult {
    std::vector<u8> data;
    int w, h;
};

enum ImageFlag {
    ImageFlag_None = 0,
    // set this if the image is a jpeg, will use oss-nvjpg to load.
    ImageFlag_JPEG = 1 << 0,
};

auto ImageLoadFromMemory(std::span<const u8> data, u32 flags = ImageFlag_None) -> ImageResult;
auto ImageLoadFromFile(const fs::FsPath& file, u32 flags = ImageFlag_None) -> ImageResult;
auto ImageResize(std::span<const u8> data, int inx, int iny, int outx, int outy) -> ImageResult;
auto ImageConvertToJpg(std::span<const u8> data, int x, int y) -> ImageResult;

} // namespace GooniesInstaller
