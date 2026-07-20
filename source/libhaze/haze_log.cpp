#include "haze/log.hpp"
#include "vapours/defines.hpp"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <atomic>
#include <switch.h>

#if haze_USE_LOG
namespace haze {
namespace {

struct ScopedMutex {
    ScopedMutex(Mutex* mutex) : m_mutex{mutex} {
        mutexLock(m_mutex);
    }
    ~ScopedMutex() {
        mutexUnlock(m_mutex);
    }

    ScopedMutex(const ScopedMutex&) = delete;
    void operator=(const ScopedMutex&) = delete;

private:
    Mutex* const m_mutex;
};

#define SCOPED_MUTEX(mutex) ScopedMutex ANONYMOUS_VARIABLE(_scoped_mutex_){mutex}

constexpr const char* logpath = "/libhaze_log.txt";

#define BUFFERED_LOG 1

#if BUFFERED_LOG
std::FILE* g_file = nullptr;
#else
std::atomic_bool g_file_open{};
#endif
Mutex g_mutex;

void log_write_arg_internal(const char* s, std::va_list* v) {
    const auto t = std::time(nullptr);
    const auto tm = std::localtime(&t);

    char buf[512];
    const auto len = std::snprintf(buf, sizeof(buf), "[%02u:%02u:%02u] -> ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    std::vsnprintf(buf + len, sizeof(buf) - len, s, *v);

    SCOPED_MUTEX(&g_mutex);
#if BUFFERED_LOG
    if (g_file) {
        std::fprintf(g_file, "%s", buf);
    }
#else
    if (g_file_open) {
        auto file = std::fopen(logpath, "a");
        if (file) {
            std::fprintf(file, "%s", buf);
            std::fclose(file);
        }
    }
#endif
}

} // namespace

auto log_file_init() -> bool {
    SCOPED_MUTEX(&g_mutex);

#if BUFFERED_LOG
    if (g_file) {
        return true;
    }

    g_file = std::fopen(logpath, "w");
    if (g_file) {
        // increase buffer size to reduce write frequency.
        setvbuf(g_file, nullptr, _IOFBF, 1024 * 64);
        return true;
    }
    return false;
#else
    if (g_file_open) {
        return false;
    }

    auto file = std::fopen(logpath, "w");
    if (file) {
        g_file_open = true;
        std::fclose(file);
        return true;
    }

    return false;
#endif
}

void log_file_exit() {
    SCOPED_MUTEX(&g_mutex);

#if BUFFERED_LOG
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
#else
    if (g_file_open) {
        g_file_open = false;
    }
#endif
}

bool log_is_init() {
#if BUFFERED_LOG
    return g_file != nullptr;
#else
    return g_file_open;
#endif
}

void log_write(const char* s, ...) {
    if (!log_is_init()) {
        return;
    }

    std::va_list v{};
    va_start(v, s);
    log_write_arg_internal(s, &v);
    va_end(v);
}

void log_write_arg(const char* s, va_list* v) {
    if (!log_is_init()) {
        return;
    }

    log_write_arg_internal(s, v);
}

} // namespace haze

#endif
