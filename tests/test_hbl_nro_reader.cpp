// Host test for HBL NRO loading logic (hbl/source/main.c)
// Verifies segment bounds, BSS calculation and protection against RomFS asset overflow into BSS/heap.
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_hbl_nro_reader.cpp -o /tmp/t && /tmp/t

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <span>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

namespace {

#pragma pack(push, 1)
struct NroStart {
    uint32_t unused[4];
};

struct NroSegment {
    uint32_t file_off;
    uint32_t size;
};

struct NroHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t flags;
    NroSegment segments[3];
    uint32_t bss_size;
    uint32_t reserved;
    uint8_t  build_id[0x20];
    uint8_t  dso_handle[0x20];
};
#pragma pack(pop)

constexpr uint32_t NROHEADER_MAGIC = 0x304F524E; // 'NRO0'

uint64_t CalcHeapSize(uint64_t mem_available, uint64_t mem_used, bool recording) {
    uint64_t size = 0;
    if (mem_available > mem_used + 0x200000)
        size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
        size = 0x2000000 * 16;
    if (size > 0x6000000 && recording)
        size -= 0x6000000;
    return size;
}

struct MockMemRegion {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
    uint32_t attr;
    uint32_t perm;
};

constexpr uint32_t MemType_Heap = 5;
constexpr uint32_t MemState_Type = 0xFF;
constexpr uint32_t Perm_Rw = 3;
constexpr uint32_t Perm_None = 0;

bool FindUsableHeapRangeMock(uint64_t override_addr, uint64_t override_size,
                            const std::vector<MockMemRegion>& regions,
                            uint64_t* out_start, uint64_t* out_size) {
    if (override_size == 0 || override_addr == 0 || (override_addr + override_size) <= override_addr) {
        return false;
    }

    const uint64_t override_end = override_addr + override_size;
    uint64_t cur_addr = override_addr;

    uint64_t best_start = 0;
    uint64_t best_size = 0;
    uint64_t current_usable_start = 0;
    uint64_t current_usable_size = 0;

    while (cur_addr < override_end) {
        const MockMemRegion* found = nullptr;
        for (const auto& r : regions) {
            if (cur_addr >= r.addr && cur_addr < (r.addr + r.size)) {
                found = &r;
                break;
            }
        }
        if (!found || found->size == 0 || (found->addr + found->size) <= found->addr) {
            return false;
        }

        const uint64_t mem_end = found->addr + found->size;
        const uint64_t block_start = (found->addr < override_addr) ? override_addr : found->addr;
        const uint64_t block_end = (mem_end > override_end) ? override_end : mem_end;
        const uint64_t block_size = block_end - block_start;

        const bool is_usable = ((found->type & MemState_Type) == MemType_Heap) &&
                               (found->perm == Perm_Rw) &&
                               (found->attr == 0);

        if (is_usable && (block_start % 0x1000 == 0) && (block_size % 0x1000 == 0)) {
            if (current_usable_size > 0 && (current_usable_start + current_usable_size) == block_start) {
                current_usable_size += block_size;
            } else {
                if (current_usable_size > best_size) {
                    best_size = current_usable_size;
                    best_start = current_usable_start;
                }
                current_usable_start = block_start;
                current_usable_size = block_size;
            }
        } else {
            if (current_usable_size > best_size) {
                best_size = current_usable_size;
                best_start = current_usable_start;
            }
            current_usable_start = 0;
            current_usable_size = 0;
        }

        if (mem_end <= cur_addr) {
            return false;
        }
        cur_addr = mem_end;
    }

    if (current_usable_size > best_size) {
        best_size = current_usable_size;
        best_start = current_usable_start;
    }

    if (best_size == 0) {
        return false;
    }

    *out_start = best_start;
    *out_size = best_size;
    return true;
}

bool CheckRwSizeBounds(uint64_t heap_size, uint32_t seg2_off, uint32_t seg2_size, uint32_t bss_size) {
    const uint64_t seg2_off_u64 = (uint64_t)seg2_off;
    const uint64_t seg2_size_u64 = (uint64_t)seg2_size;
    const uint64_t bss_size_u64 = (uint64_t)bss_size;

    const uint64_t rw_size_raw = seg2_size_u64 + bss_size_u64;
    if (rw_size_raw < seg2_size_u64) {
        return false;
    }

    const uint64_t rw_size_aligned = (rw_size_raw + 0xFFFULL) & ~0xFFFULL;
    if (rw_size_aligned < rw_size_raw) {
        return false;
    }

    if (seg2_off_u64 + rw_size_aligned < seg2_off_u64 || (seg2_off_u64 + rw_size_aligned) > heap_size) {
        return false;
    }

    return true;
}

bool CheckNroHeapBounds(uint64_t heap_size, uint32_t nro_size, uint32_t bss_size, uint32_t seg2_off, uint32_t seg2_size) {
    const uint64_t nro_size_u64 = (uint64_t)nro_size;
    const uint64_t bss_size_u64 = (uint64_t)bss_size;
    const uint64_t seg2_off_u64 = (uint64_t)seg2_off;
    const uint64_t seg2_size_u64 = (uint64_t)seg2_size;

    if (nro_size_u64 < sizeof(NroStart) + sizeof(NroHeader) || nro_size_u64 > heap_size) {
        return false;
    }

    if (seg2_off_u64 >= nro_size_u64 || seg2_size_u64 > nro_size_u64 ||
        (seg2_off_u64 + seg2_size_u64) < seg2_off_u64 || (seg2_off_u64 + seg2_size_u64) > nro_size_u64) {
        return false;
    }

    if (!CheckRwSizeBounds(heap_size, seg2_off, seg2_size, bss_size)) {
        return false;
    }

    const uint64_t total_size_raw = nro_size_u64 + bss_size_u64;
    if (total_size_raw < nro_size_u64) {
        return false;
    }

    const uint64_t total_size_aligned = (total_size_raw + 0xFFFULL) & ~0xFFFULL;
    if (total_size_aligned < total_size_raw || total_size_aligned > heap_size) {
        return false;
    }

    return true;
}

struct MockNroReader {
    static int TestNroReaderLogic() {
        CHECK(sizeof(NroStart) == 0x10);
        CHECK(sizeof(NroHeader) == 0x70);

        // Construct a synthetic NRO with appended 12MB RomFS asset (like pipensx or NX-Activity-Log)
        const uint32_t code_size = 0x400000; // 4MB NRO code+data
        const uint32_t bss_size  = 0x080000; // 512KB BSS
        const uint32_t romfs_size = 0xC00000; // 12MB RomFS payload
        const uint32_t total_file_size = code_size + romfs_size;

        std::vector<uint8_t> file_data(total_file_size);
        // Fill file with distinctive non-zero pattern
        for (size_t i = 0; i < file_data.size(); i++) {
            file_data[i] = static_cast<uint8_t>((i % 251) + 1);
        }

        // Setup valid NroHeader at offset 0x10
        auto* header = reinterpret_cast<NroHeader*>(file_data.data() + sizeof(NroStart));
        header->magic = NROHEADER_MAGIC;
        header->version = 0;
        header->size = code_size;
        header->flags = 0;
        header->segments[0] = { 0, 0x200000 };          // text (2MB)
        header->segments[1] = { 0x200000, 0x100000 };   // rodata (1MB)
        header->segments[2] = { 0x300000, 0x100000 };   // data (1MB)
        header->bss_size = bss_size;

        // Simulate HBL memory buffer (Heap buffer)
        std::vector<uint8_t> heap_buffer(0x2000000, 0xAA); // 32MB heap pre-filled with 0xAA (dirty heap)

        uint8_t* nrobuf = heap_buffer.data();
        auto* start = reinterpret_cast<NroStart*>(nrobuf + 0);
        auto* loaded_header = reinterpret_cast<NroHeader*>(nrobuf + sizeof(NroStart));
        uint8_t* rest = nrobuf + sizeof(NroStart) + sizeof(NroHeader);

        // Step 1: Read NroStart
        int64_t offset = 0;
        std::memcpy(start, file_data.data() + offset, sizeof(*start));
        offset += sizeof(*start);

        // Step 2: Read NroHeader
        std::memcpy(loaded_header, file_data.data() + offset, sizeof(*loaded_header));
        offset += sizeof(*loaded_header);

        CHECK(loaded_header->magic == NROHEADER_MAGIC);
        CHECK(loaded_header->size == code_size);
        CHECK(loaded_header->size >= sizeof(NroStart) + sizeof(NroHeader));

        // Step 3: Read rest_size (only up to header->size, never the RomFS asset)
        const size_t rest_size = loaded_header->size - (sizeof(NroStart) + sizeof(NroHeader));
        CHECK(rest_size == code_size - 0x80);
        std::memcpy(rest, file_data.data() + offset, rest_size);

        // Step 4: Calculate total_size and zero out BSS
        const size_t total_size = (loaded_header->size + loaded_header->bss_size + 0xFFF) & ~0xFFF;
        CHECK(total_size == code_size + bss_size);

        if (total_size > loaded_header->size) {
            std::memset(nrobuf + loaded_header->size, 0, total_size - loaded_header->size);
        }

        // Verify BSS is strictly zeroed
        for (size_t i = loaded_header->size; i < total_size; i++) {
            CHECK(nrobuf[i] == 0);
        }

        // Verify RomFS was NOT written into BSS or Heap past code_size
        for (size_t i = total_size; i < total_size + 0x1000; i++) {
            CHECK(nrobuf[i] == 0xAA); // Untouched heap
        }

        // Step 5: Verify nro_heap_start and nro_heap_size
        size_t rw_size = loaded_header->segments[2].size + loaded_header->bss_size;
        rw_size = (rw_size + 0xFFF) & ~0xFFF;
        const uint64_t nro_size = loaded_header->segments[2].file_off + rw_size;
        const uint64_t nro_heap_start = nro_size;
        CHECK(nro_heap_start == total_size);

        // Step 6: Verify Heap calculation behavior
        // Forwarder Application with 512MB total, 16MB used, no recording
        uint64_t heap_app = CalcHeapSize(0x20000000, 0x1000000, false);
        CHECK(heap_app == 0x1EE00000); // Does NOT subtract 96MB

        // Forwarder Application with 512MB total, 16MB used, recording enabled
        uint64_t heap_rec = CalcHeapSize(0x20000000, 0x1000000, true);
        CHECK(heap_rec == 0x18E00000); // Subtracts 96MB (0x6000000)

        // Step 7: Verify memory hole / non-contiguous heap scan logic
        {
            // Fully contiguous 64MB heap
            std::vector<MockMemRegion> mem_contiguous = {
                { 0x20000000, 0x04000000, MemType_Heap, 0, Perm_Rw }
            };
            uint64_t out_start = 0, out_size = 0;
            CHECK(FindUsableHeapRangeMock(0x20000000, 0x04000000, mem_contiguous, &out_start, &out_size));
            CHECK(out_start == 0x20000000);
            CHECK(out_size == 0x04000000);
        }

        {
            // Heap with a 1MB Perm_None hole in the middle (e.g. 16MB RW, 1MB None, 31MB RW)
            std::vector<MockMemRegion> mem_with_hole = {
                { 0x20000000, 0x01000000, MemType_Heap, 0, Perm_Rw },   // 16MB
                { 0x21000000, 0x00100000, MemType_Heap, 0, Perm_None }, // 1MB hole (Perm_None)
                { 0x21100000, 0x01F00000, MemType_Heap, 0, Perm_Rw },   // 31MB
            };
            uint64_t out_start = 0, out_size = 0;
            CHECK(FindUsableHeapRangeMock(0x20000000, 0x03000000, mem_with_hole, &out_start, &out_size));
            // Must pick the largest contiguous valid chunk (31MB at 0x21100000), NEVER bridge the hole
            CHECK(out_start == 0x21100000);
            CHECK(out_size == 0x01F00000);
        }

        {
            // Heap with borrowed/non-zero attribute page (e.g. 32MB clean, 64KB attr=1, 16MB clean)
            std::vector<MockMemRegion> mem_with_attr = {
                { 0x20000000, 0x02000000, MemType_Heap, 0, Perm_Rw },   // 32MB
                { 0x22000000, 0x00010000, MemType_Heap, 1, Perm_Rw },   // 64KB borrowed (attr != 0)
                { 0x22010000, 0x01000000, MemType_Heap, 0, Perm_Rw },   // 16MB
            };
            uint64_t out_start = 0, out_size = 0;
            CHECK(FindUsableHeapRangeMock(0x20000000, 0x03010000, mem_with_attr, &out_start, &out_size));
            // Must pick the largest contiguous chunk (32MB at 0x20000000)
            CHECK(out_start == 0x20000000);
            CHECK(out_size == 0x02000000);
        }

        {
            // Heap with non-heap memory type block (e.g. MemType_CodeStatic)
            std::vector<MockMemRegion> mem_with_code = {
                { 0x20000000, 0x00800000, MemType_Heap, 0, Perm_Rw },   // 8MB
                { 0x20800000, 0x00800000, 3,            0, Perm_Rw },   // 8MB non-heap (type=3)
                { 0x21000000, 0x01000000, MemType_Heap, 0, Perm_Rw },   // 16MB
            };
            uint64_t out_start = 0, out_size = 0;
            CHECK(FindUsableHeapRangeMock(0x20000000, 0x02000000, mem_with_code, &out_start, &out_size));
            CHECK(out_start == 0x21000000);
            CHECK(out_size == 0x01000000);
        }

        {
            // Completely unusable / missing heap
            std::vector<MockMemRegion> mem_bad = {
                { 0x20000000, 0x01000000, MemType_Heap, 0, Perm_None }
            };
            uint64_t out_start = 0, out_size = 0;
            CHECK(!FindUsableHeapRangeMock(0x20000000, 0x01000000, mem_bad, &out_start, &out_size));
        }

        // Step 8: Verify NRO size boundary and overflow protection
        {
            const uint64_t heap_size = 0x2000000; // 32MB

            // Valid NRO fits in heap
            CHECK(CheckNroHeapBounds(heap_size, 0x400000, 0x80000, 0x300000, 0x100000));

            // Oversized NRO code exceeds heap
            CHECK(!CheckNroHeapBounds(heap_size, 0x3000000, 0x80000, 0x2F00000, 0x100000));

            // Code fits, but BSS expands past heap size
            CHECK(!CheckNroHeapBounds(heap_size, 0x1000000, 0x2000000, 0xF00000, 0x100000));

            // Integer overflow in code_size + bss_size
            CHECK(!CheckNroHeapBounds(heap_size, 0x1000000, 0xFFFFFFF0, 0xF00000, 0x100000));

            // 32-bit wrap in seg2_size + bss_size (e.g. 0x10000 + 0xFFFFFFFF = 0x10000FFFF > heap_size)
            // In 32-bit addition, 0x10000 + 0xFFFFFFFF wraps to 0xFFFF, page-aligned to 0x10000 <= 32MB!
            // CheckRwSizeBounds must reject this in 64-bit arithmetic:
            CHECK(!CheckRwSizeBounds(heap_size, 0, 0x10000, 0xFFFFFFFF));
            CHECK(!CheckRwSizeBounds(heap_size, 0, 0x80000000, 0x80000000)); // 32-bit wraps to 0x00000000
            CHECK(!CheckNroHeapBounds(heap_size, 0x10000, 0xFFFFFFFF, 0, 0x10000));

            // Segment file_off + size 32-bit wrap
            CHECK(!CheckNroHeapBounds(heap_size, 0x100000, 0x1000, 0xFFFFFF00, 0x200));
        }

        return 0;
    }
};

} // namespace

int main() {
    if (MockNroReader::TestNroReaderLogic() != 0) {
        return 1;
    }
    std::printf("ok  hbl_nro_reader: %d checks passed\n", g_checks);
    return 0;
}
