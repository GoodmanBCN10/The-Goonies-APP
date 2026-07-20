#include "installer/pfs0.hpp"
#include <cstring>
#include <stdio.h>

namespace Installer {

struct Pfs0Header {
    u32 magic;
    u32 num_files;
    u32 string_table_size;
    u32 reserved;
};

bool Pfs0Parser::ParseHeader(const u8* data, size_t size) {
    if (m_header_parsed) return true;

    if (size < sizeof(Pfs0Header)) {
        return false; // Need more data
    }

    const Pfs0Header* header = reinterpret_cast<const Pfs0Header*>(data);
    
    // Check for little-endian "PFS0" magic: P=0x50, F=0x46, S=0x53, 0=0x30
    // So 0x30534650
    if (header->magic != 0x30534650) { // 'PFS0'
        return false;
    }

    size_t expected_size = sizeof(Pfs0Header) + header->num_files * sizeof(Pfs0FileEntry) + header->string_table_size;
    
    if (size < expected_size) {
        return false; // Need more data
    }

    const Pfs0FileEntry* entries = reinterpret_cast<const Pfs0FileEntry*>(data + sizeof(Pfs0Header));
    const char* string_table = reinterpret_cast<const char*>(data + sizeof(Pfs0Header) + header->num_files * sizeof(Pfs0FileEntry));

    m_files.clear();
    for (u32 i = 0; i < header->num_files; ++i) {
        if (entries[i].string_table_offset >= header->string_table_size) {
            return false;
        }
        
        const char* str_start = string_table + entries[i].string_table_offset;
        size_t max_len = header->string_table_size - entries[i].string_table_offset;
        
        // Find null terminator manually to prevent reading past string_table_size
        size_t actual_len = 0;
        while (actual_len < max_len && str_start[actual_len] != '\0') {
            actual_len++;
        }
        
        Pfs0File file;
        file.name = std::string(str_start, actual_len);
        file.offset = entries[i].offset;
        file.size = entries[i].size;
        m_files.push_back(file);
    }

    m_header_size = expected_size;
    m_header_parsed = true;
    return true;
}

} // namespace Installer
