#include "installer/cnmt.hpp"
#include <cstring>
#include <stdio.h>

namespace Installer {

bool CnmtParser::Parse(const u8* data, size_t size) {
    if (size < sizeof(CnmtHeader)) {
        printf("CnmtParser: Data too small for header\n");
        return false;
    }

    memcpy(&m_header, data, sizeof(CnmtHeader));
    m_contents.clear();

    size_t expected_size = sizeof(CnmtHeader) + m_header.table_offset + m_header.content_count * sizeof(CnmtContentEntry);
    if (size < expected_size) {
        printf("CnmtParser: Data too small for content entries\n");
        return false;
    }

    const u8* content_ptr = data + sizeof(CnmtHeader) + m_header.table_offset;
    for (u16 i = 0; i < m_header.content_count; ++i) {
        CnmtContentEntry entry;
        memcpy(&entry, content_ptr + i * sizeof(CnmtContentEntry), sizeof(CnmtContentEntry));
        m_contents.push_back(entry);
    }

    m_parsed = true;
    return true;
}

} // namespace Installer
