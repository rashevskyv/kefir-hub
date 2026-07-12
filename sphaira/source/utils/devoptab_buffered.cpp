#include "utils/devoptab_buffered.hpp"
#include "defines.hpp"
#include <cstring>
#include <algorithm>

namespace sphaira::devoptab::common {

// todo: change above function to handle bytes read instead.
Result BufferedData::Read(void *_buffer, s64 file_off, s64 read_size, u64* bytes_read) {
    auto dst = static_cast<u8*>(_buffer);
    size_t amount = 0;
    *bytes_read = 0;

    R_UNLESS(file_off < capacity, FsError_UnsupportedOperateRangeForFileStorage);
    read_size = std::min<s64>(read_size, capacity - file_off);

    if (m_size) {
        // check if we can read this data into the beginning of dst.
        if (file_off < m_off + m_size && file_off >= m_off) {
            const auto off = file_off - m_off;
            const auto size = std::min<s64>(read_size, m_size - off);
            if (size) {
                std::memcpy(dst, m_data.data() + off, size);

                read_size -= size;
                file_off += size;
                amount += size;
                dst += size;
            }
        }
    }

    if (read_size) {
        const auto alloc_size = std::min<s64>(m_data.size(), capacity - file_off);
        m_off = 0;
        m_size = 0;
        u64 read_bytes;

        // if the dst is big enough, read data in place.
        if (read_size > alloc_size) {
            R_TRY(source->Read(dst, file_off, read_size, &read_bytes));

            read_size -= read_bytes;
            file_off += read_bytes;
            amount += read_bytes;
            dst += read_bytes;

            // save the last chunk of data to the m_buffered io.
            const auto max_advance = std::min<u64>(amount, alloc_size);
            m_off = file_off - max_advance;
            m_size = max_advance;
            std::memcpy(m_data.data(), dst - max_advance, max_advance);
        } else {
            R_TRY(source->Read(m_data.data(), file_off, alloc_size, &read_bytes));
            const auto max_advance = std::min<u64>(read_size, read_bytes);
            std::memcpy(dst, m_data.data(), max_advance);

            m_off = file_off;
            m_size = read_bytes;

            read_size -= max_advance;
            file_off += max_advance;
            amount += max_advance;
            dst += max_advance;
        }
    }

    *bytes_read = amount;
    R_SUCCEED();
}

Result LruBufferedData::Read(void *_buffer, s64 file_off, s64 read_size, u64* bytes_read) {
    // log_write("[FATFS] read offset: %zu size: %zu\n", file_off, read_size);
    auto dst = static_cast<u8*>(_buffer);
    size_t amount = 0;
    *bytes_read = 0;

    R_UNLESS(file_off < capacity, FsError_UnsupportedOperateRangeForFileStorage);
    read_size = std::min<s64>(read_size, capacity - file_off);

    // fatfs reads in max 16k chunks.
    // knowing this, it's possible to detect large file reads by simply checking if
    // the read size is 16k (or more, maybe in the further).
    // however this would destroy random access performance, such as fetching 512 bytes.
    // the fix was to have 2 LRU caches, one for large data and the other for small (anything below 16k).
    // the results in file reads 32MB -> 184MB and directory listing is instant.
    const auto large_read = read_size >= 1024 * 16;
    auto& lru = large_read ? lru_cache[1] : lru_cache[0];

    for (auto list = lru.begin(); list; list = list->next) {
        const auto& m_buffered = list->data;
        if (m_buffered->size) {
            // check if we can read this data into the beginning of dst.
            if (file_off < m_buffered->off + m_buffered->size && file_off >= m_buffered->off) {
                const auto off = file_off - m_buffered->off;
                const auto size = std::min<s64>(read_size, m_buffered->size - off);
                if (size) {
                    // log_write("[FAT] cache HIT at: %zu\n", file_off);
                    std::memcpy(dst, m_buffered->data + off, size);

                    read_size -= size;
                    file_off += size;
                    amount += size;
                    dst += size;

                    lru.Update(list);
                    break;
                }
            }
        }
    }

    if (read_size) {
        // log_write("[FAT] cache miss at: %zu %zu\n", file_off, read_size);

        auto alloc_size = large_read ? CACHE_LARGE_ALLOC_SIZE : std::max<u64>(read_size, 512 * 24);
        alloc_size = std::min<s64>(alloc_size, capacity - file_off);
        u64 read_bytes;

        auto m_buffered = lru.GetNextFree();
        m_buffered->Allocate(alloc_size);

        // if the dst is big enough, read data in place.
        if (read_size > alloc_size) {
            R_TRY(source->Read(dst, file_off, read_size, &read_bytes));
            // R_TRY(fsStorageRead(storage, file_off, dst, read_size));
            read_size -= read_bytes;
            file_off += read_bytes;
            amount += read_bytes;
            dst += read_bytes;

            // save the last chunk of data to the m_buffered io.
            const auto max_advance = std::min<u64>(amount, alloc_size);
            m_buffered->off = file_off - max_advance;
            m_buffered->size = max_advance;
            std::memcpy(m_buffered->data, dst - max_advance, max_advance);
        } else {
            R_TRY(source->Read(m_buffered->data, file_off, alloc_size, &read_bytes));
            // R_TRY(fsStorageRead(storage, file_off, m_buffered->data, alloc_size));
            const auto max_advance = std::min<u64>(read_size, read_bytes);
            std::memcpy(dst, m_buffered->data, max_advance);

            m_buffered->off = file_off;
            m_buffered->size = read_bytes;

            read_size -= max_advance;
            file_off += max_advance;
            amount += max_advance;
            dst += max_advance;
        }
    }

    *bytes_read = amount;
    R_SUCCEED();
}

} // namespace sphaira::devoptab::common
