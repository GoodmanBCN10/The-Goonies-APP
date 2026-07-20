/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <haze/common.hpp>
#include <haze/event_reactor.hpp>

namespace haze {

    // wrapper around the filesystem.
    // it handles read only storage by checking if the fs is read only before
    // every write call.
    // this is done because despite telling the MTP client that the fs/file is read only,
    // it seems to ignore it and try and write anyway...
    // Before every fs call, the event reactor is polled for the last result,
    // which is set if the quit event is fired.
    class FileSystemProxy final {
        private:
            EventReactor *m_reactor;
            std::shared_ptr<FileSystemProxyImpl> m_filesystem;
        public:
            explicit FileSystemProxy(EventReactor *reactor, const std::shared_ptr<FileSystemProxyImpl>& fs) : m_reactor(reactor), m_filesystem(fs) { /* ... */ }

            void Finalize() {
                m_reactor = nullptr;
                m_filesystem = nullptr;
            }
        private:
            Result ForwardResult(Result rc) const {
                // NOTE: not thread safe!!!
                /* If the event loop was stopped, return that here. */
                R_TRY(m_reactor->GetResult());

                /* Otherwise, return the call result. */
                R_RETURN(rc);
            }

            auto FixPath(const char* path) const {
                // removes the leading '/' from the parent path concat.
                if (util::Strlen(path) > 1) {
                    return path + 1;
                }
                return path;
            }
        public:
            const char* GetName() const {
                return m_filesystem->GetName();
            }

            const char* GetDisplayName() const {
                return m_filesystem->GetDisplayName();
            }

            bool IsReadOnly() const {
                return m_filesystem->IsReadOnly();
            }

            Result GetTotalSpace(const char *path, s64 *out) const {
                R_RETURN(this->ForwardResult(m_filesystem->GetTotalSpace(FixPath(path), out)));
            }

            Result GetFreeSpace(const char *path, s64 *out) const {
                R_RETURN(this->ForwardResult(m_filesystem->GetFreeSpace(FixPath(path), out)));
            }

            Result GetEntryType(const char *path, FileAttrType *out_entry_type) const {
                R_RETURN(this->ForwardResult(m_filesystem->GetEntryType(FixPath(path), out_entry_type)));
            }

            Result GetEntryAttributes(const char *path, FileAttr *out) const {
                R_RETURN(this->ForwardResult(m_filesystem->GetEntryAttributes(FixPath(path), out)));
            }

            Result CreateFile(const char* path, s64 size) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->CreateFile(FixPath(path), size)));
            }

            Result DeleteFile(const char* path) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->DeleteFile(FixPath(path))));
            }

            Result RenameFile(const char *old_path, const char *new_path) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->RenameFile(FixPath(old_path), FixPath(new_path))));
            }

            Result OpenFile(const char *path, FileOpenMode mode, File *out_file) const {
                if (mode == FileOpenMode_WRITE) {
                    R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                }
                R_RETURN(this->ForwardResult(m_filesystem->OpenFile(FixPath(path), mode, out_file)));
            }

            Result GetFileSize(File *file, s64 *out_size) const {
                R_RETURN(this->ForwardResult(m_filesystem->GetFileSize(file, out_size)));
            }

            Result SetFileSize(File *file, s64 size) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->SetFileSize(file, size)));
            }

            Result ReadFile(File *file, s64 off, void *buf, u64 read_size, u64 *out_bytes_read) const {
                R_RETURN(this->ForwardResult(m_filesystem->ReadFile(file, off, buf, read_size, out_bytes_read)));
            }

            Result WriteFile(File *file, s64 off, const void *buf, u64 write_size) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->WriteFile(file, off, buf, write_size)));
            }

            void CloseFile(File *file) const {
                m_filesystem->CloseFile(file);
            }

            Result CreateDirectory(const char* path) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->CreateDirectory(FixPath(path))));
            }

            Result DeleteDirectoryRecursively(const char* path) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->DeleteDirectoryRecursively(FixPath(path))));
            }

            Result RenameDirectory(const char *old_path, const char *new_path) const {
                R_UNLESS(!this->IsReadOnly(), ams::fs::ResultUnsupportedWriteForReadOnlyFileSystem());
                R_RETURN(this->ForwardResult(m_filesystem->RenameDirectory(FixPath(old_path), FixPath(new_path))));
            }

            Result OpenDirectory(const char *path, Dir *out_dir) const {
                R_RETURN(this->ForwardResult(m_filesystem->OpenDirectory(FixPath(path), out_dir)));
            }

            Result ReadDirectory(Dir *d, s64 *out_total_entries, size_t max_entries, DirEntry *buf) const {
                R_RETURN(this->ForwardResult(m_filesystem->ReadDirectory(d, out_total_entries, max_entries, buf)));
            }

            Result GetDirectoryEntryCount(Dir *d, s64 *out_count) const {
                R_RETURN(this->ForwardResult(m_filesystem->GetDirectoryEntryCount(d, out_count)));
            }

            void CloseDirectory(Dir *d) const {
                m_filesystem->CloseDirectory(d);
            }
    };

}
