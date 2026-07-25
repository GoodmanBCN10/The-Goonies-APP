#pragma once

#include <string>
#include <vector>

namespace pipensx::linkuser {

// Starts link user process, returns error message or empty string on success
std::string linkAccounts();

// Starts unlink user process, returns error message or empty string on success
std::string unlinkAccounts();

// Returns a list of backup folders (directories in the backup path)
std::vector<std::string> getBackups();

// Restores from a specific backup folder, returns error message or empty string on success
std::string restoreBackup(const std::string& folderName);

// Reboots the system using bpc service
void rebootSystem();

extern bool g_shouldReboot;

} // namespace pipensx::linkuser
