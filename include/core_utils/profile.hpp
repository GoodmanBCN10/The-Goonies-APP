#pragma once

#include "frontend/types.hpp"
#include "log.hpp"

namespace GooniesInstaller::core_utils {

struct ScopedTimestampProfile final {
    ScopedTimestampProfile(const std::string& name) : m_name{name} {

    }

    ~ScopedTimestampProfile() {
        Log();
    }

    void Log() {
        log_write("\t[%s] time taken: %.2fs %.2fms\n", m_name.c_str(), m_ts.GetSecondsD(), m_ts.GetMsD());
    }

private:
    const std::string m_name;
    TimeStamp m_ts{};
};

#define SCOPED_TIMESTAMP(name) GooniesInstaller::core_utils::ScopedTimestampProfile ANONYMOUS_VARIABLE(SCOPE_PROFILE_STATE_){name};

} // namespace GooniesInstaller::core_utils
