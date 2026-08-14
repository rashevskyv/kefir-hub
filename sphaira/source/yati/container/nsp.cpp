#include "yati/container/nsp.hpp"
#include "yati/container/pfs0.hpp"
#include "defines.hpp"
#include "log.hpp"
#include <memory>
#include <cstring>
#include <optional>

namespace sphaira::yati::container {
namespace {

// stdio-like wrapper for std::vector
struct BufHelper {
    BufHelper() = default;
    BufHelper(std::span<const u8> data) {
        write(data);
    }

    void write(const void* data, u64 size) {
        if (offset + size >= buf.size()) {
            buf.resize(offset + size);
        }
        std::memcpy(buf.data() + offset, data, size);
        offset += size;
    }

    void write(std::span<const u8> data) {
        write(data.data(), data.size());
    }

    void seek(u64 where_to) {
        offset = where_to;
    }

    [[nodiscard]]
    auto tell() const {
        return offset;
    }

    std::vector<u8> buf;
    u64 offset{};
};

} // namespace

Result Nsp::GetCollections(Collections& out) {
    return GetCollections(out, 0);
}

Result Nsp::GetCollections(Collections& out, s64 off) {
    R_UNLESS(m_source != nullptr, Result_NspBadMagic);
    R_UNLESS(off >= 0, Result_NspBadMagic);

    auto read_exact = [&](void* dst, s64 read_off, s64 read_sz) -> Result {
        u64 bytes_read = 0;
        R_TRY(m_source->Read(dst, read_off, read_sz, &bytes_read));
        R_UNLESS(pfs0::IsExactRead(bytes_read, static_cast<u64>(read_sz)), Result_StreamUnexpectedEof);
        return 0;
    };

    // get header
    pfs0::Header header{};
    R_TRY(read_exact(&header, off, sizeof(header)));

    std::optional<u64> known_size{};
    s64 source_size = 0;
    const Result size_rc = m_source->GetSize(&source_size);
    if (R_SUCCEEDED(size_rc)) {
        R_UNLESS(source_size >= 0, FsError_InvalidSize);
        known_size = static_cast<u64>(source_size);
    } else if (size_rc == FsError_NotImplemented) {
        known_size = std::nullopt;
    } else {
        return size_rc;
    }

    u64 file_table_offset{};
    u64 string_table_offset{};
    u64 data_offset{};
    R_UNLESS(pfs0::ValidateHeader(header, off, file_table_offset, string_table_offset, data_offset, known_size) == pfs0::Error::Ok, Result_NspBadMagic);

    // get file table
    std::vector<pfs0::FileTableEntry> file_table(header.total_files);
    const u64 file_table_bytes = static_cast<u64>(header.total_files) * sizeof(pfs0::FileTableEntry);
    if (file_table_bytes > 0) {
        R_TRY(read_exact(file_table.data(), static_cast<s64>(file_table_offset), static_cast<s64>(file_table_bytes)));
    }

    // get string table
    std::vector<char> string_table(header.string_table_size);
    if (header.string_table_size > 0) {
        R_TRY(read_exact(string_table.data(), static_cast<s64>(string_table_offset), static_cast<s64>(header.string_table_size)));
    }

    std::vector<pfs0::ParsedEntry> parsed_entries;
    R_UNLESS(pfs0::ValidateEntries(file_table, string_table, data_offset, parsed_entries, known_size) == pfs0::Error::Ok, Result_NspBadMagic);

    out.reserve(out.size() + parsed_entries.size());
    for (auto& pe : parsed_entries) {
        out.emplace_back(CollectionEntry{
            .name = std::move(pe.name),
            .offset = pe.offset,
            .size = pe.size,
        });
    }

    R_SUCCEED();
}

auto Nsp::Build(std::span<const CollectionEntry> entries, s64& size) -> std::vector<u8> {
    BufHelper buf;

    pfs0::Header header{};
    std::vector<pfs0::FileTableEntry> file_table(entries.size());
    std::vector<char> string_table;

    u64 string_offset{};
    u64 data_offset{};

    for (u32 i = 0; i < entries.size(); i++) {
        file_table[i].data_offset = data_offset;
        file_table[i].data_size = entries[i].size;
        file_table[i].name_offset = string_offset;
        file_table[i].padding = 0;

        string_table.resize(string_offset + entries[i].name.length() + 1);
        std::memcpy(string_table.data() + string_offset, entries[i].name.c_str(), entries[i].name.length() + 1);

        data_offset += file_table[i].data_size;
        string_offset += entries[i].name.length() + 1;
    }

    // Add padding to the string table so that the header as a whole is well-aligned
    const auto nameless_header_size = sizeof(pfs0::Header) + (file_table.size() * sizeof(pfs0::FileTableEntry));
    auto padded_string_table_size = ((nameless_header_size + string_table.size() + 0x1F) & ~0x1F) - nameless_header_size;

    // Add manual padding if the full Partition FS header would already be properly aligned.
    if (padded_string_table_size == string_table.size()) {
        padded_string_table_size += 0x20;
    }

    string_table.resize(padded_string_table_size);

    header.magic = pfs0::PFS0_MAGIC;
    header.total_files = entries.size();
    header.string_table_size = string_table.size();
    header.padding = 0;

    buf.write(&header, sizeof(header));
    buf.write(file_table.data(), sizeof(pfs0::FileTableEntry) * file_table.size());
    buf.write(string_table.data(), string_table.size());

    // calculate nsp size.
    size = buf.tell();
    for (const auto& e : file_table) {
        size += e.data_size;
    }

    return buf.buf;
}

} // namespace sphaira::yati::container
