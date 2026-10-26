#include "catalog_refresh.hpp"

namespace goonies {

CatalogRefreshAdoption adoptCatalogRefresh(
    CatalogService& catalog, GameMetadataService& metadata,
    CatalogRefreshBatch batch) {
    CatalogRefreshAdoption result;
    if (batch.catalogOk) {
        catalog.adopt(std::move(batch.catalogEntries));
        result.catalogChanged = true;
    }
    if (batch.metadataOk) {
        if (!batch.metadata.items.empty()) {
            metadata.adopt(std::move(batch.metadata));
            result.metadataChanged = true;
        }
        metadata.dropMemoryImageCache();
    }
    return result;
}

} // namespace goonies
