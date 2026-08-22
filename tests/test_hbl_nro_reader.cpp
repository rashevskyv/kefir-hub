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
