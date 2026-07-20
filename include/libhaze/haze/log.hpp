// Code is taken from Sphaira.
#pragma once

#define haze_USE_LOG 1

#include <stdarg.h>

namespace haze {

#if haze_USE_LOG
bool log_file_init();
void log_file_exit();
bool log_is_init();

void log_write(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
void log_write_arg(const char* s, va_list* v);
#else
inline bool log_file_init() {
    return true;
}

inline void log_file_exit() {

}

inline bool log_is_init() {
    return false;
}

inline void log_write(const char* s, ...) {
    (void)s;
}

inline void log_write_arg(const char* s, va_list* v) {
    (void)s;
    (void)v;
}
#endif

} // namespace haze
