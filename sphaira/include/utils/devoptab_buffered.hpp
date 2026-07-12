#pragma once

#include "yati/source/file.hpp"
#include "utils/lru.hpp"
#include <memory>
#include <vector>

namespace sphaira::devoptab::common {

struct BufferedDataBase : yati::source::Base {
    BufferedDataBase(const std::shared_ptr<yati::source::Base>& _source, u64 _size)
    : source{_source}
    , capacity{_size} {

    }

    virtual Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        return source->Read(buf, off, size, bytes_read);
    }

protected:
    std::shared_ptr<yati::source::Base> source;
    const u64 capacity;
};

// buffers data in 512k chunks to maximise throughput.
// not suitable if random access >= 512k is common.
// if that is needed, see the LRU cache varient used for fatfs.
struct BufferedData : BufferedDataBase {
    BufferedData(const std::shared_ptr<yati::source::Base>& _source, u64 _size, u64 _alloc = 1024 * 512)
    : BufferedDataBase{_source, _size} {
        m_data.resize(_alloc);
    }

    virtual Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;

private:
    u64 m_off{};
    u64 m_size{};
    std::vector<u8> m_data{};
};

struct BufferedFileData {
    u8* data{};
    u64 off{};
    u64 size{};

    ~BufferedFileData() {
        if (data) {
            free(data);
        }
    }

    void Allocate(u64 new_size) {
        data = (u8*)realloc(data, new_size * sizeof(*data));
        off = 0;
        size = 0;
    }
};

constexpr u64 CACHE_LARGE_ALLOC_SIZE = 1024 * 512;
constexpr u64 CACHE_LARGE_SIZE = 1024 * 16;

struct LruBufferedData : BufferedDataBase {
    LruBufferedData(const std::shared_ptr<yati::source::Base>& _source, u64 _size, u32 small = 1024, u32 large = 2)
    : BufferedDataBase{_source, _size} {
        buffered_small.resize(small);
        buffered_large.resize(large);
        lru_cache[0].Init(buffered_small);
        lru_cache[1].Init(buffered_large);
    }

    virtual Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;

private:
    utils::Lru<BufferedFileData> lru_cache[2]{};
    std::vector<BufferedFileData> buffered_small{}; // 1MiB (usually).
    std::vector<BufferedFileData> buffered_large{}; // 1MiB
};

} // namespace sphaira::devoptab::common
