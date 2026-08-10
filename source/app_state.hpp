#pragma once
#include <atomic>

#include <curl/curl.h>

extern std::atomic<bool> g_appExiting;

inline int global_curl_xferinfo(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (g_appExiting.load()) return 1; // Abort instantly
    return 0;
}
