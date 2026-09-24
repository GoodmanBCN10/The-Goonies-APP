#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "install/install_backend.hpp"
#include "install/package_stream.hpp"
#include "yati/source/stream.hpp"

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
    std::condition_variable m_cv;
    
    bool m_all_finished = true;
    bool m_error = false;
    Result m_last_error_code = 0;
    u64 m_mtp_bytes_written = 0;
    u64 m_freed_bytes = 0;
    
    std::vector<std::string> m_finished_files;
    
    std::string m_current_filename;
    bool m_is_xci = false;

    // NSP/NSZ Engine
    std::unique_ptr<pipensx::install::InstallBackend> m_backend;
    std::unique_ptr<pipensx::install::PackageStream> m_stream;

    // XCI/XCZ Engine (Yati)
    std::queue<std::vector<u8>> m_data_queue; // empty vector = EOF
    u64 m_data_queue_size = 0;
    std::thread m_installer_thread;
    bool m_exit_requested = false;

    class YatiStreamSource : public GooniesInstaller::yati::source::Stream {
    public:
        YatiStreamSource(Core* core) : m_core(core) {}
        ~YatiStreamSource() override = default;
        Result ReadChunk(void* buf, s64 size, u64* bytes_read) override;
    private:
        Core* m_core;
        std::vector<u8> m_current_chunk;
        size_t m_current_chunk_pos = 0;
    };
    
    void YatiInstallerThreadEntry();
};

} // namespace Installer
