#pragma once

#include <string>
#include <switch.h>

namespace goonies {

class CleanupHelper {
public:
    static bool cleanSystem(std::string& errorOut, u64& outFreedBytes, int& outDeletedTickets);
};

} // namespace goonies
