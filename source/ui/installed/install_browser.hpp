#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <set>
#include <borealis.hpp>

namespace goonies::ui {

struct InstallFileEntry {
    std::string name;
    std::string lowerName;
    std::string path;
    bool directory = false;
    bool selected = false;
};

class InstallBrowserView;

class InstallDataSource : public brls::RecyclerDataSource {
public:
    explicit InstallDataSource(InstallBrowserView* owner) : owner_(owner) {}
    int numberOfRows(brls::RecyclerFrame*, int) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame*, brls::IndexPath) override;
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath) override;

private:
    InstallBrowserView* owner_;
};

class InstallFileCell : public brls::RecyclerCell {
public:
    InstallFileCell(InstallBrowserView* owner);
    void setEntry(const InstallFileEntry& entry, size_t index);

private:
    InstallBrowserView* owner_ = nullptr;
    size_t cellIndex_ = -1;
    
    brls::Label* label_ = nullptr;
};

class InstallBrowserView : public brls::Box {
public:
    explicit InstallBrowserView(const std::string& startPath);
    ~InstallBrowserView() override = default;
    
    std::vector<InstallFileEntry>& entries() { return entries_; }
    
    void loadDirectory(const std::string& path);
    void select(size_t index);
    void toggleSelection(size_t index);
    void startInstallQueue(const std::string& singleClickedPath);

private:
    std::string currentPath_;
    std::vector<InstallFileEntry> entries_;
    brls::RecyclerFrame* recycler_;
    brls::Label* titleLabel_;
};

} // namespace goonies::ui
