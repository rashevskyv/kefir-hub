#pragma once

#include <switch.h>
#include <cstring>
#include <vector>
#include <string>
#include <string_view>
#include <span>

#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/crypto.hpp"
#include "owo.hpp"

namespace sphaira {

inline constexpr u32 IVFC_MAX_LEVEL = 6;
inline constexpr u32 IVFC_HASH_BLOCK_SIZE = 0x4000;
inline constexpr u32 PFS0_EXEFS_HASH_BLOCK_SIZE = 0x10000;
inline constexpr u32 PFS0_LOGO_HASH_BLOCK_SIZE = 0x1000;
inline constexpr u32 PFS0_META_HASH_BLOCK_SIZE = 0x1000;
inline constexpr u32 PFS0_PADDING_SIZE = 0x200;
inline constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
inline constexpr u32 ROMFS_FILEPARTITION_OFS = 0x200;

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

struct NcaEntry {
    NcaEntry(const BufHelper& buf, NcmContentType _type) : data{buf.buf}, type{_type} {
        sha256CalculateHash(hash, data.data(), data.size());
    }

    const std::vector<u8> data;
    const u8 type;
    u8 hash[SHA256_HASH_SIZE];
};

struct CnmtHeader {
    u64 title_id;
    u32 title_version;
    u8 meta_type; // NcmContentMetaType
    u8 _0xD;
    NcmContentMetaHeader meta_header;
    u8 install_type; // NcmContentInstallType
    u8 _0x17;
    u32 required_sys_version;
    u8 _0x1C[0x4];
};
static_assert(sizeof(CnmtHeader) == 0x20);

struct NcmContentMetaData {
    NcmContentMetaHeader header;
    NcmApplicationMetaExtendedHeader extended;
    NcmContentInfo infos[3];
};

struct NcaMetaEntry {
    NcaMetaEntry(const BufHelper& buf, NcmContentType type) : nca_entry{buf, type} { }

    NcaEntry nca_entry;
    NcmContentMetaHeader content_meta_header{};
    NcmContentMetaKey content_meta_key{};
    ncm::ContentStorageRecord content_storage_record{};
    NcmContentMetaData content_meta_data{};
};

struct FileEntry {
    std::string name;
    std::vector<u8> data;
};

using FileEntries = std::vector<FileEntry>;

struct NpdmPatch {
    char title_name[0x10]{"Application"};
    char product_code[0x10]{};
    u64 tid;
    ForwarderAddressSpace address_space{ForwarderAddressSpace::Bit39};
    ForwarderSvcDebugMode svc_debug_mode{ForwarderSvcDebugMode::Automatic};
    CpuCoreMode core_mode{CpuCoreMode::Three};
};

struct NcapPatch {
    std::string name;
    std::string author;
    u64 tid;
    bool profile_selection{};
    bool screenshot{true};
    bool video_capture{true};
};

auto write_padding(BufHelper& buf, u64 off, u64 block) -> u64;
auto romfs_build(const FileEntries& entries, u64 *out_size) -> std::vector<u8>;
void build_romfs_into_file(const FileEntries& entries, BufHelper& buf);

auto patch_npdm(std::vector<u8>& npdm, const NpdmPatch& patch) -> bool;
void patch_nacp(NacpStruct& nacp, const NcapPatch& patch);
void add_file_entry(FileEntries& entries, const char* name, const void* data, u64 size);
void add_file_entry(FileEntries& entries, const char* name, std::span<const u8> data);

auto create_program_nca(u64 tid, const keys::Keys& keys, const FileEntries& exefs, const FileEntries& romfs, const FileEntries& logo) -> NcaEntry;
auto create_control_nca(u64 tid, const keys::Keys& keys, const FileEntries& romfs) -> NcaEntry;
auto create_meta_nca(u64 tid, const keys::Keys& keys, NcmStorageId storage_id, const std::vector<NcaEntry>& ncas) -> NcaMetaEntry;

} // namespace sphaira
