#pragma once

#include <switch.h>
#include "location.hpp"

namespace GooniesInstaller::backend_usbdvd {

Result MountAll();
void UnmountAll();

bool GetMountPoint(location::StdioEntry& out);

} // namespace GooniesInstaller::backend_usbdvd
