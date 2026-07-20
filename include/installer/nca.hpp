#pragma once
#include <switch.h>
#include <string>
#include <vector>

namespace Installer {

struct NcaHeader {
    u8 rsa_signature[0x200];
    u32 magic; // "NCA3" or "NCA2"
    u8 distribution_type;
    u8 content_type;
    u8 key_generation_old;
    u8 key_area_encryption_key_index;
    u64 content_size;
    u64 program_id;
    u32 content_index;
    u32 sdk_addon_version;
    u8 key_generation;
    u8 header_1_signature_key_generation;
    u8 reserved[0xE];
    u8 rights_id[0x10];
    // other fields follow, but this is the critical part
};

class NcaParser {
public:
    NcaParser() = default;
    ~NcaParser() = default;

    // Descifra y parsea la cabecera de 0x400 bytes iniciales de un NCA
    bool ParseHeader(const u8* encrypted_data, size_t size);
    
    u32 GetMagic() const { return m_header.magic; }
    u8 GetContentType() const { return m_header.content_type; }
    u64 GetProgramId() const { return m_header.program_id; }
    bool HasRightsId() const;
    std::vector<u8> GetRightsId() const;

private:
    bool m_header_parsed = false;
    NcaHeader m_header;

    bool DecryptHeader(const u8* encrypted_data, u8* decrypted_data);
};

} // namespace Installer
