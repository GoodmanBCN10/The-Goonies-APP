#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include "install/install_backend.hpp"
#include "install/package_stream.hpp"

namespace Installer {

class Core {
public:
    Core();
    ~Core();

    bool StartInstallation(const std::string& filename);
    bool WriteData(const void* data, size_t size);
    void FinishInstallation();
    void AbortInstallation();
    
    bool IsFinished() const { return m_all_finished; }
    bool HasError() const { return m_error; }
    Result GetErrorCode() const { return m_last_error_code; }
    u64 GetBytesWritten() const { return m_mtp_bytes_written; }
    u64 GetFreedBytes() const { return m_freed_bytes; }
    std::string GetFinishedFilename() { 
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finished_files.empty()) return "";
        std::string fn = m_finished_files.front();
        m_finished_files.erase(m_finished_files.begin());
        return fn;
    }

    static void LockConsole();
    static void UnlockConsole();
    static void SafePrintf(const char* format, ...);

private:
    std::mutex m_mutex;
    
    bool m_all_finished = true;
    bool m_error = false;
    Result m_last_error_code = 0;
    u64 m_mtp_bytes_written = 0;
    u64 m_freed_bytes = 0;
    
    std::vector<std::string> m_finished_files;
    
    std::string m_current_filename;
    std::unique_ptr<pipensx::install::InstallBackend> m_backend;
    std::unique_ptr<pipensx::install::PackageStream> m_stream;
};

} // namespace Installer

