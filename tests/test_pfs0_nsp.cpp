#include "yati/container/pfs0.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace sphaira::pfs0;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_valid_pfs0() {
    Header header{
        .magic = PFS0_MAGIC,
        .total_files = 2,
        .string_table_size = 18,
        .padding = 0,
    };

    // string table: "game.nca\0game.tik\0" -> 9 + 9 = 18 bytes
    const char str_table[] = "game.nca\0game.tik\0";
    std::span<const char> string_table(str_table, sizeof(str_table) - 1);

    std::vector<FileTableEntry> file_table = {
        FileTableEntry{
            .data_offset = 0,
            .data_size = 0x1000,
            .name_offset = 0,
            .padding = 0,
        },
        FileTableEntry{
            .data_offset = 0x1000,
            .data_size = 0x200,
            .name_offset = 9,
            .padding = 0,
        },
    };

    std::uint64_t file_table_offset = 0;
    std::uint64_t string_table_offset = 0;
    std::uint64_t data_offset = 0;

    const std::uint64_t total_container_size = sizeof(Header) + (file_table.size() * sizeof(FileTableEntry)) + string_table.size() + 0x1200;

    CHECK(ValidateHeader(header, 0, file_table_offset, string_table_offset, data_offset, total_container_size) == Error::Ok);
    CHECK(file_table_offset == sizeof(Header));
    CHECK(string_table_offset == sizeof(Header) + 2 * sizeof(FileTableEntry));
    CHECK(data_offset == string_table_offset + 18);

    std::vector<ParsedEntry> entries;
    CHECK(ValidateEntries(file_table, string_table, data_offset, entries, total_container_size) == Error::Ok);
    CHECK(entries.size() == 2);
    CHECK(entries[0].name == "game.nca");
    CHECK(entries[0].offset == static_cast<std::int64_t>(data_offset));
    CHECK(entries[0].size == 0x1000);
    CHECK(entries[1].name == "game.tik");
    CHECK(entries[1].offset == static_cast<std::int64_t>(data_offset + 0x1000));
    CHECK(entries[1].size == 0x200);

    return 0;
}

static int test_short_metadata_read() {
    // Test the production exact-read predicate (IsExactRead)
    // Short header
    CHECK(!IsExactRead(10, sizeof(Header)));
    CHECK(!IsExactRead(0, sizeof(Header)));
    CHECK(IsExactRead(sizeof(Header), sizeof(Header)));

    // Short file table
    const size_t req_table_bytes = 3 * sizeof(FileTableEntry);
    CHECK(!IsExactRead(req_table_bytes - 1, req_table_bytes));
    CHECK(!IsExactRead(0, req_table_bytes));
    CHECK(IsExactRead(req_table_bytes, req_table_bytes));

    // Short string table
    const size_t req_str_bytes = 100;
    CHECK(!IsExactRead(99, req_str_bytes));
    CHECK(!IsExactRead(0, req_str_bytes));
    CHECK(IsExactRead(req_str_bytes, req_str_bytes));

    return 0;
}

static int test_hostile_allocation_fields() {
    std::uint64_t f_off = 0, s_off = 0, d_off = 0;

    // Bad magic
    Header bad_magic_header{
        .magic = 0x12345678,
        .total_files = 1,
        .string_table_size = 10,
        .padding = 0,
    };
    CHECK(ValidateHeader(bad_magic_header, 0, f_off, s_off, d_off) == Error::BadMagic);

    // Hostile total_files exceeding cap
    Header too_many_files{
        .magic = PFS0_MAGIC,
        .total_files = 0x10000, // 65536 > 65535
        .string_table_size = 10,
        .padding = 0,
    };
    CHECK(ValidateHeader(too_many_files, 0, f_off, s_off, d_off) == Error::TooManyFiles);

    Header max_u32_files{
        .magic = PFS0_MAGIC,
        .total_files = 0xFFFFFFFF,
        .string_table_size = 10,
        .padding = 0,
    };
    CHECK(ValidateHeader(max_u32_files, 0, f_off, s_off, d_off) == Error::TooManyFiles);

    // Hostile string_table_size exceeding cap
    Header too_large_string_table{
        .magic = PFS0_MAGIC,
        .total_files = 1,
        .string_table_size = 4 * 1024 * 1024 + 1,
        .padding = 0,
    };
    CHECK(ValidateHeader(too_large_string_table, 0, f_off, s_off, d_off) == Error::StringTableTooLarge);

    Header max_u32_string_table{
        .magic = PFS0_MAGIC,
        .total_files = 1,
        .string_table_size = 0xFFFFFFFF,
        .padding = 0,
    };
    CHECK(ValidateHeader(max_u32_string_table, 0, f_off, s_off, d_off) == Error::StringTableTooLarge);

    return 0;
}

static int test_invalid_name_offset_and_missing_nul() {
    const char raw_table[] = "program.nca"; // note: missing NUL terminator
    std::span<const char> string_table(raw_table, sizeof(raw_table) - 1);

    // 1. Missing NUL terminator
    std::vector<FileTableEntry> file_table_no_nul = {
        FileTableEntry{
            .data_offset = 0,
            .data_size = 0x100,
            .name_offset = 0,
            .padding = 0,
        },
    };
    std::vector<ParsedEntry> entries;
    CHECK(ValidateEntries(file_table_no_nul, string_table, 0x100, entries) == Error::MissingNameNullTerminator);

    // 2. Name offset at or past end of string table
    const char raw_table_valid[] = "program.nca\0";
    std::span<const char> string_table_valid(raw_table_valid, sizeof(raw_table_valid));

    std::vector<FileTableEntry> file_table_oob_name = {
        FileTableEntry{
            .data_offset = 0,
            .data_size = 0x100,
            .name_offset = static_cast<std::uint32_t>(string_table_valid.size()),
            .padding = 0,
        },
    };
    CHECK(ValidateEntries(file_table_oob_name, string_table_valid, 0x100, entries) == Error::InvalidNameOffset);

    std::vector<FileTableEntry> file_table_huge_name_offset = {
        FileTableEntry{
            .data_offset = 0,
            .data_size = 0x100,
            .name_offset = 0xFFFFFFFF,
            .padding = 0,
        },
    };
    CHECK(ValidateEntries(file_table_huge_name_offset, string_table_valid, 0x100, entries) == Error::InvalidNameOffset);

    return 0;
}

static int test_overflowing_data_offset_and_size() {
    std::uint64_t f_off = 0, s_off = 0, d_off = 0;

    // Negative container offset
    Header header{
        .magic = PFS0_MAGIC,
        .total_files = 1,
        .string_table_size = 10,
        .padding = 0,
    };
    CHECK(ValidateHeader(header, -1, f_off, s_off, d_off) == Error::InvalidContainerOffset);

    // Container offset near INT64_MAX causing offset overflow
    CHECK(ValidateHeader(header, static_cast<std::int64_t>(MAX_S64), f_off, s_off, d_off) == Error::OffsetOverflow);

    // File entry data offset overflow
    const char str_table[] = "file.nca\0";
    std::span<const char> string_table(str_table, sizeof(str_table));

    std::vector<FileTableEntry> file_table_overflow_offset = {
        FileTableEntry{
            .data_offset = 0xFFFFFFFFFFFFFFFFULL,
            .data_size = 0x100,
            .name_offset = 0,
            .padding = 0,
        },
    };
    std::vector<ParsedEntry> entries;
    CHECK(ValidateEntries(file_table_overflow_offset, string_table, 0x1000, entries) == Error::DataOffsetOverflow);

    // File entry data size overflow (data_offset + data_size wraps)
    std::vector<FileTableEntry> file_table_overflow_size = {
        FileTableEntry{
            .data_offset = 0x100,
            .data_size = 0xFFFFFFFFFFFFFFFFULL,
            .name_offset = 0,
            .padding = 0,
        },
    };
    CHECK(ValidateEntries(file_table_overflow_size, string_table, 0x1000, entries) == Error::DataOffsetOverflow);

    // File entry exceeding signed 63-bit range
    std::vector<FileTableEntry> file_table_exceed_s64 = {
        FileTableEntry{
            .data_offset = 0,
            .data_size = MAX_S64 + 1ULL,
            .name_offset = 0,
            .padding = 0,
        },
    };
    CHECK(ValidateEntries(file_table_exceed_s64, string_table, 0x1000, entries) == Error::DataOffsetOverflow);

    return 0;
}

static int test_known_size_overrun() {
    Header header{
        .magic = PFS0_MAGIC,
        .total_files = 1,
        .string_table_size = 10,
        .padding = 0,
    };

    const char str_table[] = "file.nca\0";
    std::span<const char> string_table(str_table, sizeof(str_table));

    std::vector<FileTableEntry> file_table = {
        FileTableEntry{
            .data_offset = 0,
            .data_size = 0x1000,
            .name_offset = 0,
            .padding = 0,
        },
    };

    std::uint64_t f_off = 0, s_off = 0, d_off = 0;

    // Header and tables exceeding known size
    const std::uint64_t small_size = 20; // smaller than sizeof(Header) + FileTableEntry + string_table
    CHECK(ValidateHeader(header, 0, f_off, s_off, d_off, small_size) == Error::SizeExceedsContainer);

    // File data exceeding known container size
    const std::uint64_t exact_header_and_tables_size = sizeof(Header) + sizeof(FileTableEntry) + 10; // = 16 + 24 + 10 = 50
    CHECK(ValidateHeader(header, 0, f_off, s_off, d_off, exact_header_and_tables_size + 0x500) == Error::Ok);

    std::vector<ParsedEntry> entries;
    // Overrun: file size is 0x1000, but only 0x500 available past headers
    CHECK(ValidateEntries(file_table, string_table, d_off, entries, exact_header_and_tables_size + 0x500) == Error::SizeExceedsContainer);

    // Exact fit: exactly 0x1000 available past headers
    CHECK(ValidateEntries(file_table, string_table, d_off, entries, exact_header_and_tables_size + 0x1000) == Error::Ok);
    CHECK(entries.size() == 1);
    CHECK(entries[0].size == 0x1000);

    // Unknown size (stream): accepts without size cap
    CHECK(ValidateEntries(file_table, string_table, d_off, entries, std::nullopt) == Error::Ok);

    return 0;
}

int main() {
    if (test_valid_pfs0()) return 1;
    if (test_short_metadata_read()) return 1;
    if (test_hostile_allocation_fields()) return 1;
    if (test_invalid_name_offset_and_missing_nul()) return 1;
    if (test_overflowing_data_offset_and_size()) return 1;
    if (test_known_size_overrun()) return 1;

    std::printf("test_pfs0_nsp: %d checks passed\n", g_checks);
    return 0;
}
