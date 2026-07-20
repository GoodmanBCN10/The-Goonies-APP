#include "yati/source/stream.hpp"
#include "defines.hpp"
#include "log.hpp"

namespace GooniesInstaller::yati::source {

Result Stream::Read(void* _buf, s64 off, s64 size, u64* bytes_read_out) {
    // streams don't allow for random access (seeking backwards).
    R_UNLESS(off >= m_offset, Result_StreamBadSeek);

    auto buf = static_cast<u8*>(_buf);
    *bytes_read_out = 0;

    // check if we already have some data in the buffer.
    while (size) {
        // while it is invalid to seek backwards, it is valid to seek forwards.
        // this can be done to skip padding, skip undeeded files etc.
        // to handle this, simply read the data into a buffer and discard it.
        if (off > m_offset) {
            const auto skip_size = off - m_offset;
            const auto chunk_size = std::min<s64>(skip_size, 0x100000); // 1MB chunks
            std::vector<u8> temp_buf(chunk_size);
            u64 bytes_read;
            R_TRY(ReadChunk(temp_buf.data(), chunk_size, &bytes_read));

            if (bytes_read == 0) {
                break; // EOF reached
            }

            m_offset += bytes_read;
        } else {
            u64 bytes_read;
            R_TRY(ReadChunk(buf, size, &bytes_read));

            if (bytes_read == 0) {
                break; // EOF reached
            }

            *bytes_read_out += bytes_read;
            buf += bytes_read;
            off += bytes_read;
            m_offset += bytes_read;
            size -= bytes_read;
        }
    }

    R_SUCCEED();
}

} // namespace GooniesInstaller::yati::source
