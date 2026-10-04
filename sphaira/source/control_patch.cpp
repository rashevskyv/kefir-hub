#include "control_patch.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"

#include <array>
#include <cstring>
#include <memory>

namespace sphaira::control_patch {
namespace {

// control NCAs are the nacp plus up to 16 jpeg icons; anything bigger is not one we understand.
constexpr s64 MAX_CONTROL_SIZE = 32 * 1024 * 1024;
constexpr s64 CHUNK = 1024 * 1024;

void Sha256(const u8* data, std::size_t size, u8 out[32]) {
    sha256CalculateHash(out, data, size);
}

void CryptSection(u8* data, u64 size, const u8* key, u64 section_ctr, u64 nca_offset) {
    u8 ctr[0x10]{};
    crypto::SetCtr(ctr, section_ctr, nca_offset);
    Aes128CtrContext ctx;
    aes128CtrContextCreate(&ctx, key, ctr);
    aes128CtrCrypt(&ctx, data, data, size);
}

Result ReadContent(NcmContentStorage* cs, const NcmContentId& id, std::vector<u8>& out) {
    s64 size{};
    R_TRY(ncmContentStorageGetSizeFromContentId(cs, &size, &id));
    R_UNLESS(size > 0 && size <= MAX_CONTROL_SIZE, Result_NcaBadMagic);
    out.resize(size);
    for (s64 off = 0; off < size; off += CHUNK) {
        R_TRY(ncmContentStorageReadContentIdFile(cs, out.data() + off, std::min(CHUNK, size - off), &id, off));
    }
    R_SUCCEED();
}

// same id, new bytes: placeholder, then ncm::Register replaces the registered content.
Result WriteContent(NcmContentStorage* cs, const NcmContentId& id, const std::vector<u8>& data) {
    NcmPlaceHolderId ph{};
    R_TRY(ncmContentStorageGeneratePlaceHolderId(cs, &ph));
    R_TRY(ncmContentStorageCreatePlaceHolder(cs, &id, &ph, data.size()));
    bool registered = false;
    ON_SCOPE_EXIT(if (!registered) { ncmContentStorageDeletePlaceHolder(cs, &ph); });
    for (s64 off = 0; off < static_cast<s64>(data.size()); off += CHUNK) {
        R_TRY(ncmContentStorageWritePlaceHolder(cs, &ph, off, data.data() + off, std::min<s64>(CHUNK, data.size() - off)));
    }
    R_TRY(ncm::Register(cs, &id, &ph));
    registered = true;
    R_SUCCEED();
}

} // namespace

Result PatchNca(std::vector<u8>& nca, const nacp_patch::Patch& patch, bool& changed) {
    changed = false;
    R_UNLESS(nca.size() >= sizeof(nca::Header), Result_NcaBadMagic);
    keys::Keys keys;
    R_TRY(keys::parse_keys(keys, true));
    nca::Header hdr;
    R_TRY(nca::DecryptHeader(nca.data(), keys, hdr));
    R_UNLESS(hdr.content_type == nca::ContentType_Control, Result_NcaBadMagic);

    int idx = -1;
    for (int i = 0; i < hdr.GetSectionCount(); i++) {
        const auto& fsh = hdr.fs_header[i];
        if (fsh.fs_type == nca::FileSystemType_RomFS && fsh.hash_type == nca::HashType_HierarchicalIntegrity) {
            idx = i;
            break;
        }
    }
    R_UNLESS(idx >= 0, Result_NcaBadMagic);
    auto& fsh = hdr.fs_header[idx];
    const bool encrypted = fsh.encryption_type == nca::EncryptionType_AesCtr;
    // anything else (compressed, sparse, xts, patch sections, extra metadata hashes) is left alone.
    R_UNLESS(encrypted || fsh.encryption_type == nca::EncryptionType_None, Result_NcaBadMagic);
    R_UNLESS(fsh.compression_info.table_size == 0 && fsh.metadata_hash_type == 0, Result_NcaBadMagic);

    keys::KeyEntry title_key{};
    if (encrypted) {
        auto key_hdr = hdr; // GetDecryptedTitleKey decrypts the key area in place; hdr keeps it encrypted.
        R_TRY(nca::GetDecryptedTitleKey(key_hdr, keys, title_key));
    }

    const u64 sec_off = hdr.fs_table[idx].GetOffset();
    const u64 sec_size = hdr.fs_table[idx].GetSize();
    R_UNLESS(sec_off + sec_size <= nca.size(), Result_NcaBadMagic);
    u8* sec = nca.data() + sec_off;
    if (encrypted) {
        CryptSection(sec, sec_size, title_key.key, fsh.section_ctr, sec_off);
    }

    auto& ivfc = fsh.hash_data.integrity_meta_info;
    const u32 count = std::min<u32>(ivfc.info_level_hash.max_layers - 1, 6);
    R_UNLESS(count >= 2, Result_NcaBadMagic);
    std::vector<nacp_patch::IvfcLevel> levels;
    for (u32 i = 0; i < count; i++) {
        const auto& l = ivfc.info_level_hash.levels[i];
        levels.push_back({l.logical_offset, l.hash_data_size, l.block_size});
    }
    const auto& data_level = levels.back();
    R_UNLESS(data_level.offset + data_level.size <= sec_size, Result_NcaBadMagic);

    const std::span<const u8> romfs{sec + data_level.offset, data_level.size};
    const auto file = nacp_patch::FindRomfsFile(romfs, "control.nacp");
    R_UNLESS(file && file->second >= nacp_patch::NACP_SIZE && file->first + nacp_patch::NACP_SIZE <= data_level.size, Result_NcaBadMagic);

    const std::span<u8> section{sec, sec_size};
    bool master_exact = false;
    if (!nacp_patch::Rehash(section, levels, ivfc.master_hash, file->first, nacp_patch::NACP_SIZE, Sha256, true, master_exact)) {
        log_write("[CONTROL] ivfc layout not understood, not patching\n");
        R_THROW(Result_NcaBadMagic);
    }

    changed = nacp_patch::Apply({sec + data_level.offset + file->first, nacp_patch::NACP_SIZE}, patch);
    if (changed) {
        nacp_patch::Rehash(section, levels, ivfc.master_hash, file->first, nacp_patch::NACP_SIZE, Sha256, false, master_exact);
        Sha256(reinterpret_cast<const u8*>(&fsh), sizeof(fsh), hdr.fs_header_hash[idx].sha256);
    }
    if (encrypted) {
        CryptSection(sec, sec_size, title_key.key, fsh.section_ctr, sec_off);
    }
    if (changed) {
        crypto::cryptoAes128Xts(&hdr, nca.data(), keys.header_key, 0, 0x200, sizeof(hdr), true);
    }
    R_SUCCEED();
}

Result ReadState(u64 app_id, nacp_patch::State& out) {
    auto control = std::make_unique<NsApplicationControlData>();
    u64 size{};
    R_TRY(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control.get(), sizeof(*control), &size));
    R_UNLESS(size >= sizeof(NacpStruct), Result_NcaBadMagic);
    out = nacp_patch::ReadState({reinterpret_cast<const u8*>(&control->nacp), sizeof(NacpStruct)});
    R_SUCCEED();
}

Result PatchInstalled(u64 app_id, const nacp_patch::Patch& patch) {
    u32 found{};
    for (const auto storage_id : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
        NcmContentStorage cs{};
        NcmContentMetaDatabase db{};
        if (R_FAILED(ncmOpenContentStorage(&cs, storage_id))) {
            continue;
        }
        ON_SCOPE_EXIT(ncmContentStorageClose(&cs));
        if (R_FAILED(ncmOpenContentMetaDatabase(&db, storage_id))) {
            continue;
        }
        ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

        std::vector<NcmContentMetaKey> keys;
        R_TRY(ncm::ListAllKeys(&db, keys));
        for (const auto& key : keys) {
            if ((key.type != NcmContentMetaType_Application && key.type != NcmContentMetaType_Patch) || ncm::GetAppId(key) != app_id) {
                continue;
            }
            NcmContentId id{};
            if (R_FAILED(ncmContentMetaDatabaseGetContentIdByType(&db, &id, &key, NcmContentType_Control))) {
                continue;
            }
            std::vector<u8> nca;
            R_TRY(ReadContent(&cs, id, nca));
            bool changed{};
            R_TRY(PatchNca(nca, patch, changed));
            if (changed) {
                R_TRY(WriteContent(&cs, id, nca));
            }
            found++;
            log_write("[CONTROL] %016lX v%u control %s\n", key.id, key.version, changed ? "patched" : "already so");
        }
    }
    R_UNLESS(found, FsError_PathNotFound);
    if (ns::AppManager am; am) {
        ns::InvalidateApplicationControlCache(am.get(), app_id);
    }
    R_SUCCEED();
}

} // namespace sphaira::control_patch
