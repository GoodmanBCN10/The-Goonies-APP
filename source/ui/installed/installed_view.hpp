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
#include <algorithm>

namespace pipensx::ui {

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
                  std::function<void(const InstalledTitle&)> onClick = nullptr) {
        title_->setText(title.name);
        std::string subtitle = title.publisher;
        if (!subtitle.empty())
            subtitle += "   ";
        subtitle += title.titleId;
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
    std::string currentIconPath_;
    std::shared_ptr<ImageRequestState> imageState_ =
        std::make_shared<ImageRequestState>();
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

    void setTitle(const InstalledTitle& title, GameMetadataService* metadata, std::function<void(const InstalledTitle&)> onClick = nullptr) {
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

    void setTitles(const std::vector<InstalledTitle>& titles, size_t offset, int columns, GameMetadataService* metadata, std::function<void(const InstalledTitle&)> onClick = nullptr) {
        for (int i = 0; i < columns; i++) {
            if (offset + i < titles.size()) {
                items_[i]->setTitle(titles[offset + i], metadata, onClick);
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
    explicit InstalledDataSource(GameMetadataService* metadata, std::function<void(const InstalledTitle&)> onClick = nullptr)
        : metadata_(metadata), onClick_(onClick) {}

    void setTitles(std::vector<InstalledTitle> titles, const std::string& query = "") {
        if (query.empty()) {
            titles_ = std::move(titles);
        } else {
            titles_.clear();
            std::string q = query;
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            for (const auto& t : titles) {
                std::string n = t.name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(q) != std::string::npos || t.titleId.find(q) != std::string::npos) {
                    titles_.push_back(t);
                }
            }
        }
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
            cell->setMessage(t("No se encontraron aplicaciones instaladas.", "No installed applications found."));
            return cell;
        }
        
        auto* cell = static_cast<InstalledGridRow*>(recycler->dequeueReusableCell("GridRow"));
        cell->setTitles(titles_, index.row * columns_, columns_, metadata_, onClick_);
        return cell;
    }

private:
    GameMetadataService* metadata_;
    std::vector<InstalledTitle> titles_;
    int columns_ = 6;
    std::function<void(const InstalledTitle&)> onClick_;
};

class InstalledView : public brls::Box {
public:
    InstalledView(InstalledTitleService* installed, DownloadManager* manager,
                  GameMetadataService* metadata)
        : brls::Box(brls::Axis::COLUMN), installed_(installed),
          manager_(manager), alive_(std::make_shared<std::atomic<bool>>(true)) {
        status_ = new brls::Label();
        status_->setFontSize(15);
        status_->setMarginTop(10);
        status_->setMarginLeft(34);
        status_->setTextColor(theme::textTertiary());
        addView(status_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->setPadding(6, 32, 6, 32);
        
        recycler_->registerCell("Installed", [] { return new InstalledCell(); });
        recycler_->registerCell("GridRow", [this] { return new InstalledGridRow(columns_); });
        recycler_->registerCell("Message", [] { return new TextMessageCell(); });
        
        dataSource_ = new InstalledDataSource(metadata);
        recycler_->setDataSource(dataSource_);
        addView(recycler_);
        
        addView(new brls::BottomBar());
        
        updateViewMode();
        reload();

        registerAction("Refresh", brls::BUTTON_RB, [this](brls::View*) {
            refresh();
            return true;
        });

        registerAction("Buscar", brls::BUTTON_Y, [this](brls::View*) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                searchQuery_ = text;
                reload();
                brls::Application::giveFocus(this);
            }, "Buscar juegos", "Introduce el nombre o TitleID", 100, searchQuery_, 0);
            return true;
        });

        // Vista action was removed
        this->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }

    ~InstalledView() override { alive_->store(false); }

private:
    void updateViewMode() {
        recycler_->estimatedRowHeight = 230;
        dataSource_->setGridView(true, columns_);
    }

    EmptyStateView* ensureEmptyState() {
        if (emptyState_)
            return emptyState_;
        emptyState_ = new EmptyStateView();
        emptyState_->setContent(
            "No installed applications",
            "Refresh this list after your first install or when you return "
            "from Home.",
            "Refresh installed", [this] { refresh(); });
        addView(emptyState_);
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
        size_t count = titles.size();
        dataSource_->setTitles(std::move(titles), searchQuery_);
        recycler_->reloadData();
        const bool empty = count == 0;
        if (empty)
            ensureEmptyState()->setVisibility(brls::Visibility::VISIBLE);
        else if (emptyState_)
            emptyState_->setVisibility(brls::Visibility::GONE);
        recycler_->setVisibility(empty ? brls::Visibility::GONE
                                       : brls::Visibility::VISIBLE);
        status_->setText(std::to_string(count) +
            (count == 1 ? " installed application" : " installed applications"));
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
    brls::Label* status_ = nullptr;
    EmptyStateView* emptyState_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    InstalledDataSource* dataSource_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_;
    bool refreshing_ = false;
    int columns_ = 6;
    std::string searchQuery_ = "";
};

}  // namespace pipensx::ui
