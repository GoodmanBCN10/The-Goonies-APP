#include "ui/installed/install_browser.hpp"
#include <borealis/views/bottom_bar.hpp>
#include <fmt/core.h>
#include <algorithm>
#include "ui/installed/local_install_view.hpp"

namespace goonies::ui {

InstallFileCell::InstallFileCell(InstallBrowserView* owner) : owner_(owner) {
    this->setWidthPercentage(100);
    this->setHeight(64);
    this->setFocusable(true);
    this->inflateFromXMLRes("xml/install_file_cell.xml");
    
    this->registerAction("Marcar", brls::BUTTON_Y, [this](brls::View* view) {
        if (owner_ && cellIndex_ != (size_t)-1) {
            owner_->toggleSelection(cellIndex_);
            // Update this specific cell's UI directly to avoid reloadData() killing focus
            if (cellIndex_ < owner_->entries().size()) {
                this->setEntry(owner_->entries()[cellIndex_], cellIndex_);
            }
        }
        return true;
    });

    this->registerAction("Instalar Marcados", brls::BUTTON_X, [this](brls::View* view) {
        if (owner_) {
            owner_->startInstallQueue("");
        }
        return true;
    });
}

void InstallFileCell::setEntry(const InstallFileEntry& entry, size_t index) {
    cellIndex_ = index;
    if (checkbox_ && label_) {
        if (entry.directory) {
            checkbox_->setText("");
            label_->setText(fmt::format("\uE2C7 {}", entry.name)); // Folder icon if we had one, just text for now
        } else {
            if (entry.selected) {
                checkbox_->setText("\uE834"); // Checkbox checked
            } else {
                checkbox_->setText("\uE835"); // Checkbox unchecked
            }
            label_->setText(entry.name);
        }
    }
}

int InstallDataSource::numberOfRows(brls::RecyclerFrame*, int) {
    return static_cast<int>(owner_->entries().size());
}

brls::RecyclerCell* InstallDataSource::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) {
    auto* cell = static_cast<InstallFileCell*>(recycler->dequeueReusableCell("File"));
    // Owner is already passed in constructor when cell was created.
    cell->setEntry(owner_->entries()[index.row], index.row);
    return cell;
}

void InstallDataSource::didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) {
    owner_->select(static_cast<size_t>(index.row));
}

InstallBrowserView::InstallBrowserView(const std::string& startPath) : brls::Box(brls::Axis::COLUMN), currentPath_(startPath) {
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* centerBox = new brls::Box(brls::Axis::COLUMN);
    centerBox->setAlignItems(brls::AlignItems::STRETCH);
    centerBox->setPadding(40, 80, 40, 80);
    centerBox->setGrow(1.0f);

    titleLabel_ = new brls::Label();
    titleLabel_->setText("Explorador de Archivos");
    titleLabel_->setFontSize(45);
    titleLabel_->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
    titleLabel_->setMarginBottom(20);
    centerBox->addView(titleLabel_);

    recycler_ = new brls::RecyclerFrame();
    recycler_->setPadding(8, 0, 8, 0);
    recycler_->setGrow(1.0f);
    recycler_->estimatedRowHeight = 64;
    recycler_->registerCell("File", [this] { return new InstallFileCell(this); });
    recycler_->setDataSource(new InstallDataSource(this));
    
    centerBox->addView(recycler_);
    
    this->addView(centerBox);
    this->addView(new brls::BottomBar());
    
    // Y button action is now handled by the cell directly.

    // X button to install all selected
    this->registerAction("Instalar Marcados", brls::BUTTON_X, [this](brls::View* view) {
        startInstallQueue("");
        return true;
    });

    this->registerAction("Volver", brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });

    loadDirectory(currentPath_);
}

void InstallBrowserView::loadDirectory(const std::string& path) {
    entries_.clear();
    currentPath_ = path;

    InstallFileEntry loadingEntry;
    loadingEntry.name = "Cargando contenido del USB...";
    loadingEntry.directory = false;
    loadingEntry.path = "";
    entries_.push_back(loadingEntry);
    
    titleLabel_->setText("Explorador - " + path);

    std::vector<InstallFileEntry> newEntries;

    // Add parent dir if not root
    if (path != "sdmc:/" && path != "usb:/" && path != "usb0:/" && path != "usb1:/") {
        InstallFileEntry parent;
        parent.name = "..";
        parent.directory = true;
        size_t slash = path.find_last_of('/', path.length() - 2);
        if (slash != std::string::npos) {
            parent.path = path.substr(0, slash + 1);
            newEntries.push_back(parent);
        }
    }

    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

            std::string fullPath = path;
            if (fullPath.back() != '/') fullPath += "/";
            fullPath += ent->d_name;

            InstallFileEntry entry;
            entry.name = ent->d_name;
            entry.path = fullPath;
            
            struct stat st;
            if (stat(fullPath.c_str(), &st) == 0) {
                entry.directory = S_ISDIR(st.st_mode);
            } else {
                size_t dotPos = entry.name.find_last_of(".");
                entry.directory = (dotPos == std::string::npos);
            }

            if (!entry.directory) {
                std::string ext = "";
                size_t dotPos = entry.name.find_last_of(".");
                if (dotPos != std::string::npos) {
                    ext = entry.name.substr(dotPos + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                }
                if (ext != "nsp" && ext != "nsz" && ext != "xci") {
                    continue;
                }
            }
            
            // Pre-calculate lowercase name for ultra-fast sorting without O(N log N) allocations
            entry.lowerName = entry.name;
            std::transform(entry.lowerName.begin(), entry.lowerName.end(), entry.lowerName.begin(), [](unsigned char c){ return std::tolower(c); });

            newEntries.push_back(entry);
            
            // Pump main loop every 128 items to prevent Watchdog from killing the app when reading large USB folders
            if (newEntries.size() % 128 == 0) {
                appletMainLoop();
            }
        }
        closedir(dir);
        
        // Sort: directories first, then alphabetically
        std::sort(newEntries.begin(), newEntries.end(), [](const InstallFileEntry& a, const InstallFileEntry& b) {
            if (a.name == ".." && b.name != "..") return true;
            if (b.name == ".." && a.name != "..") return false;
            if (a.name == ".." && b.name == "..") return false;
            if (a.directory != b.directory) return a.directory;
            return a.lowerName < b.lowerName;
        });
    }

    entries_ = newEntries;
    recycler_->reloadData();
}

void InstallBrowserView::select(size_t index) {
    if (index >= entries_.size()) return;
    const InstallFileEntry& entry = entries_[index];
    
    if (entry.directory) {
        brls::sync([this, path = entry.path] { loadDirectory(path); });
        return;
    }

    // It's a file. If it's selected via A button, we should ask to install.
    // Wait, let's just add an action in the cell, or just pop a dialog
    auto* dialog = new brls::Dialog(fmt::format("¿Instalar {}?", entry.name));
    dialog->addButton("Instalar", [this, path = entry.path]() {
        startInstallQueue(path);
    });
    dialog->addButton("Marcar/Desmarcar", [this, index]() {
        toggleSelection(index);
    });
    dialog->addButton("Cancelar", []() {});
    dialog->open();
}

void InstallBrowserView::toggleSelection(size_t index) {
    if (index >= entries_.size()) return;
    if (entries_[index].directory) return;
    entries_[index].selected = !entries_[index].selected;
    // UI is now updated directly by the cell in registerAction
}

void InstallBrowserView::startInstallQueue(const std::string& singleClickedPath) {
    std::vector<std::string> queue;
    for (const auto& entry : entries_) {
        if (entry.selected && !entry.directory && !entry.path.empty()) {
            queue.push_back(entry.path);
        }
    }

    if (queue.empty() && !singleClickedPath.empty()) {
        queue.push_back(singleClickedPath);
    }

    if (queue.empty()) {
        brls::Application::notify("No hay archivos marcados para instalar.");
        return;
    }

    // Push the installer view
    brls::Application::pushActivity(new brls::Activity(new goonies::ui::LocalInstallView(queue)));
    // brls::Application::notify(fmt::format("Iniciando instalacion de {} archivo(s)...", queue.size()));
}

} // namespace goonies::ui
