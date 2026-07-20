#include "installer/nca.hpp"
#include "installer/keys.hpp"
#include <mbedtls/aes.h>
#include <cstring>
#include <stdio.h>

namespace Installer {

bool NcaParser::DecryptHeader(const u8* encrypted_data, u8* decrypted_data) {
    if (!KeysManager::Get().HasKey("header_key")) {
        printf("NcaParser: Missing 'header_key' from prod.keys\n");
        return false;
    }

    std::vector<u8> header_key = KeysManager::Get().GetKey("header_key");
    if (header_key.size() != 32) {
        printf("NcaParser: Invalid 'header_key' size (must be 32 bytes)\n");
        return false;
    }

    // El NCA header está encriptado con AES-XTS usando la header_key.
    // La clave es de 32 bytes (256 bits), lo cual significa 128 bits para AES1 y 128 bits para AES2 en XTS.
    mbedtls_aes_xts_context ctx;
    mbedtls_aes_xts_init(&ctx);

    // mbedtls espera el tamaño total de la clave en bits (256 bits = 32 bytes)
    int ret = mbedtls_aes_xts_setkey_dec(&ctx, header_key.data(), 256);
    if (ret != 0) {
        printf("NcaParser: mbedtls_aes_xts_setkey_dec failed: %d\n", ret);
        mbedtls_aes_xts_free(&ctx);
        return false;
    }

    // El sector size en NCA XTS típicamente es 0x200 bytes.
    // Necesitamos desencriptar los primeros 0xC00 bytes.
    u8 tweak[16] = {0}; // El tweak inicial (Sector 0) suele ser todo ceros
    
    // Desencriptar por sectores (0x200 bytes)
    for (size_t offset = 0; offset < 0xC00; offset += 0x200) {
        // Tweak es el número de sector en Big Endian
        memset(tweak, 0, 16);
        u64 sector = offset / 0x200;
        // Convert to Big Endian for tweak
        for (int i = 0; i < 8; ++i) {
            tweak[15 - i] = (sector >> (8 * i)) & 0xFF;
        }

        ret = mbedtls_aes_crypt_xts(&ctx, MBEDTLS_AES_DECRYPT, 0x200, tweak, encrypted_data + offset, decrypted_data + offset);
        if (ret != 0) {
            printf("NcaParser: mbedtls_aes_crypt_xts failed at offset 0x%zx: %d\n", offset, ret);
            mbedtls_aes_xts_free(&ctx);
            return false;
        }
    }

    mbedtls_aes_xts_free(&ctx);
    return true;
}

bool NcaParser::ParseHeader(const u8* encrypted_data, size_t size) {
    if (size < 0xC00) {
        return false; // Necesitamos al menos 0xC00 bytes para leer la cabecera completa.
    }

    u8 decrypted_data[0xC00];
    if (!DecryptHeader(encrypted_data, decrypted_data)) {
        return false;
    }

    memcpy(&m_header, decrypted_data, sizeof(NcaHeader));

    // Magic checks ("NCA3" = 0x3341434E, "NCA2" = 0x3241434E)
    if (m_header.magic != 0x3341434E && m_header.magic != 0x3241434E) {
        printf("NcaParser: Invalid NCA magic after decryption: %08X\n", m_header.magic);
        return false;
    }

    m_header_parsed = true;
    return true;
}

bool NcaParser::HasRightsId() const {
    if (!m_header_parsed) return false;
    for (int i = 0; i < 0x10; ++i) {
        if (m_header.rights_id[i] != 0) return true;
    }
    return false;
}

std::vector<u8> NcaParser::GetRightsId() const {
    std::vector<u8> rights;
    if (HasRightsId()) {
        rights.assign(m_header.rights_id, m_header.rights_id + 0x10);
    }
    return rights;
}

} // namespace Installer
