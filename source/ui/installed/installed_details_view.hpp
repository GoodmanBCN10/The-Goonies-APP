#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#include <borealis.hpp>
#include <switch.h>

#include "app/installed_title_service.hpp"
#include "app/game_metadata_service.hpp"
#include "ui/theme.hpp"
#include "ui/common/async_image.hpp"
#include "ui/saves/save_manager.hpp"

namespace pipensx::ui {

struct ContentItem {
    std::string type;
    uint64_t titleId;
    uint32_t version;
    uint64_t size;
    std::string storage;
};

inline std::string formatSize(uint64_t size) {
    if (size < 1024 * 1024) {
        float kb = (float)size / 1024.0f;
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << kb << " KB";
        return ss.str();
    } else if (size < 1024 * 1024 * 1024) {
        float mb = (float)size / (1024.0f * 1024.0f);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << mb << " MB";
        return ss.str();
    } else {
        float gb = (float)size / (1024.0f * 1024.0f * 1024.0f);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << gb << " GB";
        return ss.str();
    }
}

inline void fetchContentsFromDB(NcmContentMetaDatabase* db, uint64_t targetId, const std::string& typeName, const std::string& storageName, std::vector<ContentItem>& outList) {
    NcmContentMetaKey key;
    Result rcKey = ncmContentMetaDatabaseGetLatestContentMetaKey(db, &key, targetId);
    if (R_SUCCEEDED(rcKey)) {
        s32 count = 0;
        NcmContentInfo infos[20];
        Result rcList = ncmContentMetaDatabaseListContentInfo(db, &count, infos, 20, &key, 0);
        if (R_SUCCEEDED(rcList)) {
            uint64_t totalSize = 0;
            for (int i = 0; i < count; i++) {
                totalSize += (uint64_t)infos[i].size_low | ((uint64_t)infos[i].size_high << 32);
            }
            ContentItem item;
            item.type = typeName;
            item.titleId = targetId;
            item.version = key.version;
            item.size = totalSize;
            item.storage = storageName;
            outList.push_back(item);
        }
    }
}

class InstalledDetailsView : public brls::Box {
public:
    InstalledDetailsView(const InstalledTitle& title, GameMetadataService* metadata)
        : brls::Box(brls::Axis::COLUMN) {
        
        this->setAlignItems(brls::AlignItems::STRETCH);
        this->setPadding(40, 80, 40, 80);

        // Left Column (Icon + Info)
        brls::Box* leftColumn = new brls::Box(brls::Axis::COLUMN);
        leftColumn->setWidth(300);
        leftColumn->setPadding(40, 20, 40, 40);

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

        brls::Box* infoBox = new brls::Box(brls::Axis::COLUMN);
        infoBox->setMarginTop(20);

        brls::Label* titleLabel = new brls::Label();
        titleLabel->setText(title.name);
        titleLabel->setFontSize(28);
        titleLabel->setMarginBottom(10);
        titleLabel->setTextColor(theme::accent());
        infoBox->addView(titleLabel);

        brls::Label* titleIdLabel = new brls::Label();
        titleIdLabel->setText("TitleID: " + title.titleId);
        titleIdLabel->setFontSize(18);
        titleIdLabel->setMarginBottom(4);
        titleIdLabel->setTextColor(theme::textTertiary());
        infoBox->addView(titleIdLabel);

        std::string displayVer = "1.0.0";
        NsApplicationControlData* controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
        if (controlData) {
            uint64_t actualSize = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, title.applicationId, controlData, sizeof(NsApplicationControlData), &actualSize))) {
                displayVer = controlData->nacp.display_version;
            }
            free(controlData);
        }

        std::string reqSysVer = "0.0.0";
        std::vector<ContentItem> gameContents;
        NcmStorageId storages[] = {NcmStorageId_SdCard, NcmStorageId_BuiltInUser};
        std::string storageNames[] = {"MicroSD", "NAND"};

        for (int s = 0; s < 2; s++) {
            NcmContentMetaDatabase db;
            if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storages[s]))) {
                fetchContentsFromDB(&db, title.applicationId, "Base", storageNames[s], gameContents);
                
                NcmContentMetaKey key;
                if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, title.applicationId))) {
                    uint32_t reqV = 0;
                    if (R_SUCCEEDED(ncmContentMetaDatabaseGetRequiredSystemVersion(&db, &reqV, &key))) {
                        reqSysVer = std::to_string((reqV >> 26) & 0x3F) + "." + std::to_string((reqV >> 20) & 0x3F) + "." + std::to_string((reqV >> 16) & 0xF);
                    }
                }
                
                fetchContentsFromDB(&db, title.applicationId | 0x800, "Update", storageNames[s], gameContents);
                
                uint64_t dlcBase = (title.applicationId & 0xFFFFFFFFFFFFF000ULL) | 0x1000ULL;
                for (int i = 0; i < 256; i++) {
                    fetchContentsFromDB(&db, dlcBase + i, "DLC", storageNames[s], gameContents);
                }
                
                ncmContentMetaDatabaseClose(&db);
            }
        }

        brls::Label* verLabel = new brls::Label();
        verLabel->setText("Version: " + displayVer);
        verLabel->setFontSize(20);
        verLabel->setMarginTop(8);
        verLabel->setTextColor(theme::textSecondary());
        infoBox->addView(verLabel);

        brls::Label* reqLabel = new brls::Label();
        reqLabel->setText("Min FW: " + reqSysVer);
        reqLabel->setFontSize(20);
        reqLabel->setMarginTop(4);
        reqLabel->setTextColor(theme::textSecondary());
        infoBox->addView(reqLabel);

        leftColumn->addView(infoBox);

        // Right Column (Contents List)
        brls::Box* rightColumn = new brls::Box(brls::Axis::COLUMN);
        rightColumn->setGrow(1);
        rightColumn->setPadding(40, 40, 40, 20);

        brls::Label* contentTitle = new brls::Label();
        contentTitle->setText("Archivos Instalados");
        contentTitle->setFontSize(26);
        contentTitle->setMarginBottom(20);
        contentTitle->setTextColor(theme::accent());
        rightColumn->addView(contentTitle);

        brls::ScrollingFrame* list = new brls::ScrollingFrame();
        list->setGrow(1);
        brls::Box* listBox = new brls::Box(brls::Axis::COLUMN);
        uint64_t totalBytes = 0;

        for (const auto& item : gameContents) {
            brls::Box* listItem = new brls::Box(brls::Axis::ROW);
            listItem->setFocusable(true);
            listItem->setPadding(10, 20, 10, 20);
            listItem->setAlignItems(brls::AlignItems::CENTER);
            
            // Allow B to go back from the focused item too
            listItem->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
                brls::Application::popActivity();
                return true;
            });
            
            brls::Box* textCol = new brls::Box(brls::Axis::COLUMN);
            textCol->setGrow(1);
            
            brls::Label* typeLabel = new brls::Label();
            typeLabel->setText(item.type + "  [" + item.storage + "]");
            typeLabel->setFontSize(22);
            typeLabel->setMarginBottom(4);
            textCol->addView(typeLabel);
            
            std::string sub = "TitleID: " + InstalledTitleService::formatTitleId(item.titleId);
            if (item.version > 0) sub += "  |  v" + std::to_string(item.version);
            
            brls::Label* subLabel = new brls::Label();
            subLabel->setText(sub);
            subLabel->setFontSize(18);
            subLabel->setTextColor(theme::textTertiary());
            textCol->addView(subLabel);
            
            listItem->addView(textCol);
            
            brls::Label* sizeLabel = new brls::Label();
            sizeLabel->setText(formatSize(item.size));
            sizeLabel->setFontSize(20);
            sizeLabel->setTextColor(theme::textSecondary());
            listItem->addView(sizeLabel);
            
            listBox->addView(listItem);
            totalBytes += item.size;
        }

        if (gameContents.empty()) {
            brls::Label* emptyLabel = new brls::Label();
            emptyLabel->setText("No se encontraron contenidos instalados.");
            emptyLabel->setFontSize(20);
            emptyLabel->setTextColor(theme::textTertiary());
            emptyLabel->setMarginTop(20);
            emptyLabel->setMarginBottom(20);
            listBox->addView(emptyLabel);
        } else {
            brls::Box* totalItem = new brls::Box(brls::Axis::ROW);
            totalItem->setPadding(10, 20, 10, 20);
            totalItem->setAlignItems(brls::AlignItems::CENTER);
            
            brls::Label* totalText = new brls::Label();
            totalText->setText("Tamaño Total");
            totalText->setFontSize(22);
            totalText->setGrow(1);
            totalItem->addView(totalText);
            
            brls::Label* totalSizeLabel = new brls::Label();
            totalSizeLabel->setText(formatSize(totalBytes));
            totalSizeLabel->setFontSize(22);
            totalSizeLabel->setTextColor(theme::textPrimary());
            totalItem->addView(totalSizeLabel);
            
            listBox->addView(totalItem);
        }

        // Save Management Section inside listBox so D-Pad navigation reaches the buttons
        brls::Label* statusLabel = new brls::Label();
        statusLabel->setText("");
        statusLabel->setFontSize(16);
        statusLabel->setTextColor(theme::textSecondary());
        statusLabel->setMarginTop(15);
        statusLabel->setMarginBottom(10);
        listBox->addView(statusLabel);

        brls::Box* saveButtonsBox = new brls::Box(brls::Axis::ROW);
        saveButtonsBox->setFocusable(false);
        saveButtonsBox->setAlignItems(brls::AlignItems::CENTER);
        saveButtonsBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        saveButtonsBox->setMarginTop(5);
        saveButtonsBox->setMarginBottom(20);

        brls::Button* backupBtn = new brls::Button();
        backupBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        backupBtn->setText(t("Hacer Backup a SD", "Backup to SD"));
        backupBtn->setHeight(50);
        backupBtn->setGrow(1);
        backupBtn->setMarginRight(8);
        backupBtn->registerClickAction([title, statusLabel](brls::View*) {
            std::string msg;
            if (saves::SaveManager::BackupSave(title.applicationId, title.name, msg)) {
                statusLabel->setText(msg);
                statusLabel->setTextColor(theme::textPrimary());
            } else {
                statusLabel->setText(msg);
                statusLabel->setTextColor(nvgRGB(255, 100, 100));
            }
            return true;
        });

        brls::Button* restoreBtn = new brls::Button();
        restoreBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        restoreBtn->setText(t("Importar \"save\" desde SD", "Import save from SD"));
        restoreBtn->setHeight(50);
        restoreBtn->setGrow(1);
        restoreBtn->setMarginRight(8);
        restoreBtn->registerClickAction([title, statusLabel](brls::View*) {
            brls::Dialog* dialog = new brls::Dialog(t("¿Deseas sobreescribir la partida de la consola con la copia de la SD?", "Overwrite console save with SD copy?"));
            dialog->addButton(t("Si", "Yes"), [title, statusLabel, dialog]() {
                std::string msg;
                if (saves::SaveManager::RestoreSave(title.applicationId, title.name, msg)) {
                    statusLabel->setText(msg);
                    statusLabel->setTextColor(theme::textPrimary());
                } else {
                    statusLabel->setText(msg);
                    statusLabel->setTextColor(nvgRGB(255, 100, 100));
                }
                dialog->dismiss();
            });
            dialog->addButton(t("No", "No"), [dialog]() {
                dialog->dismiss();
            });
            dialog->open();
            return true;
        });

        brls::Button* deleteBtn = new brls::Button();
        deleteBtn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        deleteBtn->setText(t("Eliminar backup SD", "Delete SD backup"));
        deleteBtn->setHeight(50);
        deleteBtn->setGrow(1);
        deleteBtn->registerClickAction([title, statusLabel](brls::View*) {
            brls::Dialog* dialog = new brls::Dialog(t("¿Deseas eliminar la copia de seguridad de la SD?", "Delete SD backup?"));
            dialog->addButton(t("Si", "Yes"), [title, statusLabel, dialog]() {
                std::string msg;
                if (saves::SaveManager::DeleteBackup(title.applicationId, title.name, msg)) {
                    statusLabel->setText(msg);
                    statusLabel->setTextColor(theme::textPrimary());
                } else {
                    statusLabel->setText(msg);
                    statusLabel->setTextColor(nvgRGB(255, 100, 100));
                }
                dialog->dismiss();
            });
            dialog->addButton(t("No", "No"), [dialog]() {
                dialog->dismiss();
            });
            dialog->open();
            return true;
        });

        saveButtonsBox->addView(backupBtn);
        saveButtonsBox->addView(restoreBtn);
        saveButtonsBox->addView(deleteBtn);
        listBox->addView(saveButtonsBox);

        list->setContentView(listBox);

        brls::Box* listContainer = new brls::Box(brls::Axis::COLUMN);
        listContainer->setGrow(1);
        listContainer->addView(list);
        
        rightColumn->addView(listContainer);

        // Put columns into a body container
        brls::Box* bodyBox = new brls::Box(brls::Axis::ROW);
        bodyBox->setGrow(1);
        bodyBox->addView(leftColumn);
        bodyBox->addView(rightColumn);

        // This is now a COLUMN box, so we add body then BottomBar
        this->addView(bodyBox);
        this->addView(new brls::BottomBar());;

        this->registerAction("Volver", brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);
    }
};

} // namespace pipensx::ui
