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
#include <haze.hpp>
#include <haze/console_main_loop.hpp>
#include <mutex>

namespace haze {
namespace {

std::mutex g_mutex;
std::unique_ptr<haze::ConsoleMainLoop> g_haze{};

} // namespace

// generic helper for if timestamp isn't needed.
::Result FileSystemProxyImpl::GetEntryAttributes(const char *path, FileAttr *out) {
    if (auto rc = GetEntryType(path, &out->type); R_FAILED(rc)) {
        return rc;
    }

    if (IsReadOnly()) {
        out->flag |= FileAttrFlag_READ_ONLY;
    }

    if (out->type == FileAttrType_FILE) {
        File f;
        if (auto rc = OpenFile(path, FileOpenMode_READ, &f); R_FAILED(rc)) {
            return rc;
        }
        ON_SCOPE_EXIT{ CloseFile(&f); };

        s64 size;
        if (auto rc = GetFileSize(&f, &size); R_FAILED(rc)) {
            return rc;
        }

        out->size = size;
    }

    return 0;
}

bool Initialize(Callback callback, const FsEntries& entries, u16 vid, u16 pid, bool enable_log) {
    std::scoped_lock lock{g_mutex};
    if (g_haze) {
        return false;
    }

    if (entries.empty()) {
        return false;
    }

    /* Load device firmware version and serial number. */
    HAZE_R_ABORT_UNLESS(haze::LoadDeviceProperties());

    g_haze = std::make_unique<haze::ConsoleMainLoop>(callback, entries, vid, pid, enable_log);

    return true;
}

void Exit() {
    std::scoped_lock lock{g_mutex};
    if (!g_haze) {
        return;
    }

    /* this will block until thread exit. */
    g_haze.reset();
}

} // namespace haze
