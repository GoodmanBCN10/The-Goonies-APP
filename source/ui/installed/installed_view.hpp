#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/installed_title_service.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"
#include "ui/installed/installed_details_view.hpp"
#include "ui/common/message_cells.hpp"
#include "app/catalog_service.hpp"
#include "app/app_settings.hpp"
#include "ui/detail/game_detail.hpp"
#include "app/nx_versions_service.hpp"
#include "app/package_utils.hpp"

namespace goonies::ui {

class InstalledCell : public brls::RecyclerCell {
public:
    InstalledCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(10, 18, 10, 18);
        setHeight(92);

        image_ = new AsyncRgbaImage();
        image_->setWidth(64);
        image_->setHeight(64);
        image_->setCornerRadius(8);
        image_->setMarginRight(16);
        image_->setScalingType(brls::ImageScalingType::FILL);
        addView(image_);

        auto* labels = new brls::Box(brls::Axis::COLUMN);
        labels->setGrow(1);
        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setFontSize(21);
        subtitle_ = new brls::Label();
        subtitle_->setSingleLine(true);
        subtitle_->setFontSize(15);
        subtitle_->setMarginTop(6);
        subtitle_->setTextColor(theme::textTertiary());
        labels->addView(title_);
        labels->addView(subtitle_);
        addView(labels);
    }

    void setTitle(const InstalledTitle& title,
                  GameMetadataService* metadata,
                  CatalogService* catalog,
                  bool isUpdateList,
                  std::function<void(const InstalledTitle&)> onClick = nullptr) {
        title_->setText(title.name);
        std::string subtitle = title.publisher;
        if (!subtitle.empty())
            subtitle += "   ";
        subtitle += title.titleId;

        if (isUpdateList) {
            uint32_t latestVer = 0;
            if (metadata) {
                auto meta = metadata->findByTitleId(title.titleId);
                if (meta) latestVer = meta->latestVersion;
            }
            if (latestVer == 0 && catalog) {
                uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
                for (const auto& entry : catalog->entries()) {
                    if (entry.isHiddenByDefault()) continue;
                    bool match = false;
                    if (!entry.titleId.empty()) {
                        try {
                            uint64_t tid = std::stoull(entry.titleId, nullptr, 16);
                            if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                        } catch (...) {}
                    }
                    if (match && entry.updateVersion > 0) {
                        latestVer = entry.updateVersion;
                        break;
                    }
                }
            }
            if (latestVer == 0) {
                latestVer = NxVersionsService::instance().getLatestVersion(title.titleId);
            }
            if (latestVer > 0) {
                std::string versionStr = "v" + std::to_string(latestVer / 65536);
                subtitle += "   (" + versionStr + ")";
            }
        }

        subtitle_->setText(subtitle);
        setArtworkUrl(image_, metadata, title.iconPath, currentIconPath_,
                      imageState_);

        this->registerClickAction([title, metadata, onClick](brls::View* view) {
            if (onClick) {
                onClick(title);
            } else {
                brls::Application::pushActivity(new brls::Activity(
                    new InstalledDetailsView(title, metadata)));
            }
            return true;
        });
    }

private:
    AsyncRgbaImage* image_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* subtitle_ = nullptr;
    brls::Box* updateBadgeBox_ = nullptr;
    brls::Label* updateBadgeLabel_ = nullptr;
    std::string currentIconPath_;
    std::shared_ptr<ImageRequestState> imageState_ =
        std::make_shared<ImageRequestState>();
};

inline bool checkHasUpdate(const InstalledTitle& title,
                           GameMetadataService* metadata,
                           CatalogService* catalog) {
    uint32_t latestVer = NxVersionsService::instance().getLatestVersion(title.titleId);
    if (latestVer == 0 && metadata) {
        auto meta = metadata->findByTitleId(title.titleId);
        if (meta) latestVer = meta->latestVersion;
    }

    if (latestVer == 0 && catalog) {
        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        for (const auto& entry : catalog->entries()) {
            if (entry.isHiddenByDefault()) continue;
            bool match = false;
            if (!entry.titleId.empty()) {
                try {
                    uint64_t tid = std::stoull(entry.titleId, nullptr, 16);
                    if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                } catch (...) {}
            }
            if (!match && metadata) {
                const GameMetadata* meta = metadata->findByInfoHash(entry.infoHash);
                if (meta && !meta->titleId.empty()) {
                    try {
                        uint64_t tid = std::stoull(meta->titleId, nullptr, 16);
                        if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                    } catch (...) {}
                }
            }
            if (match && entry.updateVersion > latestVer) {
                latestVer = entry.updateVersion;
            }
        }
    }

    // STRICT: If latest known version is not known (0) or is already <= installed internal version,
    // then this title DOES NOT have a newer update!
    if (latestVer == 0 || latestVer <= title.internalVersion) {
        return false;
    }

    // A newer version is available (latestVer > title.internalVersion).
    // If a catalog is provided, ensure we actually have this game/update available in the catalog:
    if (catalog) {
        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        for (const auto& entry : catalog->entries()) {
            if (entry.isHiddenByDefault()) continue;
            bool match = false;
            if (!entry.titleId.empty()) {
                try {
                    uint64_t tid = std::stoull(entry.titleId, nullptr, 16);
                    if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                } catch (...) {}
            }
            if (!match && metadata) {
                const GameMetadata* meta = metadata->findByInfoHash(entry.infoHash);
                if (meta && !meta->titleId.empty()) {
                    try {
                        uint64_t tid = std::stoull(meta->titleId, nullptr, 16);
                        if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                    } catch (...) {}
                }
            }
            if (!match && releaseNameMatches(title.name, entry.title)) {
                match = true;
            }
            if (match) return true;
        }
        return false;
    }

    return true;
}

class InstalledGridItem : public brls::Box {
public:
    InstalledGridItem() : brls::Box(brls::Axis::COLUMN) {
        setFocusable(true);
        setWidth(140);
        setHeight(210);
        setCornerRadius(12);
        setPadding(10, 10, 10, 10);
        
        cover_ = new brls::Box();
        cover_->setWidth(140);
        cover_->setHeight(140);
        cover_->setCornerRadius(12);
        cover_->setClipsToBounds(true);
        cover_->setMarginBottom(8);
        cover_->setBackgroundColor(theme::surface());
        
        image_ = new AsyncRgbaImage();
        image_->setWidth(140);
        image_->setHeight(140);
        image_->setPositionType(brls::PositionType::ABSOLUTE);
        image_->setPositionTop(0);
        image_->setPositionLeft(0);
        image_->setScalingType(brls::ImageScalingType::FILL);
        cover_->addView(image_);
        
        addView(cover_);

        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        title_->setFontSize(18);
        title_->setHorizontalAlign(brls::HorizontalAlign::LEFT);
        title_->setTextColor(theme::textPrimary());
        addView(title_);

        subtitle_ = new brls::Label();
        subtitle_->setSingleLine(true);
        subtitle_->setAutoAnimate(false);
        subtitle_->setFontSize(14);
        subtitle_->setHorizontalAlign(brls::HorizontalAlign::LEFT);
        subtitle_->setTextColor(theme::textTertiary());
        subtitle_->setMarginTop(4);
        addView(subtitle_);
    }

    void setTitle(const InstalledTitle& title, GameMetadataService* metadata, CatalogService* catalog, AppSettings* settings, DownloadManager* manager, std::function<void(const InstalledTitle&)> onClick = nullptr) {
        this->setVisibility(brls::Visibility::VISIBLE);
        title_->setText(title.name);
        subtitle_->setText(title.publisher.empty() ? title.titleId : title.publisher);
        setArtworkUrl(image_, metadata, title.iconPath, currentIconPath_, imageState_);

        bool hasUpdate = checkHasUpdate(title, metadata, catalog);
        if (updateBadgeBox_) {
            updateBadgeBox_->setVisibility(hasUpdate ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        }

        this->registerClickAction([title, metadata, onClick](brls::View* view) {
            if (onClick) {
                onClick(title);
            } else {
                brls::Application::pushActivity(new brls::Activity(new InstalledDetailsView(title, metadata)));
            }
            return true;
        });
    }

    void clear() {
        this->setVisibility(brls::Visibility::INVISIBLE);
        title_->setText("");
        subtitle_->setText("");
        image_->clear();
        currentIconPath_.clear();
        this->setFocusable(false);
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        title_->setAnimated(true);
        subtitle_->setAnimated(true);
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        title_->setAnimated(false);
        subtitle_->setAnimated(false);
    }

private:
    brls::Box* cover_ = nullptr;
    AsyncRgbaImage* image_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* subtitle_ = nullptr;
    brls::Box* updateBadgeBox_ = nullptr;
    brls::Label* updateBadgeLabel_ = nullptr;
    std::string currentIconPath_;
    std::shared_ptr<ImageRequestState> imageState_ = std::make_shared<ImageRequestState>();
};

class InstalledGridRow : public brls::RecyclerCell {
public:
    InstalledGridRow(int columns) {
        setFocusable(false);
        setHeight(230);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::STRETCH);
        setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        setPadding(5, 0, 10, 0);
        
        for (int i = 0; i < columns; i++) {
            auto* item = new InstalledGridItem();
            item->setGrow(1);
            items_.push_back(item);
            addView(item);
        }
    }

    void setTitles(const std::vector<InstalledTitle>& titles, size_t offset, int columns, GameMetadataService* metadata, CatalogService* catalog, AppSettings* settings, DownloadManager* manager, std::function<void(const InstalledTitle&)> onClick = nullptr) {
        for (int i = 0; i < columns; i++) {
            if (offset + i < titles.size()) {
                items_[i]->setTitle(titles[offset + i], metadata, catalog, settings, manager, onClick);
                items_[i]->setFocusable(true);
            } else {
                items_[i]->clear();
                items_[i]->setFocusable(false);
            }
        }
    }

private:
    std::vector<InstalledGridItem*> items_;
};

class InstalledDataSource : public brls::RecyclerDataSource {
public:
    enum class SortType { Alphabetical, LastPlayed, InstallDate };

    explicit InstalledDataSource(GameMetadataService* metadata, CatalogService* catalog, AppSettings* settings, DownloadManager* manager, std::function<void(const InstalledTitle&)> onClick = nullptr)
        : metadata_(metadata), catalog_(catalog), settings_(settings), manager_(manager), onClick_(onClick) {}

    void setFilterUpdates(bool filter) {
        showUpdatesOnly_ = filter;
    }

    void setTitles(std::vector<InstalledTitle> titles, const std::string& query = "") {
        titles_.clear();
        std::string q = query;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        
        for (const auto& t : titles) {
            if (showUpdatesOnly_) {
                if (!checkHasUpdate(t, metadata_, catalog_)) {
                    continue;
                }
            }
            
            if (!q.empty()) {
                std::string n = t.name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(q) == std::string::npos && t.titleId.find(q) == std::string::npos) {
                    continue;
                }
            }
            
            titles_.push_back(t);
        }
        applySort();
    }

    size_t count() const {
        return titles_.size();
    }

    void setSortType(SortType type) {
        sortType_ = type;
        applySort();
    }

    SortType getSortType() const {
        return sortType_;
    }

    void setGridView(bool grid, int columns) {
        columns_ = columns;
    }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        if (titles_.empty()) return 1;
        return (titles_.size() + columns_ - 1) / columns_;
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override {
        if (titles_.empty()) {
            auto* cell = static_cast<TextMessageCell*>(recycler->dequeueReusableCell("Message"));
            cell->setMessage(t("No se encontraron aplicaciones instaladas.", "No installed applications found.", "Nenhum aplicativo instalado encontrado."));
            return cell;
        }
        
        if (columns_ == 1) {
            auto* cell = static_cast<InstalledCell*>(recycler->dequeueReusableCell("Installed"));
            cell->setTitle(titles_[index.row], metadata_, catalog_, showUpdatesOnly_, onClick_);
            return cell;
        }

        auto* cell = static_cast<InstalledGridRow*>(recycler->dequeueReusableCell("GridRow"));
        cell->setTitles(titles_, index.row * columns_, columns_, metadata_, catalog_, settings_, manager_, onClick_);
        return cell;
    }

private:
    void applySort() {
        if (sortType_ == SortType::LastPlayed) {
            std::stable_sort(titles_.begin(), titles_.end(), [](const InstalledTitle& a, const InstalledTitle& b) {
                return a.lastPlayedTimestamp > b.lastPlayedTimestamp;
            });
        } else if (sortType_ == SortType::InstallDate) {
            std::stable_sort(titles_.begin(), titles_.end(), [](const InstalledTitle& a, const InstalledTitle& b) {
                return a.installTimestamp > b.installTimestamp;
            });
        } else {
            std::stable_sort(titles_.begin(), titles_.end(), [](const InstalledTitle& a, const InstalledTitle& b) {
                return a.name < b.name;
            });
        }
    }

    GameMetadataService* metadata_;
    CatalogService* catalog_;
    AppSettings* settings_;
    DownloadManager* manager_;
    std::vector<InstalledTitle> titles_;
    int columns_ = 6;
    std::function<void(const InstalledTitle&)> onClick_;
    SortType sortType_ = SortType::Alphabetical;
    bool showUpdatesOnly_ = false;
};

class InstalledView : public brls::Box {
public:
    InstalledView(InstalledTitleService* installed, DownloadManager* manager,
                  GameMetadataService* metadata, CatalogService* catalog, AppSettings* settings)
        : brls::Box(brls::Axis::COLUMN), installed_(installed),
          manager_(manager), metadata_(metadata), catalog_(catalog), settings_(settings), alive_(std::make_shared<std::atomic<bool>>(true)) {
        NxVersionsService::instance().init();

        status_ = new brls::Label();
        status_->setFontSize(15);
        status_->setMarginTop(10);
        status_->setMarginLeft(34);
        status_->setTextColor(theme::textTertiary());
        
        sortLabel_ = new brls::Label();
        sortLabel_->setFontSize(15);
        sortLabel_->setMarginTop(10);
        sortLabel_->setMarginRight(34);
        sortLabel_->setTextColor(theme::textTertiary());
        sortLabel_->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        
        brls::Box* topBar = new brls::Box(brls::Axis::ROW);
        topBar->addView(status_);
        topBar->addView(new brls::Box()); // Spacer
        topBar->getChildren().back()->setGrow(1);
        topBar->addView(sortLabel_);
        addView(topBar);

        contentBox_ = new brls::Box(brls::Axis::COLUMN);
        contentBox_->setGrow(1);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->setPadding(6, 32, 6, 32);
        
        recycler_->registerCell("Installed", [] { return new InstalledCell(); });
        recycler_->registerCell("GridRow", [this] { return new InstalledGridRow(columns_); });
        recycler_->registerCell("Message", [] { return new TextMessageCell(); });
        
        auto onClick = [this](const InstalledTitle& title) {
            if (showUpdatesOnly_ && catalog_) {
                openUpdateForTitle(title);
                return;
            }
            brls::Application::pushActivity(new brls::Activity(new InstalledDetailsView(title, metadata_)));
        };
        
        dataSource_ = new InstalledDataSource(metadata, catalog, settings, manager, onClick);
        recycler_->setDataSource(dataSource_);
        contentBox_->addView(recycler_);

        emptyState_ = new EmptyStateView();
        emptyState_->setGrow(1);
        emptyState_->setVisibility(brls::Visibility::GONE);
        contentBox_->addView(emptyState_);

        addView(contentBox_);
        
        bottomBar_ = new brls::BottomBar();
        addView(bottomBar_);
        
        updateViewMode();
        reload();
        refresh();

        registerAction("Refresh", brls::BUTTON_RB, [this](brls::View*) {
            refresh();
            return true;
        });

        registerAction("Buscar", brls::BUTTON_BACK, [this](brls::View*) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                searchQuery_ = text;
                reload();
            }, "Buscar juegos instalados...", "", 256, searchQuery_, brls::KEYBOARD_DISABLE_NONE);
            return true;
        });

        updateFilterAction();

        registerAction("Ordenar", brls::BUTTON_X, [this](brls::View*) {
            auto current = dataSource_->getSortType();
            if (current == InstalledDataSource::SortType::Alphabetical) {
                dataSource_->setSortType(InstalledDataSource::SortType::InstallDate);
            } else if (current == InstalledDataSource::SortType::InstallDate) {
                dataSource_->setSortType(InstalledDataSource::SortType::LastPlayed);
            } else {
                dataSource_->setSortType(InstalledDataSource::SortType::Alphabetical);
            }
            updateSortLabel();
            recycler_->reloadData();
            recycler_->selectRowAt(brls::IndexPath(0, 0), false);
            brls::Application::giveFocus(recycler_);
            return true;
        });

        // Vista action was removed
        this->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }

    ~InstalledView() override { alive_->store(false); }

    brls::View* getDefaultFocus() override {
        if (showUpdatesOnly_ && dataSource_ && dataSource_->count() == 0 && emptyState_) {
            return emptyState_->getDefaultFocus();
        }
        if (recycler_ && recycler_->getVisibility() == brls::Visibility::VISIBLE) {
            return recycler_;
        }
        if (emptyState_ && emptyState_->getVisibility() == brls::Visibility::VISIBLE) {
            return emptyState_->getDefaultFocus();
        }
        return brls::Box::getDefaultFocus();
    }

private:
    void updateSortLabel() {
        if (!dataSource_) return;
        auto current = dataSource_->getSortType();
        if (current == InstalledDataSource::SortType::Alphabetical) {
            sortLabel_->setText("Ordenado por: Orden alfabético");
        } else if (current == InstalledDataSource::SortType::InstallDate) {
            sortLabel_->setText("Ordenado por: Fecha de instalación");
        } else {
            sortLabel_->setText("Ordenado por: Última partida");
        }
    }

    void openUpdateForTitle(const InstalledTitle& title) {
        if (!catalog_) {
            brls::Application::pushActivity(new brls::Activity(new InstalledDetailsView(title, metadata_)));
            return;
        }

        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        std::vector<CatalogEntry> matching;

        // 1. Search catalog entries matching baseAppId or titleId
        for (const auto& entry : catalog_->entries()) {
            if (entry.isHiddenByDefault()) continue;
            
            bool match = false;
            if (!entry.titleId.empty()) {
                try {
                    uint64_t tid = std::stoull(entry.titleId, nullptr, 16);
                    if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                } catch (...) {}
            }
            if (!match && metadata_) {
                const GameMetadata* meta = metadata_->findByInfoHash(entry.infoHash);
                if (meta && !meta->titleId.empty()) {
                    try {
                        uint64_t tid = std::stoull(meta->titleId, nullptr, 16);
                        if ((tid & ~0x1FFFULL) == baseAppId) match = true;
                    } catch (...) {}
                }
            }
            if (!match) {
                if (releaseNameMatches(title.name, entry.title)) {
                    match = true;
                }
            }

            if (match) {
                matching.push_back(entry);
            }
        }

        if (matching.empty()) {
            uint32_t latestVer = NxVersionsService::instance().getLatestVersion(title.titleId);
            std::string verText = "";
            if (latestVer > 0) {
                verText = " (v" + std::to_string(latestVer / 65536) + " / #" + std::to_string(latestVer) + ")";
            }
            std::string msg = "Actualizacion no disponible en el catalogo\n\n" + title.name +
                              " tiene una actualizacion oficial disponible en Nintendo" + verText +
                              ", pero actualmente no hay ningun torrent disponible en el catalogo para este juego.";
            auto* dialog = new brls::Dialog(msg);
            dialog->addButton("Ver detalles", [this, title]() {
                brls::Application::pushActivity(new brls::Activity(new InstalledDetailsView(title, metadata_)));
            });
            dialog->addButton("Aceptar", []() {});
            dialog->open();
            return;
        }

        // Rank releases:
        // 1. Releases that mention update/patch/dlc or have updateVersion > 0
        // 2. More peers
        // 3. Newer topicId / publishedAt
        std::stable_sort(matching.begin(), matching.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
            std::string aLower = a.title;
            std::string bLower = b.title;
            std::transform(aLower.begin(), aLower.end(), aLower.begin(), ::tolower);
            std::transform(bLower.begin(), bLower.end(), bLower.begin(), ::tolower);

            bool aHasUpd = (a.updateVersion > 0 || aLower.find("upd") != std::string::npos || aLower.find("patch") != std::string::npos || aLower.find("dlc") != std::string::npos || aLower.find("update") != std::string::npos);
            bool bHasUpd = (b.updateVersion > 0 || bLower.find("upd") != std::string::npos || bLower.find("patch") != std::string::npos || bLower.find("dlc") != std::string::npos || bLower.find("update") != std::string::npos);
            if (aHasUpd != bHasUpd) return aHasUpd;

            if (a.peerCount != b.peerCount) return a.peerCount > b.peerCount;
            return a.topicId > b.topicId;
        });

        CatalogEntry bestEntry = matching.front();
        if (bestEntry.titleId.empty()) {
            bestEntry.titleId = title.titleId;
        }

        brls::Application::pushActivity(
            new GameDetailActivity(bestEntry, "", manager_, metadata_, this->installed_, settings_,
                [](const std::string&, const std::string&){},
                [this](){ this->refresh(); },
                [this](){ this->refresh(); }));
    }

    void updateViewMode() {
        if (showUpdatesOnly_) {
            recycler_->estimatedRowHeight = 92;
            dataSource_->setGridView(false, 1);
        } else {
            recycler_->estimatedRowHeight = 230;
            dataSource_->setGridView(true, columns_);
        }
    }

    void updateFilterAction() {
        if (updateActionId_ != 0) {
            unregisterAction(updateActionId_);
            updateActionId_ = 0;
        }
        std::string label = showUpdatesOnly_ ? t("Ver Todos", "View All", "Ver Todos")
                                             : t("Ver Updates", "View Updates", "Ver Atualiz.");
        updateActionId_ = registerAction(label, brls::BUTTON_Y, [this](brls::View*) {
            showUpdatesOnly_ = !showUpdatesOnly_;
            updateViewMode();
            updateFilterAction();
            dataSource_->setFilterUpdates(showUpdatesOnly_);
            reload();
            if (dataSource_->count() > 0) {
                recycler_->selectRowAt(brls::IndexPath(0, 0), false);
                brls::Application::giveFocus(recycler_);
            } else if (emptyState_) {
                brls::Application::giveFocus(emptyState_->getDefaultFocus());
            }
            return true;
        });
    }

    EmptyStateView* ensureEmptyState() {
        return emptyState_;
    }

    bool hasActiveStreamInstall() const {
        for (const DownloadTask& task : manager_->snapshot()) {
            if (task.mode != TransferMode::StreamInstall)
                continue;
            if (task.status == DownloadStatus::Queued ||
                task.status == DownloadStatus::Checking ||
                task.status == DownloadStatus::Downloading ||
                task.status == DownloadStatus::Installing ||
                task.status == DownloadStatus::Committing ||
                task.status == DownloadStatus::Verifying)
                return true;
        }
        return false;
    }

    void reload() {
        std::vector<InstalledTitle> titles = installed_->titles();
        dataSource_->setTitles(std::move(titles), searchQuery_);
        recycler_->reloadData();

        size_t count = dataSource_->count();
        const bool empty = (count == 0);
        if (empty) {
            if (showUpdatesOnly_) {
                emptyState_->setContent(
                    "No hay actualizaciones disponibles",
                    "Todos tus juegos instalados están al día con el catálogo.",
                    "Refrescar", [this] { refresh(); });
            } else {
                emptyState_->setContent(
                    "No hay aplicaciones instaladas",
                    "Refresca la lista tras instalar juegos o al volver del menú Home.",
                    "Refrescar instalados", [this] { refresh(); });
            }
            recycler_->setVisibility(brls::Visibility::GONE);
            recycler_->setFocusable(false);
            emptyState_->setVisibility(brls::Visibility::VISIBLE);
            emptyState_->setFocusable(true);
            if (emptyState_->getDefaultFocus()) {
                brls::Application::giveFocus(emptyState_->getDefaultFocus());
            }
        } else {
            emptyState_->setVisibility(brls::Visibility::GONE);
            emptyState_->setFocusable(false);
            recycler_->setVisibility(brls::Visibility::VISIBLE);
            recycler_->setFocusable(true);
        }

        if (showUpdatesOnly_) {
            status_->setText(std::to_string(count) +
                (count == 1 ? " actualización disponible" : " actualizaciones disponibles"));
        } else {
            status_->setText(std::to_string(count) +
                (count == 1 ? " aplicación instalada" : " aplicaciones instaladas"));
        }
        updateSortLabel();
    }

    void refresh() {
        if (refreshing_)
            return;
        if (hasActiveStreamInstall()) {
            brls::Application::notify(
                "Installed games will refresh after streaming installation finishes.");
            return;
        }
        refreshing_ = true;
        status_->setText("Refreshing installed applications...");
        auto alive = alive_;
        InstalledTitleService* installed = installed_;
        brls::async([this, alive, installed] {
            std::string error;
            bool ok = installed->refresh(error);
            brls::sync([this, alive, ok, error] {
                if (!alive->load())
                    return;
                refreshing_ = false;
                if (!ok) {
                    status_->setText(error);
                    brls::Application::notify(error);
                    return;
                }
                reload();
            });
        });
    }

    InstalledTitleService* installed_;
    DownloadManager* manager_;
    GameMetadataService* metadata_;
    CatalogService* catalog_;
    AppSettings* settings_;
    std::shared_ptr<std::atomic<bool>> alive_;
    brls::Label* status_;
    brls::Label* sortLabel_;
    brls::Box* contentBox_ = nullptr;
    brls::RecyclerFrame* recycler_;
    brls::BottomBar* bottomBar_ = nullptr;
    InstalledDataSource* dataSource_;
    EmptyStateView* emptyState_ = nullptr;
    bool refreshing_ = false;
    int columns_ = 6;
    std::string searchQuery_ = "";
    bool showUpdatesOnly_ = false;
    brls::ActionIdentifier updateActionId_ = 0;
};

}  // namespace goonies::ui
