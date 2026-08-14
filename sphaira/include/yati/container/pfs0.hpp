#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sphaira::pfs0 {

constexpr std::uint32_t PFS0_MAGIC = 0x30534650; // "PFS0"
constexpr std::uint32_t MAX_PFS0_FILES = 0xFFFF; // 65535 files max
constexpr std::uint32_t MAX_PFS0_STRING_TABLE_SIZE = 4 * 1024 * 1024; // 4 MiB max
constexpr std::uint64_t MAX_S64 = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

struct Header {
    std::uint32_t magic;
    std::uint32_t total_files;
    std::uint32_t string_table_size;
    std::uint32_t padding;
};
static_assert(sizeof(Header) == 0x10, "Pfs0 Header size must be 0x10 bytes");

struct FileTableEntry {
    std::uint64_t data_offset;
    std::uint64_t data_size;
    std::uint32_t name_offset;
    std::uint32_t padding;
};
static_assert(sizeof(FileTableEntry) == 0x18, "Pfs0 FileTableEntry size must be 0x18 bytes");

constexpr inline bool IsExactRead(std::uint64_t bytes_read, std::uint64_t requested_size) {
    return bytes_read == requested_size;
}

constexpr inline bool CheckedAdd(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        return false;
    }
    out = a + b;
    return true;
}

enum class Error {
    Ok = 0,
    BadMagic,
    TooManyFiles,
    StringTableTooLarge,
    OffsetOverflow,
    InvalidNameOffset,
    MissingNameNullTerminator,
    DataOffsetOverflow,
    SizeExceedsContainer,
    InvalidContainerOffset,
};

struct ParsedEntry {
    std::string name;
    std::int64_t offset;
    std::int64_t size;
};

inline Error ValidateHeader(const Header& header, std::int64_t container_offset,
                           std::uint64_t& out_file_table_offset,
                           std::uint64_t& out_string_table_offset,
                           std::uint64_t& out_data_offset,
                           std::optional<std::uint64_t> known_source_size = std::nullopt) {
    if (container_offset < 0) {
        return Error::InvalidContainerOffset;
    }
    if (header.magic != PFS0_MAGIC) {
        return Error::BadMagic;
    }
    if (header.total_files > MAX_PFS0_FILES) {
        return Error::TooManyFiles;
    }
    if (header.string_table_size > MAX_PFS0_STRING_TABLE_SIZE) {
        return Error::StringTableTooLarge;
    }

    const std::uint64_t cur_off = static_cast<std::uint64_t>(container_offset);
    if (!CheckedAdd(cur_off, sizeof(Header), out_file_table_offset) || out_file_table_offset > MAX_S64) {
        return Error::OffsetOverflow;
    }

    const std::uint64_t file_table_bytes = static_cast<std::uint64_t>(header.total_files) * sizeof(FileTableEntry);
    if (!CheckedAdd(out_file_table_offset, file_table_bytes, out_string_table_offset) || out_string_table_offset > MAX_S64) {
        return Error::OffsetOverflow;
    }

    if (!CheckedAdd(out_string_table_offset, header.string_table_size, out_data_offset) || out_data_offset > MAX_S64) {
        return Error::OffsetOverflow;
    }

    if (known_source_size.has_value() && out_data_offset > *known_source_size) {
        return Error::SizeExceedsContainer;
    }

    return Error::Ok;
}

inline Error ValidateEntries(std::span<const FileTableEntry> file_table,
                            std::span<const char> string_table,
                            std::uint64_t data_offset,
                            std::vector<ParsedEntry>& out_entries,
                            std::optional<std::uint64_t> known_source_size = std::nullopt) {
    out_entries.clear();
    out_entries.reserve(file_table.size());

    for (const auto& file : file_table) {
        if (file.name_offset >= string_table.size()) {
            return Error::InvalidNameOffset;
        }

        const char* name_ptr = string_table.data() + file.name_offset;
        const size_t max_name_len = string_table.size() - file.name_offset;
        const void* nul_ptr = std::memchr(name_ptr, '\0', max_name_len);
        if (!nul_ptr) {
            return Error::MissingNameNullTerminator;
        }

        std::uint64_t entry_abs_offset = 0;
        if (!CheckedAdd(data_offset, file.data_offset, entry_abs_offset) || entry_abs_offset > MAX_S64) {
            return Error::DataOffsetOverflow;
        }

        std::uint64_t entry_abs_end = 0;
        if (!CheckedAdd(entry_abs_offset, file.data_size, entry_abs_end) || entry_abs_end > MAX_S64) {
            return Error::DataOffsetOverflow;
        }
        if (file.data_size > MAX_S64) {
            return Error::DataOffsetOverflow;
        }

        if (known_source_size.has_value() && entry_abs_end > *known_source_size) {
            return Error::SizeExceedsContainer;
        }

        out_entries.push_back({
            .name = std::string(name_ptr),
            .offset = static_cast<std::int64_t>(entry_abs_offset),
            .size = static_cast<std::int64_t>(file.data_size),
        });
    }

    return Error::Ok;
}

} // namespace sphaira::pfs0
