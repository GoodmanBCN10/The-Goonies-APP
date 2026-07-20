#include "forwarder_details_view.hpp"
#include "ui/theme.hpp"
#include "owo.hpp"

namespace pipensx::ui {

ForwarderDetailsView::ForwarderDetailsView(const HomebrewTitle& title)
    : brls::Box(brls::Axis::COLUMN), title_(title) {
    
    this->setAlignItems(brls::AlignItems::STRETCH);

    brls::Box* mainLayout = new brls::Box(brls::Axis::ROW);
    mainLayout->setGrow(1);
    
    // Left column: Icon
    brls::Box* leftColumn = new brls::Box(brls::Axis::COLUMN);
    leftColumn->setWidth(400);
    leftColumn->setAlignItems(brls::AlignItems::CENTER);
    leftColumn->setJustifyContent(brls::JustifyContent::CENTER);
    leftColumn->setPadding(40, 40, 40, 40);
    
    icon_ = new brls::Image();
    icon_->setWidth(256);
    icon_->setHeight(256);
    if (!title.icon.empty()) {
        icon_->setImageFromMem(title.icon.data(), title.icon.size());
    } else {
        icon_->setImageFromFile("romfs:/icon.jpg");
    }
    leftColumn->addView(icon_);
    
    // Right column: Details and buttons
    brls::Box* rightColumn = new brls::Box(brls::Axis::COLUMN);
    rightColumn->setGrow(1);
    rightColumn->setJustifyContent(brls::JustifyContent::CENTER);
    rightColumn->setPadding(40, 80, 40, 40);
    
    nameLabel_ = new brls::Label();
    nameLabel_->setText(title.name);
    nameLabel_->setFontSize(42);
    nameLabel_->setTextColor(theme::accent());
    rightColumn->addView(nameLabel_);
    
    authorLabel_ = new brls::Label();
    authorLabel_->setText("Autor: " + title.author);
    authorLabel_->setFontSize(24);
    authorLabel_->setMarginTop(10);
    authorLabel_->setMarginBottom(40);
    rightColumn->addView(authorLabel_);
    
    brls::Label* pathLabel = new brls::Label();
    pathLabel->setText("Ruta: " + title.path);
    pathLabel->setFontSize(18);
    pathLabel->setTextColor(nvgRGB(150, 150, 150));
    pathLabel->setMarginBottom(40);
    rightColumn->addView(pathLabel);
    
    launchButton_ = new brls::Button();
    launchButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    launchButton_->setText("Lanzar Aplicación");
    launchButton_->setMarginBottom(20);
    launchButton_->registerClickAction([this](brls::View* view) {
        this->Launch();
        return true;
    });
    rightColumn->addView(launchButton_);
    
    forwarderButton_ = new brls::Button();
    forwarderButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    forwarderButton_->setText("Crear Acceso Directo (Forwarder)");
    forwarderButton_->setMarginBottom(20);
    forwarderButton_->registerClickAction([this](brls::View* view) {
        this->CreateForwarder();
        return true;
    });
    rightColumn->addView(forwarderButton_);
    
    mainLayout->addView(leftColumn);
    mainLayout->addView(rightColumn);
    this->addView(mainLayout);
    this->addView(new brls::BottomBar());

    this->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::SOUND_BACK);
    
    UpdateButtons();
}

void ForwarderDetailsView::UpdateButtons() {
    // Both always active for now
}

void ForwarderDetailsView::Launch() {
    envSetNextLoad(title_.path.c_str(), title_.path.c_str());
    brls::Application::quit(); // Exit app so nextLoad triggers
}

void ForwarderDetailsView::CreateForwarder() {
    brls::Application::notify("Creando Forwarder... Espere por favor.");
    
    GooniesInstaller::OwoConfig owoCfg{};
    owoCfg.nro_path = title_.path;
    owoCfg.name = title_.name;
    owoCfg.author = title_.author;
    owoCfg.icon = title_.icon;
    owoCfg.nacp = title_.nacp;
    
    Result res = GooniesInstaller::install_forwarder(owoCfg, NcmStorageId_SdCard);
    
    if (R_SUCCEEDED(res)) {
        brls::Application::notify("Acceso directo creado con exito!");
    } else {
        brls::Application::notify("Error al crear el acceso directo.");
        brls::Logger::error("install_forwarder failed with: 0x%x", res);
    }
}

} // namespace pipensx::ui
