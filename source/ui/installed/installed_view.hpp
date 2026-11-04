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
#include "app/update_dismiss_store.hpp"
#include "app/update_verifier.hpp"
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
    struct Candidate {
        const CatalogEntry* entry = nullptr;
        uint32_t version = 0;
        bool isExplicitUpdate = false;
    };

    void build(const std::vector<CatalogEntry>& entries, GameMetadataService* metadata, bool force = false) {
        if (built_ && !force && entries.size() == lastSize_) {
            return;
        }

        metadata_ = metadata;
        byBaseAppId_.clear();

        for (const auto& entry : entries) {
            if (entry.isHiddenByDefault()) continue;

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
            if (tid == 0) {
                tid = parseTitleIdFromFileName(entry.title);
            }
            if (tid == 0) continue;

            Candidate c = evaluateEntry(entry, tid);

            uint64_t baseAppId = (tid & ~0x1FFFULL);
            if (baseAppId != 0) {
                byBaseAppId_[baseAppId].push_back(c);
            }
        }

        built_ = true;
        lastSize_ = entries.size();
    }

    static Candidate evaluateEntry(const CatalogEntry& entry, uint64_t tid) {
        Candidate c;
        c.entry = &entry;
        c.version = entry.updateVersion;

        // Title ID ending in 0x800 is explicitly a patch
        if (tid != 0 && (tid & 0x1FFFULL) == 0x800ULL) {
            c.isExplicitUpdate = true;
        }

        std::string lower = entry.title;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("[upd") != std::string::npos ||
            lower.find("(upd") != std::string::npos ||
            lower.find(" update") != std::string::npos ||
            lower.find("+ update") != std::string::npos ||
            lower.find("+update") != std::string::npos ||
            lower.find("+ upd") != std::string::npos ||
            lower.find("+upd") != std::string::npos ||
            lower.find("patch") != std::string::npos ||
            lower.find("actualizacion") != std::string::npos ||
            lower.find("actualización") != std::string::npos) {
            c.isExplicitUpdate = true;
        }

        if (entry.updateVersion > 0) {
            c.isExplicitUpdate = true;
        }

        uint32_t parsedV = parseVersionFromFileName(entry.title);
        if (parsedV > 0 && parsedV < 1000) {
            parsedV *= 65536;
        }
        if (parsedV > c.version) {
            c.version = parsedV;
        }

        return c;
    }

    static std::string lowerHash(const std::string& h) {
        std::string s = h;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    // Candidates for a game, best first: torrents verified to hold a newer
    // update come before unverified ones.
    std::vector<Candidate> sortedCandidates(const InstalledTitle& title) const {
        uint64_t baseAppId = title.applicationId & ~0x1FFFULL;
        auto it = byBaseAppId_.find(baseAppId);
        if (it == byBaseAppId_.end() || it->second.empty()) return {};

        std::vector<Candidate> candidates = it->second;
        auto verified = [&](const Candidate& c) {
            return UpdateVerifier::instance().check(
                       lowerHash(c.entry->infoHash), title.internalVersion) ==
                   UpdateVerifier::State::Yes;
        };
        std::stable_sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
            bool va = verified(a), vb = verified(b);
            if (va != vb) return va;
            if (a.isExplicitUpdate != b.isExplicitUpdate) return a.isExplicitUpdate;
            if (a.version != b.version) return a.version > b.version;
            if (a.entry->peerCount != b.entry->peerCount) return a.entry->peerCount > b.entry->peerCount;
            return a.entry->topicId > b.entry->topicId;
        });
        return candidates;
    }

    const CatalogEntry* findBestUpdate(const InstalledTitle& title) const {
        auto candidates = sortedCandidates(title);
        return candidates.empty() ? nullptr : candidates.front().entry;
    }

    uint32_t officialLatest(const InstalledTitle& title) const {
        uint32_t latest = NxVersionsService::instance().getLatestVersion(title.titleId);
        if (latest == 0 && metadata_) {
            auto meta = metadata_->findByTitleId(title.titleId);
            if (meta) latest = meta->latestVersion;
        }
        return latest;
    }

    // Official version is newer than installed and the catalog has torrents
    // for the game: worth inspecting, but not yet proven to be an update.
    bool isScanCandidate(const InstalledTitle& title) const {
        uint32_t latest = officialLatest(title);
        return latest > 0 && title.internalVersion < latest &&
               byBaseAppId_.count(title.applicationId & ~0x1FFFULL) != 0;
    }

    // Up to `limit` torrents of this game that still need inspecting.
    std::vector<const CatalogEntry*> torrentsToScan(const InstalledTitle& title,
                                                    size_t limit = 3) const {
        std::vector<const CatalogEntry*> out;
        if (!isScanCandidate(title)) return out;
        for (const auto& c : sortedCandidates(title)) {
            if (out.size() >= limit) break;
            if (!UpdateVerifier::instance().isKnown(lowerHash(c.entry->infoHash)))
                out.push_back(c.entry);
        }
        return out;
    }

    // An update is listed only when one of the game's catalog torrents was
    // verified (by reading its file list) to contain a package newer than the
    // installed version.
    bool hasUpdateFor(const InstalledTitle& title) const {
        if (!isScanCandidate(title)) return false;
        for (const auto& c : sortedCandidates(title)) {
            if (UpdateVerifier::instance().check(lowerHash(c.entry->infoHash),
                                                 title.internalVersion) ==
                UpdateVerifier::State::Yes)
                return true;
        }
        return false;
    }

    uint32_t getOfferedVersion(const InstalledTitle& title) const {
        const CatalogEntry* best = findBestUpdate(title);
        if (best) {
            uint32_t v = best->updateVersion;
            uint32_t parsed = parseVersionFromFileName(best->title);
            if (parsed > 0 && parsed < 1000) parsed *= 65536;
            if (parsed > v) v = parsed;
            if (v > 0) return v;
        }
        return officialLatest(title);
    }

    void setMetadataService(GameMetadataService* metadata) {
        metadata_ = metadata;
    }

    void invalidate() {
        built_ = false;
    }

private:
    std::unordered_map<uint64_t, std::vector<Candidate>> byBaseAppId_;
    GameMetadataService* metadata_ = nullptr;
    bool built_ = false;
    size_t lastSize_ = 0;
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
        if (onFocus_) onFocus_();
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        title_->setAnimated(false);
        subtitle_->setAnimated(false);
    }

    void setOnFocus(std::function<void()> cb) {
        onFocus_ = std::move(cb);
    }

private:
    std::function<void()> onFocus_;
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
    InstalledGridRow(int columns, std::shared_ptr<int> focusColumn = nullptr)
        : focusColumn_(std::move(focusColumn)) {
        setFocusable(false);
        setHeight(230);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::STRETCH);
        setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        setPadding(5, 0, 10, 0);
        
        for (int i = 0; i < columns; i++) {
            auto* item = new InstalledGridItem();
            item->setGrow(1);
            std::shared_ptr<int> colPtr = focusColumn_;
            item->setOnFocus([colPtr, i]() {
                if (colPtr) *colPtr = i;
            });
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

    brls::View* getDefaultFocus() override {
        int targetCol = (focusColumn_ ? *focusColumn_ : 0);
        if (targetCol >= 0 && targetCol < (int)items_.size()) {
            if (items_[targetCol] && items_[targetCol]->isFocusable() && items_[targetCol]->getVisibility() == brls::Visibility::VISIBLE) {
                return items_[targetCol];
            }
            for (int i = targetCol - 1; i >= 0; i--) {
                if (items_[i] && items_[i]->isFocusable() && items_[i]->getVisibility() == brls::Visibility::VISIBLE) {
                    return items_[i];
                }
            }
        }
        for (auto* item : items_) {
            if (item && item->isFocusable() && item->getVisibility() == brls::Visibility::VISIBLE) {
                return item;
            }
        }
        return nullptr;
    }

private:
    std::shared_ptr<int> focusColumn_;
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
        this->setBackgroundColor(nvgRGB(18, 19, 24));

        status_ = new brls::Label();
        status_->setFontSize(15);
        status_->setMarginTop(10);
        status_->setMarginLeft(34);
        status_->setTextColor(theme::neonPurple());
        
        sortLabel_ = new brls::Label();
        sortLabel_->setFontSize(15);
        sortLabel_->setMarginTop(10);
        sortLabel_->setMarginRight(34);
        sortLabel_->setTextColor(theme::neonPurple());
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
        recycler_->setPadding(6, 32, 60, 32);
        
        recycler_->registerCell("Installed", [] { return new InstalledCell(); });
        recycler_->registerCell("GridRow", [this] { return new InstalledGridRow(columns_, focusColumn_); });
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
            if (showUpdatesOnly_) startUpdateScan();
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

    ~InstalledView() override {
        alive_->store(false);
        UpdateVerifier::instance().cancel();
    }

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

        brls::sync([this, bestEntry]() {
            brls::Application::pushActivity(
                new GameDetailActivity(bestEntry, "", manager_, metadata_, this->installed_, settings_,
                    [](const std::string&, const std::string&){},
                    [this](){ this->refresh(); },
                    [this](){ this->refresh(); }));
        });
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

        // Only grab focus if this list currently owns it; a background refresh
        // must not pull the cursor away from a detail page opened on top.
        bool ownsFocus = true;
        if (brls::View* f = brls::Application::getCurrentFocus()) {
            ownsFocus = false;
            for (brls::View* cur = f; cur; cur = cur->hasParent() ? cur->getParent() : nullptr) {
                if (cur == this) { ownsFocus = true; break; }
            }
        }

        std::vector<InstalledTitle> titles = installed_->titles();
        dataSource_->setTitles(std::move(titles), searchQuery_);
        recycler_->reloadData();

        size_t count = dataSource_->count();
        const bool empty = (count == 0);
        if (empty) {
            if (showUpdatesOnly_ && UpdateVerifier::instance().pending() > 0) {
                emptyState_->setContent(
                    "Comprobando actualizaciones...",
                    "Se están revisando los torrents del catálogo (" + std::to_string(UpdateVerifier::instance().pending()) +
                    " pendientes). La primera vez puede tardar unos minutos.",
                    "Refrescar", [this] { refresh(); });
            } else if (showUpdatesOnly_) {
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
            if (ownsFocus && emptyState_->getDefaultFocus()) {
                brls::Application::giveFocus(emptyState_->getDefaultFocus());
            }
        } else {
            emptyState_->setVisibility(brls::Visibility::GONE);
            emptyState_->setFocusable(false);
            recycler_->setVisibility(brls::Visibility::VISIBLE);
            recycler_->setFocusable(true);
            recycler_->selectRowAt(brls::IndexPath(0, 0), false);
            if (ownsFocus)
                brls::Application::giveFocus(recycler_);
        }

        updateStatusText(count);
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
        catalogIndex_.invalidate();
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
                if (showUpdatesOnly_) startUpdateScan();
                reload();
            });
        });
    }

    // Inspect the catalog torrents of games that might have an update (official
    // version newer than installed) to confirm they really hold a newer package.
    void startUpdateScan() {
        if (!catalog_ || !installed_) return;
        catalogIndex_.build(catalog_->entries(), metadata_);
        std::vector<UpdateVerifier::Job> jobs;
        for (const auto& t : installed_->titles()) {
            for (const CatalogEntry* e : catalogIndex_.torrentsToScan(t)) {
                UpdateVerifier::Job j;
                j.infoHash = CatalogUpdateIndex::lowerHash(e->infoHash);
                j.magnet = e->magnetUri;
                j.infoDict = e->infoDict;
                j.appId = t.applicationId;
                jobs.push_back(std::move(j));
            }
        }
        if (jobs.empty()) return;
        auto alive = alive_;
        UpdateVerifier::instance().enqueue(std::move(jobs), manager_->rootPath() + "/downloads",
            [this, alive] {
                if (!alive->load()) return;
                onScanProgress();
            });
    }

    void onScanProgress() {
        size_t positives = 0;
        for (const auto& t : installed_->titles())
            if (catalogIndex_.hasUpdateFor(t)) ++positives;
        const bool done = UpdateVerifier::instance().pending() == 0;
        if (showUpdatesOnly_ && (positives != lastPositives_ || done)) {
            lastPositives_ = positives;
            reload();
        } else {
            lastPositives_ = positives;
            if (showUpdatesOnly_) updateStatusText(dataSource_->count());
        }
    }

    void updateStatusText(size_t count) {
        if (showUpdatesOnly_) {
            std::string text = std::to_string(count) +
                (count == 1 ? " actualización disponible" : " actualizaciones disponibles");
            size_t pending = UpdateVerifier::instance().pending();
            if (pending > 0)
                text += "  -  comprobando " + std::to_string(pending) + " torrents del catálogo...";
            status_->setText(text);
        } else {
            status_->setText(std::to_string(count) +
                (count == 1 ? " aplicación instalada" : " aplicaciones instaladas"));
        }
    }

    size_t lastPositives_ = 0;
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
    std::shared_ptr<int> focusColumn_ = std::make_shared<int>(0);
    std::string searchQuery_ = "";
    bool showUpdatesOnly_ = false;
};

}  // namespace goonies::ui
