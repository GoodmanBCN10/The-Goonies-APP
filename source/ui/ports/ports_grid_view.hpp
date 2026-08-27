#pragma once

#include <vector>
#include <algorithm>
#include <borealis.hpp>
#include "app/catalog_service.hpp"
#include "ui/ports/port_cell.hpp"
#include "app/simple_download_manager.hpp"

namespace goonies::ui {

class PortsGridView : public brls::Box {
public:
    PortsGridView(pipensx::CatalogService* catalog, goonies::SimpleDownloadManager* manager, pipensx::GameMetadataService* metadata, const std::string& categoryFilter) 
        : brls::Box(brls::Axis::COLUMN) 
    {
        this->setGrow(1.0f);
        this->setAlignItems(brls::AlignItems::STRETCH);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        this->addView(scroll);

        auto* container = new brls::Box(brls::Axis::COLUMN);
        container->setPadding(40, 80, 40, 80);
        scroll->setContentView(container);

        auto* title = new brls::Label();
        title->setText(categoryFilter.empty() ? "Todos" : categoryFilter);
        title->setFontSize(32);
        title->setMarginBottom(40);
        container->addView(title);

        const int columns = 3;
        brls::Box* currentRow = nullptr;
        int currentCount = 0;
        
        auto entries_list = catalog->entries();
        if (categoryFilter == "Novedades") {
            if (entries_list.size() > 6) {
                // Keep only the last 6 entries
                entries_list.erase(entries_list.begin(), entries_list.end() - 6);
            }
            std::reverse(entries_list.begin(), entries_list.end());
        }

        for (const auto& entry : entries_list) {
            if (categoryFilter != "Novedades" && !categoryFilter.empty() && entry.category != categoryFilter) {
                continue;
            }

            if (currentCount == 0) {
                currentRow = new brls::Box(brls::Axis::ROW);
                currentRow->setMarginBottom(20);
                currentRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
                container->addView(currentRow);
            }

            auto* cell = new PortCell(entry, manager, metadata);
            cell->setGrow(1.0f);
            cell->setWidth(250);
            cell->setHeight(250);
            cell->setMarginRight(20);
            currentRow->addView(cell);
            
            currentCount++;
            if (currentCount >= columns) {
                currentCount = 0;
            }
        }
        
        // Pad the last row if needed so they align to the left instead of stretching weirdly
        if (currentCount > 0 && currentRow) {
            while (currentCount < columns) {
                auto* spacer = new brls::Box();
                spacer->setGrow(1.0f);
                spacer->setWidth(250);
                spacer->setMarginRight(20);
                currentRow->addView(spacer);
                currentCount++;
            }
        }
    }
};

} // namespace goonies::ui
