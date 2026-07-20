#pragma once

#include <switch.h>
#include <vector>
#include <memory>

namespace haze {

typedef enum {
    CallbackType_OpenSession, // data = none
    CallbackType_CloseSession, // data = none
    CallbackType_CreateFile, // data = file
    CallbackType_DeleteFile, // data = file
    CallbackType_RenameFile, // data = rename
    CallbackType_RenameFolder, // data = rename
    CallbackType_CreateFolder, // data = file
    CallbackType_DeleteFolder, // data = file
    CallbackType_ReadBegin, // data = file
    CallbackType_ReadProgress, // data = progress
    CallbackType_ReadEnd, // data = file
    CallbackType_WriteBegin, // data = file
    CallbackType_WriteProgress, // data = progress
    CallbackType_WriteEnd, // data = file
} CallbackType;

typedef struct {
    char filename[FS_MAX_PATH];
} CallbackDataFile;

typedef struct {
    char filename[FS_MAX_PATH];
    char newname[FS_MAX_PATH];
} CallbackDataRename;

typedef struct {
    long long offset;
    long long size;
} CallbackDataProgress;


typedef struct {
    CallbackType type;
    union {
        CallbackDataFile file;
        CallbackDataRename rename;
        CallbackDataProgress progress;
    };
} CallbackData;

typedef void(*Callback)(const CallbackData* data);

enum FileOpenMode {
    FileOpenMode_READ,
    FileOpenMode_WRITE,
};

enum FileAttrType : u8 {
    FileAttrType_DIR = 0,
    FileAttrType_FILE = 1,
};

enum FileAttrFlag {
    FileAttrFlag_NONE = 0,
    FileAttrFlag_READ_ONLY = BIT(1),
};

struct FileAttr {
    FileAttrType type{}; // FileAttrType
    u8 flag{}; // FileAttrFlag
    u64 size{};
    u64 ctime{};
    u64 mtime{};
};

struct DirEntry {
    char name[FS_MAX_PATH]{};
};

struct File {
    void* impl{};
};

struct Dir {
    void* impl{};
};

struct FileSystemProxyImpl {
    virtual const char* GetName() const = 0;
    virtual const char* GetDisplayName() const = 0;

    virtual Result GetTotalSpace(const char *path, s64 *out) = 0;
    virtual Result GetFreeSpace(const char *path, s64 *out) = 0;
    virtual Result GetEntryType(const char *path, FileAttrType *out_entry_type) = 0;
    virtual Result GetEntryAttributes(const char *path, FileAttr *out);
    virtual Result CreateFile(const char* path, s64 size) = 0;
    virtual Result DeleteFile(const char* path) = 0;
    virtual Result RenameFile(const char *old_path, const char *new_path) = 0;
    virtual Result OpenFile(const char *path, FileOpenMode mode, File *out_file) = 0;
    virtual Result GetFileSize(File *file, s64 *out_size) = 0;
    virtual Result SetFileSize(File *file, s64 size) = 0;
    virtual Result ReadFile(File *file, s64 off, void *buf, u64 read_size, u64 *out_bytes_read) = 0;
    virtual Result WriteFile(File *file, s64 off, const void *buf, u64 write_size) = 0;
    virtual void CloseFile(File *file) = 0;

    virtual Result CreateDirectory(const char* path) = 0;
    virtual Result DeleteDirectoryRecursively(const char* path) = 0;
    virtual Result RenameDirectory(const char *old_path, const char *new_path) = 0;
    virtual Result OpenDirectory(const char *path, Dir *out_dir) = 0;
    virtual Result ReadDirectory(Dir *d, s64 *out_total_entries, size_t max_entries, DirEntry *buf) = 0;
    virtual Result GetDirectoryEntryCount(Dir *d, s64 *out_count) = 0;
    virtual void CloseDirectory(Dir *d) = 0;

    virtual bool IsReadOnly() { return false; }
};

using FsEntries = std::vector<std::shared_ptr<FileSystemProxyImpl>>;

/* Callback is optional */
bool Initialize(Callback callback, const FsEntries& entries, u16 vid = 0x057e, u16 pid = 0x201d, bool enable_log = false);
void Exit();

} // namespace haze
