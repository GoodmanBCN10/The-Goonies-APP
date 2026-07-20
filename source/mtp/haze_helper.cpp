#include "mtp/haze_helper.hpp"

#include <haze.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <filesystem>
namespace MTP {
namespace {

struct InstallSharedData {
    std::mutex mutex;
    std::string current_file;
    size_t current_file_size = 0;

    OnInstallStart on_start;
    OnInstallWrite on_write;
    OnInstallClose on_close;

    bool in_progress = false;
    bool enabled = false;
};

std::atomic_bool g_should_exit = false;
bool g_is_running = false;
std::mutex g_mutex;
InstallSharedData g_shared_data;

const char* SUPPORTED_EXT[] = {
    ".nsp", ".xci", ".nsz", ".xcz",
};

void on_thing() {
    std::lock_guard<std::mutex> lock(g_shared_data.mutex);

    if (!g_shared_data.in_progress) {
        if (!g_shared_data.current_file.empty()) {
            if (!g_shared_data.on_start || !g_shared_data.on_start(g_shared_data.current_file.c_str(), g_shared_data.current_file_size)) {
                g_shared_data.current_file.clear();
                g_shared_data.current_file_size = 0;
            } else {
                g_shared_data.in_progress = true;
            }
        }
    }
}

struct FsProxyBase : haze::FileSystemProxyImpl {
    FsProxyBase(const char* name, const char* display_name) : m_name(name), m_display_name(display_name) {}

    const char* GetName() const override {
        return m_name.c_str();
    }
    const char* GetDisplayName() const override {
        return m_display_name.c_str();
    }

protected:
    const std::string m_name;
    const std::string m_display_name;
};

struct FsProxyVfs : FsProxyBase {
    struct File {
        u64 index{};
        haze::FileOpenMode mode{};
    };

    struct Dir {
        u64 pos{};
    };

    using FsProxyBase::FsProxyBase;
    virtual ~FsProxyVfs() = default;

    auto GetFileName(const char* s) -> const char* {
        const auto file_name = std::strrchr(s, '/');
        if (!file_name || file_name[1] == '\0') {
            return nullptr;
        }
        return file_name + 1;
    }

    virtual Result GetEntryType(const char *path, haze::FileAttrType *out_entry_type) {
        if (std::strcmp(path, "/") == 0 || std::strcmp(path, "") == 0) {
            *out_entry_type = haze::FileAttrType_DIR;
            return 0; // R_SUCCEED
        } else {
            const auto file_name = GetFileName(path);
            if(!file_name) return 0x202; // FsError_PathNotFound

            auto it = std::find_if(m_entries.begin(), m_entries.end(), [file_name](auto& e){
                return !strcasecmp(file_name, e.name);
            });
            if(it == m_entries.end()) return 0x202; // FsError_PathNotFound

            *out_entry_type = haze::FileAttrType_FILE;
            return 0; // R_SUCCEED
        }
    }

    virtual Result CreateFile(const char* path, s64 size) {
        const auto file_name = GetFileName(path);
        if(!file_name) return 0x202;

        auto it = std::find_if(m_entries.begin(), m_entries.end(), [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        if(it != m_entries.end()) return 0x402; // PathAlreadyExists

        FsDirectoryEntry entry{};
        std::strncpy(entry.name, file_name, sizeof(entry.name) - 1);
        entry.type = FsDirEntryType_File;
        entry.file_size = size;

        m_entries.push_back(entry);
        return 0;
    }

    virtual Result DeleteFile(const char* path) {
        const auto file_name = GetFileName(path);
        if(!file_name) return 0x202;

        auto it = std::find_if(m_entries.begin(), m_entries.end(), [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        if(it == m_entries.end()) return 0x202;

        m_entries.erase(it);
        return 0;
    }

    virtual Result RenameFile(const char *old_path, const char *new_path) {
        const auto file_name = GetFileName(old_path);
        if(!file_name) return 0x202;

        auto it = std::find_if(m_entries.begin(), m_entries.end(), [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        if(it == m_entries.end()) return 0x202;

        const auto file_name_new = GetFileName(new_path);
        if(!file_name_new) return 0x202;

        auto new_it = std::find_if(m_entries.begin(), m_entries.end(), [file_name_new](auto& e){
            return !strcasecmp(file_name_new, e.name);
        });
        if(new_it != m_entries.end()) return 0x402;

        std::strncpy(it->name, file_name_new, sizeof(it->name) - 1);
        return 0;
    }

    virtual Result OpenFile(const char *path, haze::FileOpenMode mode, haze::File *out_file) {
        const auto file_name = GetFileName(path);
        if(!file_name) return 0x202;

        auto it = std::find_if(m_entries.begin(), m_entries.end(), [file_name](auto& e){
            return !strcasecmp(file_name, e.name);
        });
        if(it == m_entries.end()) return 0x202;

        auto f = new File();
        f->index = std::distance(m_entries.begin(), it);
        f->mode = mode;
        out_file->impl = f;
        return 0;
    }

    virtual Result GetFileSize(haze::File *file, s64 *out_size) {
        auto f = static_cast<File*>(file->impl);
        *out_size = m_entries[f->index].file_size;
        return 0;
    }

    virtual Result SetFileSize(haze::File *file, s64 size) {
        auto f = static_cast<File*>(file->impl);
        m_entries[f->index].file_size = size;
        return 0;
    }

    virtual Result ReadFile(haze::File *file, s64 off, void *buf, u64 read_size, u64 *out_bytes_read) {
        return MAKERESULT(Module_Libnx, 1); // Not implemented
    }

    virtual Result WriteFile(haze::File *file, s64 off, const void *buf, u64 write_size) {
        auto f = static_cast<File*>(file->impl);
        auto& e = m_entries[f->index];
        e.file_size = std::max<s64>(e.file_size, off + write_size);
        return 0;
    }

    virtual void CloseFile(haze::File *file) {
        auto f = static_cast<File*>(file->impl);
        if (f) {
            delete f;
            file->impl = nullptr;
        }
    }

    Result CreateDirectory(const char* path) override {
        return MAKERESULT(Module_Libnx, 1); // Not implemented
    }

    Result DeleteDirectoryRecursively(const char* path) override {
        return MAKERESULT(Module_Libnx, 1);
    }

    Result RenameDirectory(const char *old_path, const char *new_path) override {
        return MAKERESULT(Module_Libnx, 1);
    }

    Result OpenDirectory(const char *path, haze::Dir *out_dir) override {
        auto dir = new Dir();
        out_dir->impl = dir;
        return 0;
    }

    Result ReadDirectory(haze::Dir *d, s64 *out_total_entries, size_t max_entries, haze::DirEntry *buf) override {
        auto dir = static_cast<Dir*>(d->impl);
        max_entries = std::min<s64>(m_entries.size() - dir->pos, max_entries);

        for (size_t i = 0; i < max_entries; i++) {
            std::strncpy(buf[i].name, m_entries[dir->pos + i].name, sizeof(buf[i].name) - 1);
        }

        dir->pos += max_entries;
        *out_total_entries = max_entries;
        return 0;
    }

    Result GetDirectoryEntryCount(haze::Dir *d, s64 *out_count) override {
        *out_count = m_entries.size();
        return 0;
    }

    void CloseDirectory(haze::Dir *d) override {
        auto dir = static_cast<Dir*>(d->impl);
        if (dir) {
            delete dir;
            d->impl = nullptr;
        }
    }

    virtual Result GetTotalSpace(const char *path, s64 *out) {
        struct statvfs stat;
        if (statvfs("sdmc:/", &stat) == 0) {
            *out = (s64)stat.f_blocks * (s64)stat.f_frsize;
        } else {
            *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        }
        return 0;
    }

    virtual Result GetFreeSpace(const char *path, s64 *out) {
        struct statvfs stat;
        if (statvfs("sdmc:/", &stat) == 0) {
            *out = (s64)stat.f_bfree * (s64)stat.f_frsize;
        } else {
            *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        }
        return 0;
    }

    virtual Result GetEntryAttributes(const char *path, haze::FileAttr *out) {
        haze::FileAttrType type;
        Result rc = GetEntryType(path, &type);
        if(R_FAILED(rc)) return rc;

        out->type = type;
        if(type == haze::FileAttrType_FILE) {
            const auto file_name = GetFileName(path);
            auto it = std::find_if(m_entries.begin(), m_entries.end(), [file_name](auto& e){
                return !strcasecmp(file_name, e.name);
            });
            if(it != m_entries.end()) {
                out->size = it->file_size;
            }
        }
        return 0;
    }

protected:
    std::vector<FsDirectoryEntry> m_entries;
};

struct FsInstallProxy final : FsProxyVfs {
    using FsProxyVfs::FsProxyVfs;

    Result FailedIfNotEnabled() {
        std::lock_guard<std::mutex> lock(g_shared_data.mutex);
        if (!g_shared_data.enabled) {
            return MAKERESULT(Module_Libnx, 1);
        }
        return 0;
    }

    Result IsValidFileType(const char* name) {
        const char* ext = std::strrchr(name, '.');
        if (!ext) return MAKERESULT(Module_Libnx, 1);

        bool found = false;
        for (size_t i = 0; i < std::size(SUPPORTED_EXT); i++) {
            if (!strcasecmp(ext, SUPPORTED_EXT[i])) {
                found = true;
                break;
            }
        }

        if (!found) return MAKERESULT(Module_Libnx, 1);
        return 0;
    }

    Result GetEntryType(const char *path, haze::FileAttrType *out_entry_type) override {
        Result rc = FsProxyVfs::GetEntryType(path, out_entry_type);
        if(R_FAILED(rc)) return rc;
        if (*out_entry_type == haze::FileAttrType_FILE) {
            rc = FailedIfNotEnabled();
            if(R_FAILED(rc)) return rc;
        }
        return 0;
    }

    Result CreateFile(const char* path, s64 size) override {
        Result rc = FailedIfNotEnabled();
        if(R_FAILED(rc)) return rc;
        rc = IsValidFileType(path);
        if(R_FAILED(rc)) return rc;
        return FsProxyVfs::CreateFile(path, size);
    }

    Result OpenFile(const char *path, haze::FileOpenMode mode, haze::File *out_file) override {
        Result rc = FailedIfNotEnabled();
        if(R_FAILED(rc)) return rc;
        rc = IsValidFileType(path);
        if(R_FAILED(rc)) return rc;
        rc = FsProxyVfs::OpenFile(path, mode, out_file);
        if(R_FAILED(rc)) return rc;

        if (mode == haze::FileOpenMode_WRITE) {
            auto f = static_cast<File*>(out_file->impl);
            const auto& e = m_entries[f->index];

            if(!g_shared_data.current_file.empty()) return MAKERESULT(Module_Libnx, 1);
            g_shared_data.current_file = e.name;
            g_shared_data.current_file_size = e.file_size;
            on_thing();
        }
        return 0;
    }

    Result WriteFile(haze::File *file, s64 off, const void *buf, u64 write_size) override {
        {
            std::lock_guard<std::mutex> lock(g_shared_data.mutex);
            if (!g_shared_data.enabled) {
                return MAKERESULT(Module_Libnx, 1);
            }

            if (!g_shared_data.on_write || !g_shared_data.on_write(buf, write_size)) {
                return MAKERESULT(Module_Libnx, 1);
            }
        }

        return FsProxyVfs::WriteFile(file, off, buf, write_size);
    }

    void CloseFile(haze::File *file) override {
        auto f = static_cast<File*>(file->impl);
        if (!f) return;

        bool update = false;
        {
            std::lock_guard<std::mutex> lock(g_shared_data.mutex);
            if (f->mode == haze::FileOpenMode_WRITE) {
                if (g_shared_data.on_close) {
                    g_shared_data.on_close();
                }
                g_shared_data.in_progress = false;
                g_shared_data.current_file.clear();
                update = true;
            }
        }

        if (update) {
            on_thing();
        }

        FsProxyVfs::CloseFile(file);
    }
};

struct FsProxySdCard : FsProxyBase {
    using FsProxyBase::FsProxyBase;

    struct SdFile {
        FILE* f;
    };

    struct SdDir {
        std::vector<haze::DirEntry> entries;
        size_t pos = 0;
    };

    std::string GetSdPath(const char* path) {
        if (!path) return "sdmc:/";
        std::string p = path;
        if (p.find(m_name) == 0) {
            p = p.substr(m_name.length());
        }
        if (p.empty() || p == "/") return "sdmc:/";
        if (p[0] == '/') p = p.substr(1);
        return "sdmc:/" + p;
    }

    Result GetEntryType(const char *path, haze::FileAttrType *out_entry_type) override {
        std::string p = path ? path : "";
        if (p.find(m_name) == 0) {
            p = p.substr(m_name.length());
        }
        if (p.empty() || p == "/") {
            *out_entry_type = haze::FileAttrType_DIR;
            return 0;
        }
        struct stat st;
        if (stat(GetSdPath(path).c_str(), &st) != 0) {
            return 0x202; // Not found
        }
        if (S_ISDIR(st.st_mode)) {
            *out_entry_type = haze::FileAttrType_DIR;
        } else {
            *out_entry_type = haze::FileAttrType_FILE;
        }
        return 0;
    }

    Result GetEntryAttributes(const char *path, haze::FileAttr *out) override {
        std::string p = path ? path : "";
        if (p.find(m_name) == 0) {
            p = p.substr(m_name.length());
        }
        if (p.empty() || p == "/") {
            out->type = haze::FileAttrType_DIR;
            out->size = 0;
            return 0;
        }
        struct stat st;
        if (stat(GetSdPath(path).c_str(), &st) != 0) {
            return 0x202; // Not found
        }
        if (S_ISDIR(st.st_mode)) {
            out->type = haze::FileAttrType_DIR;
            out->size = 0;
        } else {
            out->type = haze::FileAttrType_FILE;
            out->size = st.st_size;
        }
        return 0;
    }

    Result CreateFile(const char* path, s64 size) override {
        FILE* f = fopen(GetSdPath(path).c_str(), "wb");
        if (!f) return 0x202;
        fclose(f);
        return 0;
    }

    Result DeleteFile(const char* path) override {
        if (remove(GetSdPath(path).c_str()) != 0) return 0x202;
        return 0;
    }

    Result RenameFile(const char *old_path, const char *new_path) override {
        if (rename(GetSdPath(old_path).c_str(), GetSdPath(new_path).c_str()) != 0) return 0x202;
        return 0;
    }

    Result OpenFile(const char *path, haze::FileOpenMode mode, haze::File *out_file) override {
        const char* m = (mode == haze::FileOpenMode_WRITE) ? "r+b" : "rb";
        FILE* f = fopen(GetSdPath(path).c_str(), m);
        if (!f && mode == haze::FileOpenMode_WRITE) {
            f = fopen(GetSdPath(path).c_str(), "w+b");
        }
        if (!f) return 0x202;
        auto sf = new SdFile();
        sf->f = f;
        out_file->impl = sf;
        return 0;
    }

    Result GetFileSize(haze::File *file, s64 *out_size) override {
        auto sf = static_cast<SdFile*>(file->impl);
        fseek(sf->f, 0, SEEK_END);
        *out_size = ftell(sf->f);
        fseek(sf->f, 0, SEEK_SET);
        return 0;
    }

    Result SetFileSize(haze::File *file, s64 size) override {
        return 0;
    }

    Result ReadFile(haze::File *file, s64 off, void *buf, u64 read_size, u64 *out_bytes_read) override {
        auto sf = static_cast<SdFile*>(file->impl);
        fseek(sf->f, off, SEEK_SET);
        *out_bytes_read = fread(buf, 1, read_size, sf->f);
        return 0;
    }

    Result WriteFile(haze::File *file, s64 off, const void *buf, u64 write_size) override {
        auto sf = static_cast<SdFile*>(file->impl);
        fseek(sf->f, off, SEEK_SET);
        fwrite(buf, 1, write_size, sf->f);
        return 0;
    }

    void CloseFile(haze::File *file) override {
        auto sf = static_cast<SdFile*>(file->impl);
        if (sf) {
            if (sf->f) fclose(sf->f);
            delete sf;
            file->impl = nullptr;
        }
    }

    Result CreateDirectory(const char* path) override {
        if (mkdir(GetSdPath(path).c_str(), 0777) != 0) return 0x202;
        return 0;
    }

    Result DeleteDirectoryRecursively(const char* path) override {
        try {
            std::filesystem::remove_all(GetSdPath(path));
            return 0;
        } catch (...) {
            return 0x202;
        }
    }

    Result RenameDirectory(const char *old_path, const char *new_path) override {
        if (rename(GetSdPath(old_path).c_str(), GetSdPath(new_path).c_str()) != 0) return 0x202;
        return 0;
    }

    Result OpenDirectory(const char *path, haze::Dir *out_dir) override {
        DIR* d = opendir(GetSdPath(path).c_str());
        if (!d) return 0x202;
        auto sd = new SdDir();
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            haze::DirEntry e;
            memset(&e, 0, sizeof(e));
            std::strncpy(e.name, ent->d_name, sizeof(e.name) - 1);
            sd->entries.push_back(e);
        }
        closedir(d);
        out_dir->impl = sd;
        return 0;
    }

    Result ReadDirectory(haze::Dir *d, s64 *out_total_entries, size_t max_entries, haze::DirEntry *buf) override {
        auto sd = static_cast<SdDir*>(d->impl);
        size_t count = std::min<size_t>(max_entries, sd->entries.size() - sd->pos);
        for (size_t i = 0; i < count; i++) {
            buf[i] = sd->entries[sd->pos + i];
        }
        sd->pos += count;
        *out_total_entries = count;
        return 0;
    }

    Result GetDirectoryEntryCount(haze::Dir *d, s64 *out_count) override {
        auto sd = static_cast<SdDir*>(d->impl);
        *out_count = sd->entries.size();
        return 0;
    }

    void CloseDirectory(haze::Dir *d) override {
        auto sd = static_cast<SdDir*>(d->impl);
        if (sd) {
            delete sd;
            d->impl = nullptr;
        }
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        struct statvfs stat;
        if (statvfs("sdmc:/", &stat) == 0) {
            *out = (s64)stat.f_blocks * (s64)stat.f_frsize;
        } else {
            *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        }
        return 0;
    }

    Result GetFreeSpace(const char *path, s64 *out) override {
        struct statvfs stat;
        if (statvfs("sdmc:/", &stat) == 0) {
            *out = (s64)stat.f_bfree * (s64)stat.f_frsize;
        } else {
            *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
        }
        return 0;
    }
};

haze::FsEntries g_fs_entries;

void haze_callback(const haze::CallbackData *data) {
    // Optional callback logging
}

} // namespace

bool Init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_is_running) return false;

    g_fs_entries.emplace_back(std::make_shared<FsInstallProxy>("1. SD Card Install [NSP, NSZ, XCI]", "1. SD Card Install [NSP, NSZ, XCI]"));
    g_fs_entries.emplace_back(std::make_shared<FsProxySdCard>("2. SD Card Explorer", "2. SD Card Explorer"));

    g_should_exit = false;
    // Standard Nintendo VID/PID
    if (!haze::Initialize(haze_callback, g_fs_entries, 0x057E, 0x201D)) {
        return false;
    }

    g_is_running = true;
    return true;
}

bool IsInit() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_is_running;
}

void Exit() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_is_running) return;

    haze::Exit();
    g_is_running = false;
    g_should_exit = true;
    g_fs_entries.clear();
}

void InitInstallMode(const OnInstallStart& on_start, const OnInstallWrite& on_write, const OnInstallClose& on_close) {
    std::lock_guard<std::mutex> lock(g_shared_data.mutex);
    g_shared_data.on_start = on_start;
    g_shared_data.on_write = on_write;
    g_shared_data.on_close = on_close;
    g_shared_data.enabled = true;
}

void DisableInstallMode() {
    std::lock_guard<std::mutex> lock(g_shared_data.mutex);
    g_shared_data.enabled = false;
}

} // namespace MTP
