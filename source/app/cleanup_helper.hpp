#pragma once

#include <string>
#include <switch.h>

namespace pipensx {

class CleanupHelper {
public:
    static bool cleanSystem(std::string& errorOut, u64& outFreedBytes, int& outDeletedTickets);
};

} // namespace pipensx
