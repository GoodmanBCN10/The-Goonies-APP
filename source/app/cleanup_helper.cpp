#include "app/cleanup_helper.hpp"
#include "log.hpp"
#include <set>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
extern "C" {
#include <ipcext/es.h>
}
#endif

namespace pipensx {

bool CleanupHelper::cleanSystem(std::string& errorOut, u64& outFreedBytes, int& outDeletedTickets) {
    outFreedBytes = 0;
    outDeletedTickets = 0;
#ifdef __SWITCH__
    auto cleanStorage = [&outFreedBytes](NcmStorageId storage_id) {
        NcmContentStorage cs;
        if (R_SUCCEEDED(ncmOpenContentStorage(&cs, storage_id))) {
            s64 initialFree = 0;
            s64 finalFree = 0;
            ncmContentStorageGetFreeSpaceSize(&cs, &initialFree);
            
            // 1. Clean Placeholders
            ncmContentStorageCleanupAllPlaceHolder(&cs);
            
            // 2. Clean Orphaned Contents (NCAs)
            NcmContentMetaDatabase db;
            if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storage_id))) {
                s32 contentCount = 0;
                if (R_SUCCEEDED(ncmContentStorageGetContentCount(&cs, &contentCount)) && contentCount > 0) {
                    std::vector<NcmContentId> contentIds(contentCount);
                    s32 outCount = 0;
                    if (R_SUCCEEDED(ncmContentStorageListContentId(&cs, contentIds.data(), contentCount, &outCount, 0)) && outCount > 0) {
                        bool* orphaned = new bool[outCount];
                        if (R_SUCCEEDED(ncmContentMetaDatabaseLookupOrphanContent(&db, orphaned, contentIds.data(), outCount))) {
                            for (s32 i = 0; i < outCount; i++) {
                                if (orphaned[i]) {
                                    ncmContentStorageDelete(&cs, &contentIds[i]);
                                }
                            }
                        }
                        delete[] orphaned;
                    }
                }
                ncmContentMetaDatabaseClose(&db);
            }
            
            ncmContentStorageGetFreeSpaceSize(&cs, &finalFree);
            if (finalFree > initialFree) {
                outFreedBytes += (finalFree - initialFree);
            }
            ncmContentStorageClose(&cs);
        }
    };

    cleanStorage(NcmStorageId_SdCard);
    cleanStorage(NcmStorageId_BuiltInUser);

    // 2. Clean Orphan Tickets
    std::set<u64> installedBaseTids;
    
    // Get all installed applications
    s32 offset = 0;
    const s32 batchSize = 100;
    std::vector<NsApplicationRecord> records(batchSize);
    
    while (true) {
        s32 recordCount = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), batchSize, offset, &recordCount))) {
            break;
        }
        if (recordCount == 0) {
            break;
        }
        
        for (s32 i = 0; i < recordCount; i++) {
            installedBaseTids.insert(records[i].application_id);
        }
        offset += recordCount;
    }
    
    // Check and delete tickets
    auto checkAndDeleteTickets = [&installedBaseTids, &outDeletedTickets](bool personalized) {
        u32 count = personalized ? esCountPersonalizedTicket() : esCountCommonTicket();
        if (count == 0) return;
        
        std::vector<EsRightsId> rightsIds(count);
        u32 written = 0;
        Result rc = personalized ? 
            esListPersonalizedTicket(&written, rightsIds.data(), count * sizeof(EsRightsId)) :
            esListCommonTicket(&written, rightsIds.data(), count * sizeof(EsRightsId));
            
        if (R_FAILED(rc)) return;
        
        for (u32 i = 0; i < written; i++) {
            u64 tid = esGetRightsIdApplicationId(&rightsIds[i]);
            u64 baseTid = tid & ~0x1FFF; // Mask to get the base title ID
            
            if (installedBaseTids.find(baseTid) == installedBaseTids.end()) {
                // Ticket does not belong to any installed application, it's an orphan
                if (R_SUCCEEDED(esDeleteTicket(&rightsIds[i]))) {
                    outDeletedTickets++;
                }
            }
        }
    };
    
    checkAndDeleteTickets(false); // Common Tickets
    checkAndDeleteTickets(true);  // Personalized Tickets

#endif
    return true;
}

} // namespace pipensx
