#include "save_details_view.hpp"
#include "save_manager.hpp"
#include "ui/theme.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/common/async_image.hpp"

namespace pipensx::ui {

SaveDetailsView::SaveDetailsView(const InstalledTitle& title, GameMetadataService* metadata)
    : brls::Box(brls::Axis::COLUMN), title_(title), metadata_(metadata) {
    
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setPadding(40, 80, 40, 80);

    // Header (Title)
    brls::Label* header = new brls::Label();
    header->setText("Gestor de Partidas Guardadas");
    header->setFontSize(28);
    header->setTextColor(theme::accent());
    header->setMarginBottom(30);
    this->addView(header);

    // Main layout
    brls::Box* mainLayout = new brls::Box(brls::Axis::ROW);
    mainLayout->setGrow(1);
    
    // Left Column (Icon + Info)
    brls::Box* leftColumn = new brls::Box(brls::Axis::COLUMN);
    leftColumn->setWidth(300);
    leftColumn->setPadding(0, 40, 0, 0);

    brls::Box* iconBox = new brls::Box(brls::Axis::COLUMN);
    iconBox->setWidth(256);
    iconBox->setHeight(256);
    AsyncRgbaImage* icon = new AsyncRgbaImage();
    icon->setWidth(256);
    icon->setHeight(256);
    icon->setCornerRadius(16);
    icon->setScalingType(brls::ImageScalingType::FILL);
    std::string currentIconPath;
    std::shared_ptr<ImageRequestState> imageState = std::make_shared<ImageRequestState>();
    setArtworkUrl(icon, metadata, title.iconPath, currentIconPath, imageState);
    iconBox->addView(icon);
    leftColumn->addView(iconBox);

    brls::Label* titleLabel = new brls::Label();
    titleLabel->setText(title.name);
    titleLabel->setFontSize(22);
    titleLabel->setMarginTop(20);
    titleLabel->setSingleLine(false);
    leftColumn->addView(titleLabel);

    mainLayout->addView(leftColumn);

    // Right Column (Buttons)
    brls::Box* rightColumn = new brls::Box(brls::Axis::COLUMN);
    rightColumn->setGrow(1);
    rightColumn->setAlignItems(brls::AlignItems::STRETCH);

    statusLabel_ = new brls::Label();
    statusLabel_->setText("");
    statusLabel_->setFontSize(18);
    statusLabel_->setTextColor(theme::textSecondary());
    statusLabel_->setMarginBottom(20);
    rightColumn->addView(statusLabel_);

    backupButton_ = new brls::Button();
    backupButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    backupButton_->setText("Hacer Backup a SD");
    backupButton_->setMarginBottom(15);
    backupButton_->setHeight(60);
    backupButton_->registerClickAction([this](brls::View* view) {
        std::string message;
        if (saves::SaveManager::BackupSave(title_.applicationId, title_.name, message)) {
            statusLabel_->setText(message);
            statusLabel_->setTextColor(brls::Application::getTheme().getColor("brls/text"));
        } else {
            statusLabel_->setText(message);
            statusLabel_->setTextColor(nvgRGB(255, 100, 100)); // Red for error
        }
        UpdateButtons();
        return true;
    });
    rightColumn->addView(backupButton_);

    restoreButton_ = new brls::Button();
    restoreButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    restoreButton_->setText("Restaurar desde SD");
    restoreButton_->setMarginBottom(15);
    restoreButton_->setHeight(60);
    restoreButton_->registerClickAction([this](brls::View* view) {
        // Confirmation dialog
        brls::Dialog* dialog = new brls::Dialog("¿Estas seguro de que quieres sobreescribir la partida actual de la consola con la copia de la SD?");
        dialog->addButton("Si", [this, dialog]() {
            std::string message;
            if (saves::SaveManager::RestoreSave(title_.applicationId, title_.name, message)) {
                statusLabel_->setText(message);
                statusLabel_->setTextColor(brls::Application::getTheme().getColor("brls/text"));
            } else {
                statusLabel_->setText(message);
                statusLabel_->setTextColor(nvgRGB(255, 100, 100));
            }
            dialog->dismiss();
        });
        dialog->addButton("No", [dialog]() {
            dialog->dismiss();
        });
        dialog->open();
        return true;
    });
    rightColumn->addView(restoreButton_);

    deleteButton_ = new brls::Button();
    deleteButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    deleteButton_->setText("Eliminar Backup de SD");
    deleteButton_->setMarginBottom(15);
    deleteButton_->setHeight(60);
    deleteButton_->registerClickAction([this](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("¿Estas seguro de que quieres eliminar la copia de seguridad de la SD?");
        dialog->addButton("Si", [this, dialog]() {
            std::string message;
            if (saves::SaveManager::DeleteBackup(title_.applicationId, title_.name, message)) {
                statusLabel_->setText(message);
                statusLabel_->setTextColor(brls::Application::getTheme().getColor("brls/text"));
            } else {
                statusLabel_->setText(message);
                statusLabel_->setTextColor(nvgRGB(255, 100, 100));
            }
            UpdateButtons();
            dialog->dismiss();
        });
        dialog->addButton("No", [dialog]() {
            dialog->dismiss();
        });
        dialog->open();
        return true;
    });
    rightColumn->addView(deleteButton_);

    mainLayout->addView(rightColumn);
    this->addView(mainLayout);
    this->addView(new brls::BottomBar());

    this->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::SOUND_BACK);

    UpdateButtons();
}

void SaveDetailsView::UpdateButtons() {
    bool hasBackup = saves::SaveManager::HasBackup(title_.applicationId, title_.name);
    
    if (hasBackup) {
        restoreButton_->setState(brls::ButtonState::ENABLED);
        deleteButton_->setState(brls::ButtonState::ENABLED);
        restoreButton_->setTextColor(brls::Application::getTheme().getColor("brls/text"));
        deleteButton_->setTextColor(brls::Application::getTheme().getColor("brls/text"));
    } else {
        restoreButton_->setState(brls::ButtonState::DISABLED);
        deleteButton_->setState(brls::ButtonState::DISABLED);
        restoreButton_->setTextColor(theme::textDisabled());
        deleteButton_->setTextColor(theme::textDisabled());
    }
}

} // namespace pipensx::ui
