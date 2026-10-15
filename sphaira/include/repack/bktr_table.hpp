#pragma once

// BKTR bucket tables of a patch (update) NCA romfs section, libnx-free so the host tests can use it.
// Indirect table: which byte range of the patched section comes from the base NCA (storage 0) or from
// the patch NCA (storage 1). AesCtrEx table: which AES-CTR generation encrypts each range of the patch.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sphaira::repack::bktr {

constexpr uint64_t NODE_SIZE = 0x4000;
constexpr uint32_t STORAGE_BASE = 0;
constexpr uint32_t STORAGE_PATCH = 1;

struct IndirectEntry {
    uint64_t offset; // virtual offset in the patched section.
    uint64_t phys;   // offset in the storage below.
    uint32_t storage;
};

struct CtrExEntry {
    uint64_t offset; // offset in the patch section.
    uint32_t generation;
};

// table: the decrypted table as stored at PatchInfo offset/size (L1 node, then the entry sets).
// count: BucketTreeHeader::count. end: end of the covered range, from the L1 node.
// Returns false on a malformed table.
bool ParseIndirect(std::span<const uint8_t> table, uint32_t count, std::vector<IndirectEntry>& out, uint64_t& end);
bool ParseCtrEx(std::span<const uint8_t> table, uint32_t count, std::vector<CtrExEntry>& out, uint64_t& end);

// Upper half of the AES-CTR counter for one AesCtrEx range: the section counter with the generation in
// its low 32 bits (counter bytes 4..7, as hactool's nca_update_bktr_ctr).
constexpr uint64_t CtrUpper(uint64_t section_ctr, uint32_t generation) {
    return (section_ctr & 0xFFFFFFFF00000000ull) | generation;
}

// Calls fn(entry, offset, length) for each piece of [off, off + size) that one entry covers.
// entries: sorted by offset, the first at 0 (as the parsers return them). Returns false when the range
// is outside [0, end) or fn returns false.
template<typename T, typename Fn>
bool ForEachPiece(const std::vector<T>& entries, uint64_t end, uint64_t off, uint64_t size, Fn&& fn) {
    if (entries.empty() || off > end || size > end - off) {
        return false;
    }

    // last entry with entry.offset <= off.
    size_t lo = 0, hi = entries.size();
    while (hi - lo > 1) {
        const auto mid = lo + (hi - lo) / 2;
        if (entries[mid].offset <= off) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    for (auto i = lo; size; i++) {
        const auto next = i + 1 < entries.size() ? entries[i + 1].offset : end;
        const auto len = std::min<uint64_t>(size, next - off);
        if (!fn(entries[i], off, len)) {
            return false;
        }
        off += len;
        size -= len;
    }
    return true;
}

} // namespace sphaira::repack::bktr
