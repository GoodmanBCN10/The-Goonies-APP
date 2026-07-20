#pragma once

#include <switch.h>
#include <vapours/results.hpp>
#include <functional>
#include <algorithm>

namespace sphaira::thread {

using Result = ams::Result;

static constexpr u64 BUFFER_SIZE_READ = 1024*1024*1;
static constexpr u64 BUFFER_SIZE_WRITE = 1024*1024*1;

enum class Mode {
    // default, always multi-thread.
    MultiThreaded,
    // always single-thread.
    SingleThreaded,
    // check buffer size, if smaller, single thread.
    SingleThreadedIfSmaller,
};

using ReadCallback = std::function<Result(void* data, s64 off, s64 size, u64* bytes_read)>;
using WriteCallback = std::function<Result(const void* data, s64 off, s64 size)>;

// reads data from rfunc into wfunc.
Result Transfer(s64 size, const ReadCallback& rfunc, const WriteCallback& wfunc, u64 buffer_size, Mode mode = Mode::MultiThreaded);

} // namespace sphaira::thread
