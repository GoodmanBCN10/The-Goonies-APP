#pragma once

#include "frontend/menus/filebrowser.hpp"

namespace GooniesInstaller::frontend::menu::filebrowser::picker {

using Callback = std::function<bool(const fs::FsPath& path)>;

struct Menu final : Base {
    explicit Menu(const Callback& cb, const std::vector<std::string>& filter = {}, const fs::FsPath& path = {});

private:
    void OnClick(FsView* view, const FsEntry& fs_entry, const FileEntry& entry, const fs::FsPath& path) override;

private:
    const Callback m_callback;
};

} // namespace GooniesInstaller::frontend::menu::filebrowser::picker
