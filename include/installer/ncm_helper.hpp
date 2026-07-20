#pragma once
#include <switch.h>

namespace Installer {

class NcmHelper {
public:
    NcmHelper();
    ~NcmHelper();

    bool Initialize();
    void Finalize();

    // Registra un nuevo archivo NCA en el almacenamiento de la microSD
    bool CreateContent(const NcmContentId& content_id, size_t size);
    
    // Escribe datos a un NCA previamente creado
    bool WriteContent(const void* data, size_t size, size_t offset);
    
    // Cierra el archivo y lo da por instalado
    bool CommitContent();

private:
    bool m_initialized = false;
    NcmContentStorage m_storage;
    NcmContentMetaDatabase m_meta_db;
    
    FsFile m_current_file;
    bool m_file_open = false;
};

} // namespace Installer
