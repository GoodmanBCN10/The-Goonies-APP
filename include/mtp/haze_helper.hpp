#pragma once

#include <switch.h>
#include <functional>

namespace MTP {

using OnInstallStart = std::function<bool(const char*, size_t)>;
using OnInstallWrite = std::function<bool(const void*, size_t)>;
using OnInstallClose = std::function<void()>;

bool Init();
void Exit();
bool IsInit();

void InitInstallMode(const OnInstallStart& on_start, const OnInstallWrite& on_write, const OnInstallClose& on_close);
void DisableInstallMode();

} // namespace MTP
