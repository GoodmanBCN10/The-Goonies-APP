#pragma once
#include <switch.h>
#include <vector>
#include <string>

namespace Installer {

struct Pfs0FileEntry {
    u64 offset;
    u64 size;
    u32 string_table_offset;
    u32 reserved;
};

struct Pfs0File {
    std::string name;
    u64 offset;
    u64 size;
};

class Pfs0Parser {
public:
    Pfs0Parser() = default;
    ~Pfs0Parser() = default;

    bool ParseHeader(const u8* data, size_t size);
    bool IsHeaderParsed() const { return m_header_parsed; }
    
    size_t GetHeaderSize() const { return m_header_size; }
    const std::vector<Pfs0File>& GetFiles() const { return m_files; }

private:
    bool m_header_parsed = false;
    size_t m_header_size = 0;
    std::vector<Pfs0File> m_files;
};

} // namespace Installer
