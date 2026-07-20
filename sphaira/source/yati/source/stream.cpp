#include "yati/source/stream.hpp"
#include "defines.hpp"
#include "log.hpp"
#include <algorithm>

namespace sphaira::yati::source {

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
            const auto skip_size = std::min<s64>(off - m_offset, 65536);
            std::vector<u8> temp_buf(skip_size);
            u64 bytes_read;
            R_TRY(ReadChunk(temp_buf.data(), temp_buf.size(), &bytes_read));
            if (bytes_read == 0) {
                break;
            }

            m_offset += bytes_read;
        } else {
            u64 bytes_read;
            R_TRY(ReadChunk(buf, size, &bytes_read));
            if (bytes_read == 0) {
                break;
            }

            *bytes_read_out += bytes_read;
            buf += bytes_read;
            off += bytes_read;
            m_offset += bytes_read;
            size -= bytes_read;
            break; // Return early with whatever chunk size we got to avoid blocking the pipeline
        }
    }

    R_SUCCEED();
}

} // namespace sphaira::yati::source
