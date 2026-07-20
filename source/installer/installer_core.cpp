#include "installer/installer_core.hpp"
#include <stdio.h>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <stdarg.h>
#include <borealis.hpp>
#include <borealis/core/logger.hpp>

#include "yati/yati.hpp"
#include <switch.h>

namespace Installer {

static std::mutex g_console_mutex;
void Core::LockConsole() { g_console_mutex.lock(); }
void Core::UnlockConsole() { g_console_mutex.unlock(); }
void Core::SafePrintf(const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_console_mutex);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

Core::Core() {
    m_installer_thread = std::thread(&Core::InstallerThreadEntry, this);
}

Core::~Core() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exit_requested = true;
        m_cv.notify_all();
    }
    if (m_installer_thread.joinable()) {
        m_installer_thread.join();
    }
}

bool Core::StartInstallation(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_file_queue.push_back(filename);
    m_mtp_bytes_written = 0;
    m_all_finished = false;
    m_error = false;
    m_last_error_code = 0;
    SafePrintf("Installer Core: Queued for %s\n", filename.c_str());
    m_cv.notify_all();
    return true;
}

bool Core::WriteData(const void* data, size_t size) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    // Wait if queue is getting too big (prevent OOM)
    const u64 MAX_QUEUE_SIZE = 32 * 1024 * 1024; // 32 MB
    m_cv.wait(lock, [this, size]() {
        return m_error || m_exit_requested || (m_data_queue_size + size <= MAX_QUEUE_SIZE);
    });

    if (m_error || m_exit_requested) return false;



    const u8* u8_data = static_cast<const u8*>(data);
    m_data_queue.push(std::vector<u8>(u8_data, u8_data + size));
    m_data_queue_size += size;
    m_mtp_bytes_written += size;
    m_cv.notify_all();
    return true;
}

void Core::FinishInstallation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data_queue.push(std::vector<u8>()); // EOF
    SafePrintf("Installer Core: Finished writing to MTP.\n");
    m_cv.notify_all();
}

void Core::AbortInstallation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_error = true;
    m_last_error_code = MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen);
    m_cv.notify_all();
}

Result Core::MtpSource::ReadChunk(void* buf, s64 size, u64* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    
    if (m_current_chunk_pos >= m_current_chunk.size()) {
        std::unique_lock<std::mutex> lock(m_core->m_mutex);
        
        m_core->m_cv.wait(lock, [this]() {
            return m_core->m_error || m_core->m_exit_requested || !m_core->m_data_queue.empty();
        });
        
        if (m_core->m_error || m_core->m_exit_requested) return MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen);
        
        m_current_chunk = std::move(m_core->m_data_queue.front());
        m_core->m_data_queue.pop();
        m_core->m_data_queue_size -= m_current_chunk.size();
        m_current_chunk_pos = 0;
        
        m_core->m_cv.notify_all();
        
        if (m_current_chunk.empty()) {
            return 0; // EOF
        }
    }
    
    size_t to_read = std::min((size_t)size, m_current_chunk.size() - m_current_chunk_pos);
    std::memcpy(buf, m_current_chunk.data() + m_current_chunk_pos, to_read);
    m_current_chunk_pos += to_read;
    
    if (bytes_read) *bytes_read = to_read;
    return 0;
}

void Core::InstallerThreadEntry() {
    brls::Logger::info("InstallerThreadEntry started");
    while (!m_exit_requested) {
        std::string filename;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return m_exit_requested || !m_file_queue.empty();
            });
            if (m_exit_requested) break;
            
            filename = m_file_queue.front();
            m_file_queue.erase(m_file_queue.begin());
        }
        
        brls::Logger::info("InstallerThreadEntry popping file: {}", filename);
        
        GooniesInstaller::yati::ConfigOverride config;
        config.sd_card_install = true;
        config.lower_system_version = false;
        config.lower_master_key = true;
        config.convert_to_standard_crypto = true;
        config.convert_to_common_ticket = true;
        
        MtpSource source(this);
        brls::Logger::info("InstallerThreadEntry calling InstallFromSource");
        Result rc = GooniesInstaller::yati::InstallFromSource(&source, fs::FsPath{filename.c_str()}, config, &m_freed_bytes);
        brls::Logger::info("InstallerThreadEntry InstallFromSource finished with rc: 0x{:08x}", rc);
        
        std::lock_guard<std::mutex> lock(m_mutex);
        if (R_FAILED(rc)) {
            brls::Logger::error("InstallerThreadEntry Error: 0x{:08x}", rc);
            m_error = true;
            m_last_error_code = rc;
        } else {
            brls::Logger::info("InstallerThreadEntry Success!");
        }
        
        m_finished_files.push_back(filename);
        if (m_file_queue.empty()) {
            m_all_finished = true;
        }
        
        // Drain any remaining chunks for this file to avoid desync
        while (!m_data_queue.empty()) {
            bool is_eof = m_data_queue.front().empty();
            m_data_queue_size -= m_data_queue.front().size();
            m_data_queue.pop();
            if (is_eof) break;
        }
        m_cv.notify_all();
        
        if (m_error) break;
    }
}

} // namespace Installer
