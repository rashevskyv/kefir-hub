#include "repack/bktr_table.hpp"

#include <cstring>

namespace sphaira::repack::bktr {
namespace {

constexpr uint64_t NODE_HEADER_SIZE = 0x10; // u32 index, u32 count, u64 end offset.

template<typename V>
V Rd(const uint8_t* p) {
    V v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

template<typename T, typename Decode>
bool Parse(std::span<const uint8_t> t, uint32_t count, uint64_t entry_size, std::vector<T>& out, uint64_t& end, Decode decode) {
    out.clear();
    const auto per_set = (NODE_SIZE - NODE_HEADER_SIZE) / entry_size;
    const auto sets = (count + per_set - 1) / per_set;

    // ponytail: L1 node only. Tables that need L2 nodes (over 2046 entry sets, about 1.6M indirect
    // entries) are rejected; no shipped update comes near that.
    if (!count || sets > (NODE_SIZE - NODE_HEADER_SIZE) / sizeof(uint64_t) || t.size() < NODE_SIZE * (1 + sets)) {
        return false;
    }
    if (Rd<uint32_t>(t.data() + 4) != sets) {
        return false;
    }
    end = Rd<uint64_t>(t.data() + 8);

    out.reserve(count);
    for (uint64_t s = 0; s < sets; s++) {
        const auto* set = t.data() + NODE_SIZE * (1 + s);
        const auto n = Rd<uint32_t>(set + 4);
        if (n > per_set) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            out.emplace_back(decode(set + NODE_HEADER_SIZE + i * entry_size));
        }
    }

    if (out.size() != count || out.front().offset != 0 || out.back().offset >= end) {
        return false;
    }
    for (size_t i = 1; i < out.size(); i++) {
        if (out[i].offset <= out[i - 1].offset) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ParseIndirect(std::span<const uint8_t> table, uint32_t count, std::vector<IndirectEntry>& out, uint64_t& end) {
    // entry: u64 virtual offset, u64 physical offset, s32 storage index (0x14 bytes).
    return Parse(table, count, 0x14, out, end, [](const uint8_t* p) {
        return IndirectEntry{Rd<uint64_t>(p), Rd<uint64_t>(p + 8), Rd<uint32_t>(p + 0x10)};
    }) && std::all_of(out.begin(), out.end(), [](const auto& e) { return e.storage <= STORAGE_PATCH; });
}

bool ParseCtrEx(std::span<const uint8_t> table, uint32_t count, std::vector<CtrExEntry>& out, uint64_t& end) {
    // entry: u64 offset, u32 size (unused here), u32 generation (0x10 bytes).
    return Parse(table, count, 0x10, out, end, [](const uint8_t* p) {
        return CtrExEntry{Rd<uint64_t>(p), Rd<uint32_t>(p + 0xC)};
    });
}

} // namespace sphaira::repack::bktr
