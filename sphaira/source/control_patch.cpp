#include "control_patch.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "fs.hpp"

#include <minIni.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
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

    const std::span<u8> romfs{sec + data_level.offset, data_level.size};
    const auto file = nacp_patch::FindRomfsFile(romfs, "control.nacp");
    R_UNLESS(file && file->second >= nacp_patch::NACP_SIZE && file->first + nacp_patch::NACP_SIZE <= data_level.size, Result_NcaBadMagic);

    // the whole image is checked and rehashed: an icon may change anywhere in it, and a control
    // RomFS is small.
    const std::span<u8> section{sec, sec_size};
    bool master_exact = false;
    if (!nacp_patch::Rehash(section, levels, ivfc.master_hash, 0, data_level.size, Sha256, true, master_exact)) {
        log_write("[CONTROL] ivfc layout not understood, not patching\n");
        R_THROW(Result_NcaBadMagic);
    }

    changed = nacp_patch::Apply({romfs.data() + file->first, nacp_patch::NACP_SIZE}, patch);
    if (patch.icon) {
        auto icons = nacp_patch::ListRomfsFiles(romfs);
        std::erase_if(icons, [](const auto& f){ return !nacp_patch::IsIconFile(f.name); });
        R_UNLESS(!icons.empty(), Result_NcaBadMagic);
        u64 room = icons.front().capacity;
        for (const auto& f : icons) {
            room = std::min(room, f.capacity);
        }
        const auto jpeg = patch.icon(room);
        if (jpeg.empty() || jpeg.size() > room) {
            log_write("[CONTROL] icon does not fit %lu bytes\n", room);
            R_THROW(Result_IconTooLarge);
        }
        for (const auto& f : icons) {
            R_UNLESS(nacp_patch::ReplaceRomfsFile(romfs, f, jpeg), Result_IconTooLarge);
        }
        log_write("[CONTROL] icon %zu bytes into %zu slots of %lu\n", jpeg.size(), icons.size(), room);
        changed = true;
    }
    if (changed) {
        nacp_patch::Rehash(section, levels, ivfc.master_hash, 0, data_level.size, Sha256, false, master_exact);
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

namespace {

static_assert(offsetof(NacpStruct, titles_data_format) == nacp_patch::OFF_TITLES_DATA_FORMAT);
static_assert(offsetof(NacpStruct, display_version) == nacp_patch::OFF_DISPLAY_VERSION);

// the control NCA as it was before the first name or icon change, one file per content id.
auto OriginalPath(const NcmContentId& id) -> fs::FsPath {
    char hex[33]{};
    for (int i = 0; i < 16; i++) {
        std::snprintf(hex + i * 2, 3, "%02x", id.c[i]);
    }
    return fs::FsPath{(paths::DATA_ROOT + "/control_original/" + hex + ".nca").c_str()};
}

// every installed Control NCA of the game: base and update, SD and NAND.
using ControlFn = std::function<Result(NcmContentStorage*, const NcmContentMetaKey&, const NcmContentId&)>;
Result ForEachControl(u64 app_id, const ControlFn& fn) {
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
            R_TRY(fn(&cs, key, id));
            found++;
        }
    }
    R_UNLESS(found, FsError_PathNotFound);
    R_SUCCEED();
}

// the HOME menu and ns read the new control data on their next look.
Result Invalidated(u64 app_id, Result rc) {
    if (ns::AppManager am; am) {
        ns::InvalidateApplicationControlCache(am.get(), app_id);
    }
    return rc;
}

} // namespace

bool HasOriginal(u64 app_id) {
    fs::FsNativeSd sd;
    bool has = false;
    std::ignore = ForEachControl(app_id, [&](auto*, const auto&, const NcmContentId& id) -> Result {
        has = has || sd.FileExists(OriginalPath(id));
        R_SUCCEED();
    });
    return has;
}

Result RestoreOriginal(u64 app_id) {
    // the restrictions set since then stay: they are applied to the original again.
    nacp_patch::State now{};
    R_TRY(ReadState(app_id, now));
    nacp_patch::Patch keep;
    keep.linked_account_required = now.linked_account_required;
    keep.screenshots_allowed = now.screenshots_allowed;
    keep.video_allowed = now.video_allowed;

    fs::FsNativeSd sd;
    return Invalidated(app_id, ForEachControl(app_id, [&](NcmContentStorage* cs, const NcmContentMetaKey& key, const NcmContentId& id) -> Result {
        const auto path = OriginalPath(id);
        std::vector<u8> nca;
        if (R_FAILED(sd.read_entire_file(path, nca))) {
            R_SUCCEED(); // never changed, or changed by an older Hub
        }
        bool changed{};
        R_TRY(PatchNca(nca, keep, changed));
        R_TRY(WriteContent(cs, id, nca));
        std::ignore = sd.DeleteFile(path);
        log_write("[CONTROL] %016lX v%u control restored\n", key.id, key.version);
        R_SUCCEED();
    }));
}

Result PatchInstalled(u64 app_id, const nacp_patch::Patch& patch) {
    fs::FsNativeSd sd;
    return Invalidated(app_id, ForEachControl(app_id, [&](NcmContentStorage* cs, const NcmContentMetaKey& key, const NcmContentId& id) -> Result {
        std::vector<u8> nca;
        R_TRY(ReadContent(cs, id, nca));
        const auto original = patch.EditsTitle() ? nca : std::vector<u8>{};
        bool changed{};
        R_TRY(PatchNca(nca, patch, changed));
        if (changed) {
            // kept before the first name or icon change, so "Restore original" has it.
            if (const auto path = OriginalPath(id); !original.empty() && !sd.FileExists(path)) {
                std::ignore = sd.CreateDirectoryRecursivelyWithPath(path);
                R_TRY(sd.write_entire_file(path, original));
            }
            R_TRY(WriteContent(cs, id, nca));
        }
        log_write("[CONTROL] %016lX v%u control %s\n", key.id, key.version, changed ? "patched" : "already so");
        R_SUCCEED();
    }));
}

namespace {
constexpr const char* SECTION = "install";
bool GetFlag(const char* key) { return ini_getbool(SECTION, key, 0, App::CONFIG_PATH); }
void SetFlag(const char* key, bool v) { ini_putl(SECTION, key, v, App::CONFIG_PATH); }
} // namespace

bool GetInstallNoLinkedAccount() { return GetFlag("patch_no_linked_account"); }
void SetInstallNoLinkedAccount(bool v) { SetFlag("patch_no_linked_account", v); }
bool GetInstallScreenshots() { return GetFlag("patch_screenshots"); }
void SetInstallScreenshots(bool v) { SetFlag("patch_screenshots", v); }
bool GetInstallVideo() { return GetFlag("patch_video"); }
void SetInstallVideo(bool v) { SetFlag("patch_video", v); }

auto InstallPatch() -> nacp_patch::Patch {
    nacp_patch::Patch p;
    if (GetInstallNoLinkedAccount()) p.linked_account_required = false;
    if (GetInstallScreenshots()) p.screenshots_allowed = true;
    if (GetInstallVideo()) p.video_allowed = true;
    return p;
}

} // namespace sphaira::control_patch
