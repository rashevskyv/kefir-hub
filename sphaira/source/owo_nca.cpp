#include "owo_internal.hpp"
#include "yati/nx/npdm.hpp"
#include "yati/nx/crypto.hpp"
#include "nacp_util.hpp"
#include <switch.h>
#include <cstring>
#include <vector>
#include <span>

namespace sphaira {

struct Pfs0Header {
    u32 magic;
    u32 total_files;
    u32 string_table_size;
    u32 padding;
};

struct Pfs0FileTable {
    u64 data_offset;
    u64 data_size;
    u32 name_offset;
    u32 padding;
};

struct Pfs0StringTable {
    char name[256];
};

auto npdm_patch_kc(std::vector<u8>& npdm, u32 off, u32 size, u32 bitmask, u32 value) -> bool {
    const u32 pattern = BIT(bitmask) - 1;
    const u32 mask = BIT(bitmask) | pattern;

    for (u32 i = 0; i < size; i += 4) {
        u32 cup;
        std::memcpy(&cup, npdm.data() + off + i, sizeof(cup));
        if ((cup & mask) == pattern) {
            cup = value | pattern;
            std::memcpy(npdm.data() + off + i, &cup, sizeof(cup));
            return true;
        }
    }

    return false;
}

void patch_npdm(std::vector<u8>& npdm, const NpdmPatch& patch) {
    npdm::Meta meta{};
    npdm::Aci0 aci0{};
    npdm::Acid acid{};
    std::memcpy(&meta, npdm.data(), sizeof(meta));
    std::memcpy(&aci0, npdm.data() + meta.aci0_offset, sizeof(aci0));
    std::memcpy(&acid, npdm.data() + meta.acid_offset, sizeof(acid));

    // apply patch
    std::memcpy(meta.title_name, &patch.title_name, sizeof(meta.title_name));
    std::memcpy(meta.product_code, &patch.product_code, sizeof(patch.product_code));
    // ProcessAddressSpace lives in bits 1-3 of the meta flags.
    meta.flags = (meta.flags & ~0x0E) | (static_cast<u8>(patch.address_space) << 1);
    aci0.program_id = patch.tid;
    acid.program_id_min = patch.tid;
    acid.program_id_max = patch.tid;

    // patch debug flags based on ams version
    // SEE: https://github.com/ITotalJustice/sphaira/issues/67
    const auto force_debug = [&patch](){
        if (patch.svc_debug_mode != ForwarderSvcDebugMode::Automatic) {
            return patch.svc_debug_mode == ForwarderSvcDebugMode::Enabled;
        }

        u64 ver{};
        splInitialize();
        ON_SCOPE_EXIT(splExit());
        const auto SplConfigItem_ExosphereVersion = (SplConfigItem)65000;
        splGetConfig(SplConfigItem_ExosphereVersion, &ver);
        ver >>= 40;
        return ver >= MAKEHOSVERSION(1,8,0);
    }();

    if (force_debug) {
        npdm_patch_kc(npdm, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 16, BIT(19));
        npdm_patch_kc(npdm, meta.acid_offset + acid.kac_offset, acid.kac_size, 16, BIT(19));
    }

    std::memcpy(npdm.data(), &meta, sizeof(meta));
    std::memcpy(npdm.data() + meta.aci0_offset, &aci0, sizeof(aci0));
    std::memcpy(npdm.data() + meta.acid_offset, &acid, sizeof(acid));
}

void patch_nacp(NacpStruct& nacp, const NcapPatch& patch) {
    // patch title
    if (!patch.name.empty()) {
        for (s64 i = 0; i < 16; i++) {
            auto& lang = nacp_util::GetLanguageEntry(nacp, i);
            std::strncpy(lang.name, patch.name.c_str(), sizeof(lang.name)-1);
        }
    }

    // patch author
    if (!patch.author.empty()) {
        for (s64 i = 0; i < 16; i++) {
            auto& lang = nacp_util::GetLanguageEntry(nacp, i);
            std::strncpy(lang.author, patch.author.c_str(), sizeof(lang.author)-1);
        }
    }

    // misc
    nacp.startup_user_account = patch.profile_selection ? 0x01 : 0x00; // prompt for a user, or skip it
    nacp.user_account_switch_lock = 0x00; // allow account switch
    nacp.add_on_content_registration_type = 0x01; // on demand
    nacp.screenshot = patch.screenshot ? 0x0 : 0x1; // 0x0 = allowed
    // hos gates recording behind the capture button being usable at all, so
    // denying screenshots kills video capture too. write what actually happens.
    // 0x1 = Manual (hold capture). 0x2 = Auto crashes am (2128-0007) on homebrew titles.
    nacp.video_capture = (patch.screenshot && patch.video_capture) ? 0x1 : 0x0;
    nacp.logo_type = 0x2; // Nintendo
    nacp.logo_handling = 0x0; // auto
    nacp.data_loss_confirmation = 0x0; // disable as we don't use saves
    nacp.required_network_service_license_on_launch = 0x0; // don't require linked account
    const char error_code[] = "sphaira";
    static_assert(sizeof(error_code) <= 9);
    nacp.application_error_code_category = 0; // this is actually a char[8], not a u64 :)
    std::memcpy(&nacp.application_error_code_category, error_code, sizeof(error_code)-1);

    // update tid
    nacp.presence_group_id = patch.tid;
    nacp.save_data_owner_id = patch.tid;
    nacp.pseudo_device_id_seed = patch.tid;
    nacp.add_on_content_base_id = patch.tid ^ 0x1000;
    for (auto& id : nacp.local_communication_id) {
        id = patch.tid;
    }

    // enable play logging
    nacp.play_log_policy = 0x0; // open
    nacp.play_log_query_capability = 0x0;

    // disable save creation
    nacp.user_account_save_data_size = 0x0;
    nacp.user_account_save_data_journal_size = 0x0;
    nacp.device_save_data_size = 0x0;
    nacp.device_save_data_journal_size = 0x0;
    nacp.user_account_save_data_size_max = 0x0;
    nacp.user_account_save_data_journal_size_max = 0x0;
    nacp.device_save_data_size_max = 0x0;
    nacp.device_save_data_journal_size_max = 0x0;
}

void add_file_entry(FileEntries& entries, const char* name, const void* data, u64 size) {
    FileEntry entry;
    entry.name = name;
    entry.data.resize(size);
    std::memcpy(entry.data.data(), data, size);
    entries.emplace_back(entry);
}

void add_file_entry(FileEntries& entries, const char* name, std::span<const u8> data) {
    add_file_entry(entries, name, data.data(), data.size());
}

auto build_ivfc_master_hash(std::span<const u8> level1) -> std::vector<u8> {
    std::vector<u8> hash(SHA256_HASH_SIZE);
    sha256CalculateHash(hash.data(), level1.data(), level1.size());
    return hash;
}

auto build_pfs0(const FileEntries& entries) -> std::vector<u8> {
    BufHelper buf;

    Pfs0Header header{};
    std::vector<Pfs0FileTable> file_table(entries.size());
    std::vector<char> string_table;

    u64 string_offset{};
    u64 data_offset{};

    for (u32 i = 0; i < entries.size(); i++) {
        file_table[i].data_offset = data_offset;
        file_table[i].data_size = entries[i].data.size();
        file_table[i].name_offset = string_offset;
        file_table[i].padding = 0;

        string_table.resize(string_offset + entries[i].name.length() + 1);
        std::memcpy(string_table.data() + string_offset, entries[i].name.c_str(), entries[i].name.length() + 1);

        data_offset += entries[i].data.size();
        string_offset += entries[i].name.length() + 1;
    }

    // align table
    string_table.resize((string_table.size() + 0x1F) & ~0x1F);

    header.magic = 0x30534650;
    header.total_files = entries.size();
    header.string_table_size = string_table.size();
    header.padding = 0;

    buf.write(&header, sizeof(header));
    buf.write(file_table.data(), sizeof(Pfs0FileTable) * file_table.size());
    buf.write(string_table.data(), string_table.size());

    for (const auto&e : entries) {
        buf.write(e.data.data(), e.data.size());
    }

    return buf.buf;
}

auto build_pfs0_hash_table(const std::vector<u8>& pfs0, u32 block_size) -> std::vector<u8> {
    BufHelper buf;
    u8 hash[SHA256_HASH_SIZE];
    u32 read_size = block_size;

    for (u32 i = 0; i < pfs0.size(); i += read_size) {
        if (i + read_size >= pfs0.size()) {
            read_size = pfs0.size() - i;
        }
        sha256CalculateHash(hash, pfs0.data() + i, read_size);
        buf.write(hash, sizeof(hash));
    }

    return buf.buf;
}

auto build_pfs0_master_hash(const std::vector<u8>& pfs0_hash_table) -> std::vector<u8> {
    std::vector<u8> hash(SHA256_HASH_SIZE);
    sha256CalculateHash(hash.data(), pfs0_hash_table.data(), pfs0_hash_table.size());
    return hash;
}

void write_nca_padding(BufHelper& buf) {
    write_padding(buf, buf.tell(), 0x200);
}

void nca_encrypt_header(nca::Header* header, std::span<const u8> key) {
    Aes128XtsContext ctx{};
    aes128XtsContextCreate(&ctx, key.data(), key.data() + 0x10, true);

    u8 sector{};
    for (u64 pos = 0; pos < 0xC00; pos += 0x200) {
        aes128XtsContextResetSector(&ctx, sector++, true);
        aes128XtsEncrypt(&ctx, (u8*)header + pos, (const u8*)header + pos, 0x200);
    }
}

void write_nca_section(nca::Header& nca_header, u8 index, u64 start, u64 end) {
    auto& section = nca_header.fs_table[index];
    section.media_start_offset = start / 0x200; // 0xC00 / 0x200
    section.media_end_offset = end / 0x200; // Section end offset / 200
    section._0x8[0] = 0x1; // Always 1
}

void write_nca_fs_header_pfs0(nca::Header& nca_header, u8 index, const std::vector<u8>& master_hash, u64 hash_table_size, u32 block_size) {
    auto& fs_header = nca_header.fs_header[index];
    fs_header.hash_type = nca::HashType_HierarchicalSha256;
    fs_header.fs_type = nca::FileSystemType_PFS0;
    fs_header.version = 0x2; // Always 2
    fs_header.hash_data.hierarchical_sha256_data.layer_count = 0x2;
    fs_header.hash_data.hierarchical_sha256_data.block_size = block_size;
    fs_header.encryption_type = nca::EncryptionType_None;
    fs_header.hash_data.hierarchical_sha256_data.hash_layer.size = hash_table_size;
    std::memcpy(fs_header.hash_data.hierarchical_sha256_data.master_hash, master_hash.data(), master_hash.size());
    sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
}

void write_nca_fs_header_romfs(nca::Header& nca_header, u8 index) {
    auto& fs_header = nca_header.fs_header[index];
    fs_header.hash_type = nca::HashType_HierarchicalIntegrity;
    fs_header.fs_type = nca::FileSystemType_RomFS;
    fs_header.version = 0x2; // Always 2
    fs_header.hash_data.integrity_meta_info.magic = 0x43465649;
    fs_header.hash_data.integrity_meta_info.version = 0x20000; // Always 0x20000
    fs_header.hash_data.integrity_meta_info.master_hash_size = SHA256_HASH_SIZE;
    fs_header.hash_data.integrity_meta_info.info_level_hash.max_layers = 0x7;
    fs_header.encryption_type = nca::EncryptionType_None;
    fs_header.hash_data.integrity_meta_info.info_level_hash.levels[5].block_size = 0x0E; // 0x4000
    sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
}

void write_nca_pfs0(nca::Header& nca_header, u8 index, const FileEntries& entries, u32 block_size, BufHelper& buf) {
    const auto pfs0 = build_pfs0(entries);
    const auto pfs0_hash_table = build_pfs0_hash_table(pfs0, block_size);
    const auto pfs0_master_hash = build_pfs0_master_hash(pfs0_hash_table);

    buf.write(pfs0_hash_table.data(), pfs0_hash_table.size());
    const auto padding_size = write_padding(buf, pfs0_hash_table.size(), PFS0_PADDING_SIZE);

    nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.offset = pfs0_hash_table.size() + padding_size;
    nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.size = pfs0.size();

    buf.write(pfs0.data(), pfs0.size());
    write_nca_padding(buf);

    const auto section_start = index == 0 ? sizeof(nca_header) : nca_header.fs_table[index-1].media_end_offset * 0x200;
    write_nca_section(nca_header, index, section_start, buf.tell());
    write_nca_fs_header_pfs0(nca_header, index, pfs0_master_hash, pfs0_hash_table.size(), block_size);
}

auto ivfc_create_level(const std::vector<u8>& src) -> std::vector<u8> {
    BufHelper buf;
    u8 hash[SHA256_HASH_SIZE];
    u64 read_size = IVFC_HASH_BLOCK_SIZE;

    for (u32 i = 0; i < src.size(); i += read_size) {
        if (i + read_size >= src.size()) {
            read_size = src.size() - i;
        }
        sha256CalculateHash(hash, src.data() + i, read_size);
        buf.write(hash, sizeof(hash));
    }

    write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);

    return buf.buf;
}

void write_nca_romfs(nca::Header& nca_header, u8 index, const FileEntries& entries, u32 block_size, BufHelper& buf) {
    auto& fs_header = nca_header.fs_header[index];
    auto& meta_info = fs_header.hash_data.integrity_meta_info;
    auto& info_level_hash = meta_info.info_level_hash;

    std::vector<u8> ivfc[IVFC_MAX_LEVEL];

    ivfc[5] = romfs_build(entries, &info_level_hash.levels[5].hash_data_size);

    for (int b = 4; b >= 0; b--) {
        ivfc[b] = ivfc_create_level(ivfc[b + 1]);
        info_level_hash.levels[b].hash_data_size = ivfc[b].size();
        info_level_hash.levels[b].block_size = 0x0E; // 0x4000
    }

    info_level_hash.levels[0].logical_offset = 0;
    for (int i = 1; i <= 5; i++) {
        info_level_hash.levels[i].logical_offset = info_level_hash.levels[i - 1].logical_offset + info_level_hash.levels[i - 1].hash_data_size;
    }

    for (const auto& iv : ivfc) {
        buf.write(iv.data(), iv.size());
    }

    write_nca_padding(buf);

    const auto ivfc_master_hash = build_ivfc_master_hash(ivfc[0]);
    std::memcpy(meta_info.master_hash, ivfc_master_hash.data(), sizeof(meta_info.master_hash));

    const auto section_start = index == 0 ? sizeof(nca_header) : nca_header.fs_table[index-1].media_end_offset * 0x200;
    write_nca_section(nca_header, index, section_start, buf.tell());
    write_nca_fs_header_romfs(nca_header, index);
}

void write_nca_header_encypted(nca::Header& nca_header, u64 tid, const keys::Keys& keys, nca::ContentType type, BufHelper& buf) {
    nca_header.magic = NCA3_MAGIC;
    nca_header.distribution_type = nca::DistributionType_System;
    nca_header.content_type = type;
    nca_header.program_id = tid;
    nca_header.sdk_version = 0x000C1100;
    nca_header.size = buf.tell();

    nca_encrypt_header(&nca_header, keys.header_key);
    buf.seek(0);
    buf.write(&nca_header, sizeof(nca_header));
}

auto create_program_nca(u64 tid, const keys::Keys& keys, const FileEntries& exefs, const FileEntries& romfs, const FileEntries& logo) -> NcaEntry {
    BufHelper buf;
    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));

    write_nca_pfs0(nca_header, 0, exefs, PFS0_EXEFS_HASH_BLOCK_SIZE, buf);
    write_nca_romfs(nca_header, 1, romfs, IVFC_HASH_BLOCK_SIZE, buf);
    // only write logo if set (can only 1 file be added?)
    if (logo.size() == 2 && !logo[0].data.empty() && !logo[1].data.empty()) {
        write_nca_pfs0(nca_header, 2, logo, PFS0_LOGO_HASH_BLOCK_SIZE, buf);
    }
    write_nca_header_encypted(nca_header, tid, keys, nca::ContentType_Program, buf);

    return {buf, NcmContentType_Program};
}

auto create_control_nca(u64 tid, const keys::Keys& keys, const FileEntries& romfs) -> NcaEntry{
    nca::Header nca_header{};
    BufHelper buf;
    buf.write(&nca_header, sizeof(nca_header));

    write_nca_romfs(nca_header, 0, romfs, IVFC_HASH_BLOCK_SIZE, buf);
    write_nca_header_encypted(nca_header, tid, keys, nca::ContentType_Control, buf);

    return {buf, NcmContentType_Control};
}

auto create_meta_nca(u64 tid, const keys::Keys& keys, NcmStorageId storage_id, const std::vector<NcaEntry>& ncas) -> NcaMetaEntry {
    CnmtHeader cnmt_header{};
    NcmApplicationMetaExtendedHeader cnmt_extended{};
    NcmPackagedContentInfo packaged_content_info[2]{};
    u8 digest[0x20]{};
    BufHelper buf;

    cnmt_header.title_id = tid;
    cnmt_header.title_version = 0; // todo: parse nacp.disaply_version
    cnmt_header.meta_type = NcmContentMetaType_Application;
    cnmt_header.meta_header.extended_header_size = sizeof(cnmt_extended);
    cnmt_header.meta_header.content_count = 0x2; // program + control
    cnmt_header.meta_header.content_meta_count = 0x1; // only 1 meta
    cnmt_header.meta_header.attributes = 0x0;
    cnmt_header.meta_header.storage_id = storage_id;
    cnmt_extended.patch_id = cnmt_header.title_id | 0x800;

    for (u32 i = 0; i < ncas.size(); i++) {
        std::memcpy(packaged_content_info[i].hash, ncas[i].hash, sizeof(packaged_content_info[i].hash));
        std::memcpy(&packaged_content_info[i].info.content_id, ncas[i].hash, sizeof(packaged_content_info[i].info.content_id));
        packaged_content_info[i].info.content_type = ncas[i].type;
        ncmU64ToContentInfoSize(ncas[i].data.size(), &packaged_content_info[i].info);
    }

    // create control
    BufHelper cnmt_buf;
    cnmt_buf.write(&cnmt_header, sizeof(cnmt_header));
    cnmt_buf.write(&cnmt_extended, sizeof(cnmt_extended));
    cnmt_buf.write(&packaged_content_info, sizeof(packaged_content_info));
    cnmt_buf.write(digest, sizeof(digest));

    FileEntries cnmt;
    char cnmt_name[34];
    std::snprintf(cnmt_name, sizeof(cnmt_name), "Application_%016lX.cnmt", tid);
    add_file_entry(cnmt, cnmt_name, cnmt_buf.buf.data(), cnmt_buf.buf.size());

    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));
    write_nca_pfs0(nca_header, 0, cnmt, PFS0_META_HASH_BLOCK_SIZE, buf);
    write_nca_header_encypted(nca_header, tid, keys, nca::ContentType_Meta, buf);

    // entry
    NcaMetaEntry entry{buf, NcmContentType_Meta};

    // header
    entry.content_meta_header = cnmt_header.meta_header;
    entry.content_meta_header.content_count++;
    entry.content_meta_header.storage_id = 0;

    // key
    entry.content_meta_key.id = cnmt_header.title_id;
    entry.content_meta_key.version = cnmt_header.title_version;
    entry.content_meta_key.type = cnmt_header.meta_type;
    entry.content_meta_key.install_type = NcmContentInstallType_Full;
    std::memset(entry.content_meta_key.padding, 0, sizeof(entry.content_meta_key.padding));

    // record
    entry.content_storage_record.key = entry.content_meta_key;
    entry.content_storage_record.storage_id = storage_id;
    std::memset(entry.content_storage_record.padding, 0, sizeof(entry.content_storage_record.padding));

    // data
    entry.content_meta_data.header = entry.content_meta_header;
    entry.content_meta_data.extended = cnmt_extended;

    // meta content info
    std::memcpy(&entry.content_meta_data.infos[0].content_id, entry.nca_entry.hash, sizeof(entry.content_meta_data.infos[0].content_id));
    entry.content_meta_data.infos[0].content_type = entry.nca_entry.type;
    entry.content_meta_data.infos[0].attr = 0;
    ncmU64ToContentInfoSize(cnmt_buf.buf.size(), &entry.content_meta_data.infos[0]);
    entry.content_meta_data.infos[0].id_offset = 0;

    // program + control content info
    entry.content_meta_data.infos[1] = packaged_content_info[0].info;
    entry.content_meta_data.infos[2] = packaged_content_info[1].info;

    return entry;
}


} // namespace sphaira
