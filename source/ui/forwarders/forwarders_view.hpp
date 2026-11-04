#pragma once
#include <borealis.hpp>
#include "app/homebrew_service.hpp"
#include <functional>

namespace goonies::ui {

class HomebrewGridItem : public brls::Box {
public:
    HomebrewGridItem();
    void setTitle(const HomebrewTitle& title, std::function<void(const HomebrewTitle&)> onClick);
    void clear();
    void onFocusGained() override;
    void onFocusLost() override;
    void setOnFocus(std::function<void()> cb) { onFocus_ = std::move(cb); }

private:
    std::function<void()> onFocus_;
    brls::Box* cover_ = nullptr;
    brls::Image* icon_ = nullptr;
    brls::Label* nameLabel_ = nullptr;
    brls::Label* authorLabel_ = nullptr;
    HomebrewTitle title_;
    std::function<void(const HomebrewTitle&)> onClick_;
};

class HomebrewGridRow : public brls::RecyclerCell {
public:
    HomebrewGridRow(int numColumns, std::shared_ptr<int> focusColumn = nullptr);
    void populate(const std::vector<HomebrewTitle>& titles, int startIndex, std::function<void(const HomebrewTitle&)> onClick);
    brls::View* getDefaultFocus() override;
    static brls::RecyclerCell* create(int numColumns) { return new HomebrewGridRow(numColumns); }

private:
    std::shared_ptr<int> focusColumn_;
    int numColumns_;
    std::vector<HomebrewGridItem*> items_;
};

class HomebrewCell : public brls::RecyclerCell {
public:
    HomebrewCell();
    void populate(const HomebrewTitle& title, std::function<void(const HomebrewTitle&)> onClick);
    static brls::RecyclerCell* create() { return new HomebrewCell(); }

private:
    brls::Image* icon_;
    brls::Label* nameLabel_;
    brls::Label* authorLabel_;
    HomebrewTitle title_;
    std::function<void(const HomebrewTitle&)> onClick_;
};

class ForwardersDataSource : public brls::RecyclerDataSource {
public:
    ForwardersDataSource(std::function<void(const HomebrewTitle&)> onClick);

    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

    void setTitles(const std::vector<HomebrewTitle>& titles, const std::string& query);
    void setGridView(bool grid, int columns);

private:
    std::function<void(const HomebrewTitle&)> onClick_;
    std::vector<HomebrewTitle> titles_;
    bool isGridView_ = true;
    int columns_ = 6;
};

class ForwardersView : public brls::Box {
public:
    ForwardersView(const std::string& currentPath = "sdmc:/switch/");

private:
    HomebrewService service_;
    std::string currentPath_;
    brls::RecyclerFrame* recycler_;
    ForwardersDataSource* dataSource_;
    std::shared_ptr<int> focusColumn_ = std::make_shared<int>(0);
};

} // namespace goonies::ui
