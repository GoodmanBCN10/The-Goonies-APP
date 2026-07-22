#include "saves_view.hpp"
#include "save_details_view.hpp"
#include "ui/theme.hpp"

namespace pipensx::ui {

SavesView::SavesView(InstalledTitleService* installedService, GameMetadataService* metadata)
    : brls::Box(brls::Axis::COLUMN), installedService_(installedService), metadata_(metadata) {
    
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* header = new brls::Box(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setPadding(40, 80, 20, 80);
    
    brls::Label* title = new brls::Label();
    title->setText(t("Partidas Guardadas", "Save Data"));
    title->setFontSize(28);
    title->setTextColor(theme::accent());
    header->addView(title);
    
    this->addView(header);
    
    recycler_ = new brls::RecyclerFrame();
    recycler_->setGrow(1);
    recycler_->setPadding(0, 80, 40, 80);
    recycler_->estimatedRowHeight = 250;
    
    auto onClick = [this, metadata](const InstalledTitle& title) {
        brls::Application::pushActivity(new brls::Activity(new SaveDetailsView(title, metadata)));
    };
    
    dataSource_ = new InstalledDataSource(metadata, onClick);
    dataSource_->setGridView(true, 6);
    recycler_->setDataSource(dataSource_);
    
    recycler_->registerCell("GridRow", []() { return new InstalledGridRow(6); });
    recycler_->registerCell("Installed", []() { return new InstalledCell(); });
    recycler_->registerCell("Message", []() { return new TextMessageCell(); });
    
    this->addView(recycler_);
    this->addView(new brls::BottomBar());
    
    auto titles = installedService_->titles();
    dataSource_->setTitles(titles, "");
    recycler_->reloadData();
    
    this->registerAction(t("Volver", "Back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::SOUND_BACK);
    
    // Vista toggle button was removed

    this->registerAction(t("Buscar", "Search"), brls::BUTTON_Y, [this](brls::View*) {
        brls::Application::getImeManager()->openForText([this](std::string text) {
            auto titles = installedService_->titles();
            dataSource_->setTitles(titles, text);
            recycler_->reloadData();
            brls::Application::giveFocus(this);
        }, t("Buscar partidas", "Search save data"), t("Introduce el nombre del juego", "Enter game name"), 100, "", 0);
        return true;
    });
}

} // namespace pipensx::ui
