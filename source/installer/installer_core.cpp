#include "installer/installer_core.hpp"
#include <stdio.h>
#include <cstring>
#include <algorithm>
#include <stdarg.h>
#include <borealis/core/logger.hpp>

#include "yati/yati.hpp"

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
}

Core::~Core() {
    AbortInstallation();
}

bool Core::StartInstallation(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_current_filename = filename;
    m_mtp_bytes_written = 0;
    m_freed_bytes = 0;
    m_all_finished = false;
    m_error = false;
    m_last_error_code = 0;
    
    // Check if it's an XCI/XCZ
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
    if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".xci" || lower.substr(lower.size() - 4) == ".xcz")) {
        m_is_xci = true;
        
        m_exit_requested = false;
        while (!m_data_queue.empty()) m_data_queue.pop();
        m_data_queue_size = 0;
        
        m_installer_thread = std::thread(&Core::YatiInstallerThreadEntry, this);
        SafePrintf("Installer Core: Queued XCI for %s\n", filename.c_str());
        return true;
    }
    
    m_is_xci = false;
    
    pipensx::install::InstallStorageTarget target = pipensx::install::InstallStorageTarget::SdCard;
    m_backend = pipensx::install::createInstallBackend("sdmc:/switch/thegoonies", target);
    
    if (!m_backend->beginPackage("local_install", filename)) {
        m_error = true;
        m_last_error_code = 1;
        SafePrintf("Installer Core: Failed to beginPackage\n");
        return false;
    }

    pipensx::install::PackageCallbacks cb;
    cb.beginFile = [this](const std::string& name, uint64_t size) {
        return m_backend->beginFile(name, size);
    };
    cb.setFileSize = [this](uint64_t size) {
        return m_backend->setFileSize(size);
    };
    cb.writeFile = [this](const uint8_t* data, size_t size) {
        return m_backend->writeFile(data, size);
    };
    cb.endFile = [this]() {
        return m_backend->endFile();
    };
    cb.skipFile = [this](const std::string& name) {
        return m_backend->shouldSkipFile(name);
    };

    bool compressed = (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".nsz");
    m_stream = std::make_unique<pipensx::install::PackageStream>(compressed, cb, "local_install");

    SafePrintf("Installer Core: Queued NSP for %s\n", filename.c_str());
    return true;
}

bool Core::WriteData(const void* data, size_t size) {
    if (size == 0) return true;
    if (m_is_xci) {
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
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_error || !m_stream) return false;

        if (!m_stream->write(static_cast<const uint8_t*>(data), size)) {
            m_error = true;
            m_last_error_code = 2;
            SafePrintf("Installer Core: stream.write failed: %s\n", m_stream->error().c_str());
            return false;
        }

        m_mtp_bytes_written += size;
        return true;
    }
}

void Core::FinishInstallation() {
    if (m_is_xci) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_data_queue.push(std::vector<u8>()); // EOF
            m_cv.notify_all();
        }
        
        if (m_installer_thread.joinable()) {
            m_installer_thread.join();
        }
        
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_error) {
            m_finished_files.push_back(m_current_filename);
            m_all_finished = true;
            SafePrintf("Installer Core: Finished XCI writing to MTP.\n");
        }
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_error || !m_stream) return;

        if (!m_stream->finish()) {
            m_error = true;
            m_last_error_code = 3;
            SafePrintf("Installer Core: stream.finish failed: %s\n", m_stream->error().c_str());
            return;
        }

        bool alreadyInstalled = false;
        if (!m_backend->commitPackage(alreadyInstalled)) {
            m_error = true;
            m_last_error_code = 4;
            SafePrintf("Installer Core: commitPackage failed: %s\n", m_backend->error().c_str());
            return;
        }

        m_finished_files.push_back(m_current_filename);
        m_all_finished = true;
        m_stream.reset();
        m_backend.reset();
        SafePrintf("Installer Core: Finished NSP writing to MTP.\n");
    }
}

void Core::AbortInstallation() {
    if (m_is_xci) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_exit_requested = true;
            m_error = true;
            m_last_error_code = 100;
            m_cv.notify_all();
        }
        if (m_installer_thread.joinable()) {
            m_installer_thread.join();
        }
    } else {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_error && !m_all_finished && m_backend) {
            m_backend->rollbackPackage();
        }
        m_error = true;
        m_last_error_code = 100;
        m_stream.reset();
        m_backend.reset();
    }
}

Result Core::YatiStreamSource::ReadChunk(void* buf, s64 size, u64* bytes_read) {
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

void Core::YatiInstallerThreadEntry() {
    brls::Logger::info("YatiInstallerThreadEntry started for {}", m_current_filename);
    
    GooniesInstaller::yati::ConfigOverride config;
    config.sd_card_install = true;
    config.lower_system_version = false;
    config.lower_master_key = true;
    config.convert_to_standard_crypto = true;
    config.convert_to_common_ticket = true;
    
    YatiStreamSource source(this);
    Result rc = GooniesInstaller::yati::InstallFromSource(&source, fs::FsPath{m_current_filename.c_str()}, config, &m_freed_bytes);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (R_FAILED(rc)) {
        brls::Logger::error("YatiInstallerThreadEntry Error: 0x{:08x}", rc);
        m_error = true;
        m_last_error_code = rc;
    } else {
        brls::Logger::info("YatiInstallerThreadEntry Success!");
    }
    
    // Drain any remaining chunks to avoid memory leaks
    while (!m_data_queue.empty()) {
        bool is_eof = m_data_queue.front().empty();
        m_data_queue_size -= m_data_queue.front().size();
        m_data_queue.pop();
        if (is_eof) break;
    }
    m_cv.notify_all();
}

} // namespace Installer
