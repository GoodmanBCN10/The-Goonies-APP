#include "forwarders_view.hpp"
#include "forwarder_details_view.hpp"
#include "ui/theme.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/common/message_cells.hpp"
#include <algorithm>

namespace pipensx::ui {

// --- HomebrewGridItem ---
HomebrewGridItem::HomebrewGridItem() : brls::Box(brls::Axis::COLUMN) {
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
    
    icon_ = new brls::Image();
    icon_->setWidth(140);
    icon_->setHeight(140);
    icon_->setPositionType(brls::PositionType::ABSOLUTE);
    icon_->setPositionTop(0);
    icon_->setPositionLeft(0);
    icon_->setScalingType(brls::ImageScalingType::FILL);
    cover_->addView(icon_);
    
    addView(cover_);

    nameLabel_ = new brls::Label();
    nameLabel_->setSingleLine(true);
    nameLabel_->setAutoAnimate(false);
    nameLabel_->setFontSize(18);
    nameLabel_->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    nameLabel_->setTextColor(theme::textPrimary());
    addView(nameLabel_);

    authorLabel_ = new brls::Label();
    authorLabel_->setSingleLine(true);
    authorLabel_->setAutoAnimate(false);
    authorLabel_->setFontSize(14);
    authorLabel_->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    authorLabel_->setTextColor(theme::textTertiary());
    authorLabel_->setMarginTop(4);
    addView(authorLabel_);

    this->registerClickAction([this](brls::View* view) {
        if (onClick_) onClick_(this->title_);
        return true;
    });
}

void HomebrewGridItem::setTitle(const HomebrewTitle& title, std::function<void(const HomebrewTitle&)> onClick) {
    title_ = title;
    onClick_ = onClick;
    this->setVisibility(brls::Visibility::VISIBLE);
    
    nameLabel_->setText(title.name);
    authorLabel_->setText(title.author);
    
    if (!title.icon.empty()) {
        icon_->setImageFromMem(title.icon.data(), title.icon.size());
    } else {
        icon_->setImageFromFile("romfs:/icon.jpg");
    }
}

void HomebrewGridItem::clear() {
    this->setVisibility(brls::Visibility::INVISIBLE);
    this->setFocusable(false);
}

void HomebrewGridItem::onFocusGained() {
    brls::Box::onFocusGained();
    nameLabel_->setAnimated(true);
    authorLabel_->setAnimated(true);
}

void HomebrewGridItem::onFocusLost() {
    brls::Box::onFocusLost();
    nameLabel_->setAnimated(false);
    authorLabel_->setAnimated(false);
}

// --- HomebrewGridRow ---
HomebrewGridRow::HomebrewGridRow(int numColumns) : numColumns_(numColumns) {
    setFocusable(false);
    setHeight(230);
    setAxis(brls::Axis::ROW);
    setAlignItems(brls::AlignItems::STRETCH);
    setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    setPadding(5, 0, 10, 0);
    
    for (int i = 0; i < numColumns; i++) {
        auto* item = new HomebrewGridItem();
        item->setGrow(1);
        items_.push_back(item);
        addView(item);
    }
}

void HomebrewGridRow::populate(const std::vector<HomebrewTitle>& titles, int startIndex, std::function<void(const HomebrewTitle&)> onClick) {
    for (int i = 0; i < numColumns_; i++) {
        if (startIndex + i < titles.size()) {
            items_[i]->setTitle(titles[startIndex + i], onClick);
            items_[i]->setFocusable(true);
        } else {
            items_[i]->clear();
            items_[i]->setFocusable(false);
        }
    }
}

// --- HomebrewCell ---
HomebrewCell::HomebrewCell() {
    this->setFocusable(true);
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(10, 18, 10, 18);
    this->setHeight(92);
    this->setWidth(brls::View::AUTO);
    
    icon_ = new brls::Image();
    icon_->setWidth(64);
    icon_->setHeight(64);
    icon_->setCornerRadius(8);
    icon_->setMarginRight(16);
    icon_->setScalingType(brls::ImageScalingType::FIT); // Use FIT like grid view
    this->addView(icon_);
    
    brls::Box* textContainer = new brls::Box(brls::Axis::COLUMN);
    textContainer->setGrow(1);
    textContainer->setAlignItems(brls::AlignItems::FLEX_START);
    
    nameLabel_ = new brls::Label();
    nameLabel_->setSingleLine(true);
    nameLabel_->setFontSize(21);
    nameLabel_->setWidth(brls::View::AUTO);
    nameLabel_->setHeight(brls::View::AUTO);
    textContainer->addView(nameLabel_);
    
    authorLabel_ = new brls::Label();
    authorLabel_->setSingleLine(true);
    authorLabel_->setFontSize(15);
    authorLabel_->setMarginTop(6);
    authorLabel_->setTextColor(theme::textTertiary());
    authorLabel_->setWidth(brls::View::AUTO);
    authorLabel_->setHeight(brls::View::AUTO);
    textContainer->addView(authorLabel_);
    
    this->addView(textContainer);

    this->registerClickAction([this](brls::View* view) {
        if (onClick_) onClick_(this->title_);
        return true;
    });
}

void HomebrewCell::populate(const HomebrewTitle& title, std::function<void(const HomebrewTitle&)> onClick) {
    title_ = title;
    onClick_ = onClick;

    this->setVisibility(brls::Visibility::VISIBLE);
    this->setFocusable(true);

    nameLabel_->setText(title.name.empty() ? "Desconocido" : title.name);
    authorLabel_->setText(title.author.empty() ? "Autor Desconocido" : title.author);
    
    if (!title.icon.empty()) {
        icon_->setImageFromMem(title.icon.data(), title.icon.size());
    } else {
        icon_->setImageFromFile("romfs:/icon.jpg");
    }
}

// --- ForwardersDataSource ---
ForwardersDataSource::ForwardersDataSource(HomebrewService* service, std::function<void(const HomebrewTitle&)> onClick)
    : service_(service), onClick_(onClick) {}

void ForwardersDataSource::setTitles(const std::vector<HomebrewTitle>& titles, const std::string& query) {
    titles_.clear();
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& t : titles) {
        if (lowerQuery.empty()) {
            titles_.push_back(t);
        } else {
            std::string lowerName = t.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (lowerName.find(lowerQuery) != std::string::npos) {
                titles_.push_back(t);
            }
        }
    }
}

void ForwardersDataSource::setGridView(bool grid, int columns) {
    isGridView_ = grid;
    columns_ = columns;
}

int ForwardersDataSource::numberOfRows(brls::RecyclerFrame* recycler, int section) {
    if (titles_.empty()) return 1;
    if (isGridView_) {
        return (titles_.size() + columns_ - 1) / columns_;
    }
    return titles_.size();
}

brls::RecyclerCell* ForwardersDataSource::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) {
    if (titles_.empty()) {
        auto* cell = recycler->dequeueReusableCell("Message");
        dynamic_cast<pipensx::ui::TextMessageCell*>(cell)->setMessage("No se han encontrado aplicaciones homebrew.");
        return cell;
    }

    if (isGridView_) {
        HomebrewGridRow* cell = dynamic_cast<HomebrewGridRow*>(recycler->dequeueReusableCell("GridRow"));
        cell->populate(titles_, index.row * columns_, onClick_);
        return cell;
    } else {
        HomebrewCell* cell = dynamic_cast<HomebrewCell*>(recycler->dequeueReusableCell("HomebrewCell"));
        cell->populate(titles_[index.row], onClick_);
        return cell;
    }
}

float ForwardersDataSource::heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) {
    if (titles_.empty()) return 100;
    return isGridView_ ? 250 : 92;
}

// --- ForwardersView ---
ForwardersView::ForwardersView(HomebrewService* service)
    : brls::Box(brls::Axis::COLUMN), service_(service) {
    
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* header = new brls::Box(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setPadding(40, 80, 20, 80);
    
    brls::Label* title = new brls::Label();
    title->setText("Aplicaciones Homebrew");
    title->setFontSize(28);
    title->setTextColor(theme::accent());
    header->addView(title);
    
    this->addView(header);
    
    recycler_ = new brls::RecyclerFrame();
    recycler_->setGrow(1);
    recycler_->setPadding(0, 80, 40, 80);
    recycler_->estimatedRowHeight = 250;
    
    auto onClick = [this](const HomebrewTitle& title) {
        brls::Application::pushActivity(new brls::Activity(new ForwarderDetailsView(title)));
    };
    
    dataSource_ = new ForwardersDataSource(service_, onClick);
    dataSource_->setGridView(true, 6);
    recycler_->setDataSource(dataSource_);
    
    recycler_->registerCell("GridRow", []() { return new HomebrewGridRow(6); });
    recycler_->registerCell("HomebrewCell", []() { return new HomebrewCell(); });
    recycler_->registerCell("Message", []() { return new TextMessageCell(); });
    
    this->addView(recycler_);
    this->addView(new brls::BottomBar());
    
    std::string err;
    if (service_->refresh(err)) {
        auto titles = service_->titles();
        dataSource_->setTitles(titles, "");
        recycler_->reloadData();
    } else {
        // error
        dataSource_->setTitles({}, "");
        recycler_->reloadData();
    }

    this->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::SOUND_BACK);
    
    // Vista toggle button was removed

    this->registerAction("Buscar", brls::BUTTON_Y, [this](brls::View*) {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            auto titles = service_->titles();
            dataSource_->setTitles(titles, text);
            recycler_->reloadData();
            brls::Application::giveFocus(this);
        }, "Buscar homebrew", "Introduce el nombre del homebrew", 100, "", 0);
        return true;
    });
}

} // namespace pipensx::ui
