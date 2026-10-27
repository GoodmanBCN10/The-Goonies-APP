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
                  uint32_t offeredVersion,
                  bool isUpdateList,
                  std::function<void(const InstalledTitle&)> onClick = nullptr) {
        title_->setText(title.name);
        std::string subtitle = title.publisher;
        if (!subtitle.empty())
            subtitle += "   ";
        subtitle += title.titleId;

        if (isUpdateList) {
            uint32_t latestVer = offeredVersion;
            if (latestVer == 0 && metadata) {
                auto meta = metadata->findByTitleId(title.titleId);
                if (meta) latestVer = meta->latestVersion;
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

class CatalogUpdateIndex {
public:
    struct UpdateEntry {
        const CatalogEntry* entry = nullptr;
        uint32_t version = 0;
        bool isExplicitUpdate = false;
    };

    void build(const std::vector<CatalogEntry>& entries, GameMetadataService* metadata) {
        byBaseAppId_.clear();
        byCleanName_.clear();

        for (const auto& entry : entries) {
            if (entry.isHiddenByDefault()) continue;

            UpdateEntry u;
            u.entry = &entry;
            u.version = entry.updateVersion;

            uint64_t tid = 0;
            if (!entry.titleId.empty()) {
                try { tid = std::stoull(entry.titleId, nullptr, 16); } catch (...) {}
            }
            if (tid == 0 && metadata) {
                const GameMetadata* meta = metadata->findByInfoHash(entry.infoHash);
                if (meta && !meta->titleId.empty()) {
                    try { tid = std::stoull(meta->titleId, nullptr, 16); } catch (...) {}
                }
            }

            if ((tid & 0x1FFFULL) == 0x800ULL) {
                u.isExplicitUpdate = true;
            }

            std::string lower = entry.title;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("[upd") != std::string::npos ||
                lower.find(" update") != std::string::npos ||
                lower.find("+ update") != std::string::npos ||
                lower.find("+update") != std::string::npos ||
                lower.find("patch") != std::string::npos ||
                lower.find("actualizacion") != std::string::npos) {
                u.isExplicitUpdate = true;
            }

            // Exclude entries that do not offer an update (base games, pure DLCs)
            if (!u.isExplicitUpdate && u.version == 0) {
                continue;
            }

            uint32_t parsedV = parseVersionFromFileName(entry.title);
            if (parsedV > 0 && parsedV < 1000) {
                parsedV *= 65536;
            }
            if (parsedV > u.version) {
                u.version = parsedV;
            }

            uint64_t baseAppId = (tid != 0) ? (tid & ~0x1FFFULL) : 0;
            if (baseAppId != 0) {
                byBaseAppId_[baseAppId].push_back(u);
            } else {
                std::string clean = cleanTitle(entry.title);
                std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);
                byCleanName_.push_back({clean, u});
            }
        }
    }

    static bool offersUpdate(const UpdateEntry& u, uint32_t installedVersion) {
        if (!u.isExplicitUpdate && u.version == 0) return false;
        if (u.version > 0) {
            return u.version > installedVersion;
        }
        return (installedVersion == 0);
    }

    bool hasUpdateFor(const InstalledTitle& title) const {
        uint32_t latestVer = NxVersionsService::instance().getLatestVersion(title.titleId);
        if (latestVer > 0 && latestVer <= title.internalVersion) {
            return false;
        }

        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        auto it = byBaseAppId_.find(baseAppId);
        if (it != byBaseAppId_.end()) {
            for (const auto& u : it->second) {
                if (offersUpdate(u, title.internalVersion)) {
                    return true;
                }
            }
        }

        for (const auto& item : byCleanName_) {
            if (releaseNameMatches(title.name, item.first)) {
                if (offersUpdate(item.second, title.internalVersion)) {
                    return true;
                }
            }
        }

        return false;
    }

    uint32_t getOfferedVersion(const InstalledTitle& title) const {
        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        uint32_t bestV = 0;
        auto it = byBaseAppId_.find(baseAppId);
        if (it != byBaseAppId_.end()) {
            for (const auto& u : it->second) {
                if (offersUpdate(u, title.internalVersion) && u.version > bestV) {
                    bestV = u.version;
                }
            }
        }
        if (bestV == 0) {
            for (const auto& item : byCleanName_) {
                if (releaseNameMatches(title.name, item.first)) {
                    if (offersUpdate(item.second, title.internalVersion) && item.second.version > bestV) {
                        bestV = item.second.version;
                    }
                }
            }
        }
        return bestV;
    }

    const CatalogEntry* findBestUpdate(const InstalledTitle& title) const {
        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        std::vector<UpdateEntry> candidates;

        auto it = byBaseAppId_.find(baseAppId);
        if (it != byBaseAppId_.end()) {
            for (const auto& u : it->second) {
                if (offersUpdate(u, title.internalVersion)) {
                    candidates.push_back(u);
                }
            }
        }

        for (const auto& item : byCleanName_) {
            if (releaseNameMatches(title.name, item.first)) {
                if (offersUpdate(item.second, title.internalVersion)) {
                    candidates.push_back(item.second);
                }
            }
        }

        if (candidates.empty()) return nullptr;

        std::stable_sort(candidates.begin(), candidates.end(), [](const UpdateEntry& a, const UpdateEntry& b) {
            if (a.version != b.version) return a.version > b.version;
            if (a.entry->peerCount != b.entry->peerCount) return a.entry->peerCount > b.entry->peerCount;
            return a.entry->topicId > b.entry->topicId;
        });

        return candidates.front().entry;
    }

private:
    std::unordered_map<uint64_t, std::vector<UpdateEntry>> byBaseAppId_;
    std::vector<std::pair<std::string, UpdateEntry>> byCleanName_;
};

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

    explicit InstalledDataSource(GameMetadataService* metadata, CatalogService* catalog,
                                AppSettings* settings, DownloadManager* manager,
                                std::function<void(const InstalledTitle&)> onClick = nullptr,
                                const CatalogUpdateIndex* catalogIndex = nullptr)
        : metadata_(metadata), catalog_(catalog), settings_(settings), manager_(manager),
          catalogIndex_(catalogIndex), onClick_(onClick) {}

    void setFilterUpdates(bool filter) {
        showUpdatesOnly_ = filter;
    }

    void setTitles(std::vector<InstalledTitle> titles, const std::string& query = "") {
        titles_.clear();
        std::string q = query;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        
        for (const auto& t : titles) {
            if (showUpdatesOnly_) {
                if (!catalogIndex_ || !catalogIndex_->hasUpdateFor(t)) {
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
            uint32_t offeredVer = catalogIndex_ ? catalogIndex_->getOfferedVersion(titles_[index.row]) : 0;
            cell->setTitle(titles_[index.row], metadata_, offeredVer, showUpdatesOnly_, onClick_);
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
    const CatalogUpdateIndex* catalogIndex_ = nullptr;
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
        recycler_->setPadding(6, 32, 28, 32);
        
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
        
        dataSource_ = new InstalledDataSource(metadata, catalog, settings, manager, onClick, &catalogIndex_);
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
        if (installed_->titles().empty()) {
            refresh();
        }

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

        registerAction("Ver Updates", brls::BUTTON_Y, [this](brls::View*) {
            showUpdatesOnly_ = !showUpdatesOnly_;
            updateViewMode();
            this->updateActionHint(brls::BUTTON_Y, showUpdatesOnly_ ? "Ver Todos" : "Ver Updates");
            dataSource_->setFilterUpdates(showUpdatesOnly_);
            reload();
            return true;
        });

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
        const CatalogEntry* best = catalogIndex_.findBestUpdate(title);
        if (!best) {
            uint32_t latestVer = NxVersionsService::instance().getLatestVersion(title.titleId);
            std::string verText = "";
            if (latestVer > 0) {
                verText = " (v" + std::to_string(latestVer / 65536) + " / #" + std::to_string(latestVer) + ")";
            }
            std::string msg = "Actualizacion no disponible en el catalogo\n\n" + title.name +
                              " tiene una actualizacion oficial disponible en Nintendo" + verText +
                              ", pero actualmente no hay ningun torrent de actualizacion disponible en el catalogo para este juego.";
            auto* dialog = new brls::Dialog(msg);
            dialog->addButton("Ver detalles", [this, title]() {
                brls::Application::pushActivity(new brls::Activity(new InstalledDetailsView(title, metadata_)));
            });
            dialog->addButton("Aceptar", []() {});
            dialog->open();
            return;
        }

        CatalogEntry bestEntry = *best;
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
        if (catalog_) {
            catalogIndex_.build(catalog_->entries(), metadata_);
        }

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
            recycler_->selectRowAt(brls::IndexPath(0, 0), false);
            brls::Application::giveFocus(recycler_);
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
    CatalogUpdateIndex catalogIndex_;
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
};

}  // namespace goonies::ui
