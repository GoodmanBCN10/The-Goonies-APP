#include "installer/installer_core.hpp"
#include <stdio.h>
#include <cstring>
#include <algorithm>
#include <stdarg.h>

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

    SafePrintf("Installer Core: Queued for %s\n", filename.c_str());
    return true;
}

bool Core::WriteData(const void* data, size_t size) {
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

void Core::FinishInstallation() {
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
    SafePrintf("Installer Core: Finished writing to MTP.\n");
}

void Core::AbortInstallation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_error && !m_all_finished && m_backend) {
        m_backend->rollbackPackage();
    }
    m_error = true;
    m_last_error_code = 100;
    m_stream.reset();
    m_backend.reset();
}

} // namespace Installer
