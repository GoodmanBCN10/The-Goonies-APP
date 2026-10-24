#pragma once

#include <string>

namespace goonies {
class InstalledTitleService;

class OrphanCleaner {
public:
    static bool cleanOrphans(InstalledTitleService* installed, std::string& errorOut, std::string& resultOut);
};

} // namespace goonies
