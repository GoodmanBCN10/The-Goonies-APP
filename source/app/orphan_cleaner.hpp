#pragma once

#include <string>

namespace pipensx {
class InstalledTitleService;

class OrphanCleaner {
public:
    static bool cleanOrphans(InstalledTitleService* installed, std::string& errorOut, std::string& resultOut);
};

} // namespace pipensx
