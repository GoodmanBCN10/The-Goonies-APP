#pragma once

#include <thread>
#include <functional>
#include <memory>
#include <type_traits>

#ifdef __SWITCH__
#include <switch.h>
#include <pthread.h>
#endif

namespace nx {

#ifdef __SWITCH__
class thread {
public:
    thread() noexcept : m_handle(0), m_joinable(false) {}

    template<class Function, class... Args>
    explicit thread(Function&& f, Args&&... args) {
        auto bound_func = std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
        auto* func_ptr = new decltype(bound_func)(std::move(bound_func));

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        // Set 1MB stack size for libcurl / SSL on switch
        pthread_attr_setstacksize(&attr, 1024 * 1024);

        int rc = pthread_create(&m_handle, &attr, [](void* arg) -> void* {
            auto* func = static_cast<decltype(func_ptr)>(arg);
            (*func)();
            delete func;
            return nullptr;
        }, func_ptr);
        
        pthread_attr_destroy(&attr);

        if (rc != 0) {
            delete func_ptr;
            m_joinable = false;
        } else {
            m_joinable = true;
        }
    }

    ~thread() {
        if (joinable()) {
            std::terminate();
        }
    }

    thread(thread&& other) noexcept : m_handle(other.m_handle), m_joinable(other.m_joinable) {
        other.m_handle = 0;
        other.m_joinable = false;
    }

    thread& operator=(thread&& other) noexcept {
        if (joinable()) {
            std::terminate();
        }
        m_handle = other.m_handle;
        m_joinable = other.m_joinable;
        other.m_handle = 0;
        other.m_joinable = false;
        return *this;
    }

    bool joinable() const noexcept {
        return m_joinable;
    }

    void join() {
        if (joinable()) {
            pthread_join(m_handle, nullptr);
            m_joinable = false;
        }
    }

    void detach() {
        if (joinable()) {
            pthread_detach(m_handle);
            m_joinable = false;
        }
    }

private:
    pthread_t m_handle;
    bool m_joinable;
};

#else
// Fallback to std::thread on PC
using thread = std::thread;
#endif

} // namespace nx
