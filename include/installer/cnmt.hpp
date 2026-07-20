#pragma once
#include <switch.h>
#include <vector>

namespace Installer {

enum class ContentType : u8 {
    Meta = 0,
    Program = 1,
    Data = 2,
    Control = 3,
    HtmlDocument = 4,
    LegalInformation = 5,
    DeltaFragment = 6
};

enum class MetaType : u8 {
    SystemProgram = 1,
    SystemData = 2,
    SystemUpdate = 3,
    BootImagePackage = 4,
    BootImagePackageSafe = 5,
    Application = 0x80,
    Patch = 0x81,
    AddOnContent = 0x82,
    Delta = 0x83
};

struct CnmtHeader {
    u64 title_id;
    u32 title_version;
    MetaType type;
    u8 reserved;
    u16 table_offset;
    u16 content_count;
    u16 meta_count;
    u8 attributes;
    u8 reserved2[11];
};

struct CnmtContentEntry {
    u8 hash[0x20];
    u8 nca_id[0x10];
    u8 size[6];
    ContentType type;
    u8 id_offset;
};

class CnmtParser {
public:
    CnmtParser() = default;
    ~CnmtParser() = default;

    bool Parse(const u8* data, size_t size);
    
    u64 GetTitleId() const { return m_header.title_id; }
    u32 GetTitleVersion() const { return m_header.title_version; }
    MetaType GetType() const { return m_header.type; }
    const std::vector<CnmtContentEntry>& GetContents() const { return m_contents; }

private:
    bool m_parsed = false;
    CnmtHeader m_header;
    std::vector<CnmtContentEntry> m_contents;
};

} // namespace Installer
