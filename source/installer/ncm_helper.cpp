#include "installer/ncm_helper.hpp"
#include <stdio.h>

namespace Installer {

NcmHelper::NcmHelper() {
}

NcmHelper::~NcmHelper() {
    Finalize();
}

bool NcmHelper::Initialize() {
    if (m_initialized) return true;

    // Abrimos el almacenamiento y base de datos para la SD Card
    Result rc = ncmOpenContentStorage(&m_storage, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        printf("NcmHelper: ncmOpenContentStorage failed: %08x\n", rc);
        return false;
    }

    rc = ncmOpenContentMetaDatabase(&m_meta_db, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        printf("NcmHelper: ncmOpenContentMetaDatabase failed: %08x\n", rc);
        ncmContentStorageClose(&m_storage);
        return false;
    }

    m_initialized = true;
    return true;
}

void NcmHelper::Finalize() {
    if (m_file_open) {
        fsFileClose(&m_current_file);
        m_file_open = false;
    }
    
    if (m_initialized) {
        ncmContentMetaDatabaseClose(&m_meta_db);
        ncmContentStorageClose(&m_storage);
        m_initialized = false;
    }
}

bool NcmHelper::CreateContent(const NcmContentId& content_id, size_t size) {
    if (!m_initialized) return false;

    // Primero obtenemos la ruta temporal para crear el contenido
    char path[FS_MAX_PATH];
    Result rc = ncmContentStorageGeneratePlaceHolderId(&m_storage, (NcmPlaceHolderId*)&content_id);
    if (R_FAILED(rc)) {
        printf("NcmHelper: GeneratePlaceHolderId failed: %08x\n", rc);
        return false;
    }

    // NCM v10.0.0+ requires creating content placeholder and writing directly or via ContentStorage methods.
    // For simplicity in this base structure from scratch, we simulate standard file writing through ncm.
    
    // En una implementación real, esto volcaría los datos iniciales al placeholder.
    // rc = ncmContentStorageWritePlaceHolder(&m_storage, (NcmPlaceHolderId*)&content_id, 0, nullptr, size);

    return R_SUCCEEDED(rc);
}

bool NcmHelper::WriteContent(const void* data, size_t size, size_t offset) {
    if (!m_initialized) return false;

    // Aquí iría el volcado del buffer data a la microSD mediante fsFileWrite o equivalente de NCM.
    // Como el instalador se construye desde cero, este sería el puente MTP -> SD.
    return true;
}

bool NcmHelper::CommitContent() {
    if (!m_initialized) return false;
    
    // Registro del meta y commit final
    return true;
}

} // namespace Installer
