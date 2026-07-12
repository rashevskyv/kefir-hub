#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "utils/devoptab.hpp"
#include "yati/nx/ncm.hpp"
#include "defines.hpp"

#include <switch.h>
#include <format>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::hats {

namespace detail {

// Format title ID as 16-character hex string (lowercase for atmosphere paths)
auto FormatTitleId(u64 title_id) -> std::string {
    return std::format("{:016X}", title_id);
}

// Format title ID as lowercase for file paths
auto FormatTitleIdLower(u64 title_id) -> std::string {
    return std::format("{:016x}", title_id);
}

auto GetBaseApplicationTitleId(u64 title_id) -> u64 {
    constexpr u64 update_title_id_suffix = 0x800;
    constexpr u64 title_id_content_suffix_mask = 0xFFF;

    if ((title_id & title_id_content_suffix_mask) == update_title_id_suffix) {
        return title_id & ~title_id_content_suffix_mask;
    }

    return title_id;
}

// Convert bytes to hex string (uppercase)
auto BytesToHex(const u8* data, size_t len) -> std::string {
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", data[i]);
        hex += buf;
    }
    return hex;
}

auto BytesToBuildId(const u8* data, size_t len) -> std::string {
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        const auto value = data[len - 1 - i];
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", value);
        hex += buf;
    }
    return hex;
}

auto NormalizeBuildId(std::string build_id) -> std::string {
    std::transform(build_id.begin(), build_id.end(), build_id.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return build_id;
}

auto ReverseBuildIdBytes(std::string build_id) -> std::string {
    build_id = NormalizeBuildId(std::move(build_id));
    if ((build_id.size() % 2) != 0) {
        return build_id;
    }

    std::string reversed;
    reversed.reserve(build_id.size());
    for (size_t i = build_id.size(); i > 0; i -= 2) {
        reversed.push_back(build_id[i - 2]);
        reversed.push_back(build_id[i - 1]);
    }
    return reversed;
}

auto IsValidBuildId(const std::string& build_id) -> bool {
    if (build_id.size() != 16) {
        return false;
    }

    bool all_zero = true;
    for (const auto c : build_id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        if (c != '0') {
            all_zero = false;
        }
    }

    return !all_zero;
}

// Case-insensitive string comparison
auto StringsEqualIgnoreCase(const std::string& a, const std::string& b) -> bool {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
        return std::tolower(c1) == std::tolower(c2);
    });
}

} // namespace detail

namespace {

void LogMountedDirectoryEntries(fs::FsStdio& mounted_fs, const char* path) {
    fs::Dir dir;
    auto rc = mounted_fs.OpenDirectory(path,
        FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize,
        &dir);
    if (R_FAILED(rc)) {
        log_write("[Cheats] Mounted NCA directory open failed for %s: %x\n", path, rc);
        return;
    }

    std::vector<FsDirectoryEntry> entries(32);
    s64 read_count = 0;
    rc = dir.Read(&read_count, entries.size(), entries.data());
    if (R_FAILED(rc)) {
        log_write("[Cheats] Mounted NCA directory read failed for %s: %x\n", path, rc);
        return;
    }

    log_write("[Cheats] Mounted NCA directory %s has %lld entr%s\n",
              path, static_cast<long long>(read_count), read_count == 1 ? "y" : "ies");
    for (s64 i = 0; i < read_count; i++) {
        log_write("[Cheats]   %s/%s\n", path, entries[i].name);
    }
}

auto TryGetBuildIdFromNsoWithStorage(u64 title_id, NcmStorageId storage_id, const char* path) -> std::string {
    FsCodeInfo code_info{};
    FsFileSystem fs{};
    Result rc = fsldrOpenCodeFileSystem(&code_info, title_id, storage_id, path, FsContentAttributes_None, &fs);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromNso: fsldrOpenCodeFileSystem failed for storage=%d path=%s: %x\n",
                  static_cast<int>(storage_id), path ? path : "(null)", rc);
        return "";
    }
    ON_SCOPE_EXIT(fsFsClose(&fs));

    FsFile file{};
    rc = fsFsOpenFile(&fs, "/main", FsOpenMode_Read, &file);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromNso: failed to open /main for storage=%d path=%s: %x\n",
                  static_cast<int>(storage_id), path ? path : "(null)", rc);
        return "";
    }
    ON_SCOPE_EXIT(fsFileClose(&file));

    u8 build_id_bytes[8]{};
    u64 bytes_read = 0;
    rc = fsFileRead(&file, 0x40, build_id_bytes, sizeof(build_id_bytes), FsReadOption_None, &bytes_read);
    if (R_FAILED(rc) || bytes_read != sizeof(build_id_bytes)) {
        log_write("[Cheats] GetBuildIdFromNso: failed to read /main build ID for storage=%d path=%s: %x (read=%lu)\n",
                  static_cast<int>(storage_id), path ? path : "(null)", rc, bytes_read);
        return "";
    }

    const auto build_id = detail::NormalizeBuildId(detail::BytesToBuildId(build_id_bytes, sizeof(build_id_bytes)));
    if (!detail::IsValidBuildId(build_id)) {
        log_write("[Cheats] GetBuildIdFromNso: invalid Build ID read from /main for storage=%d path=%s: %s\n",
                  static_cast<int>(storage_id), path ? path : "(null)", build_id.c_str());
        return "";
    }

    log_write("[Cheats] GetBuildIdFromNso: build ID = %s (storage=%d path=%s)\n",
              build_id.c_str(), static_cast<int>(storage_id), path ? path : "(null)");
    return build_id;
}

} // namespace

auto GetBuildIdFromInstalledNcaDetailed(u64 title_id) -> InstalledNcaLookupResult {
    InstalledNcaLookupResult result;
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: nsInitialize failed for %016lx: %x\n", title_id, rc);
        result.failure_reason = InstalledNcaFailureReason::Unavailable;
        return result;
    }
    ON_SCOPE_EXIT(nsExit());

    s32 count = 0;
    if (R_FAILED(nsCountApplicationContentMeta(title_id, &count)) || count <= 0) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: no meta entries for title %016lx\n", title_id);
        result.failure_reason = InstalledNcaFailureReason::NotInstalled;
        return result;
    }

    std::vector<NsApplicationContentMetaStatus> entries(count);
    s32 entries_read = 0;
    if (R_FAILED(nsListApplicationContentMetaStatus(title_id, 0, entries.data(), entries.size(), &entries_read)) || entries_read <= 0) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to list meta entries for title %016lx\n", title_id);
        result.failure_reason = InstalledNcaFailureReason::Unavailable;
        return result;
    }
    entries.resize(entries_read);

    const auto is_supported_storage = [](u8 storage_id) {
        return storage_id == NcmStorageId_SdCard ||
               storage_id == NcmStorageId_BuiltInUser ||
               storage_id == NcmStorageId_GameCard;
    };

    const NsApplicationContentMetaStatus* best_status = nullptr;
    for (const auto& entry : entries) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: candidate meta app=%016lx version=%u storage=%u type=%u\n",
                  entry.application_id, entry.version, entry.storageID, entry.meta_type);
        if (!is_supported_storage(entry.storageID)) {
            continue;
        }

        if (!best_status || entry.version > best_status->version) {
            best_status = &entry;
        }
    }

    if (!best_status) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: no supported storage entries for title %016lx\n",
                  title_id);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }

    log_write("[Cheats] GetBuildIdFromInstalledNca: selected meta app=%016lx version=%u storage=%u type=%u\n",
              best_status->application_id, best_status->version, best_status->storageID, best_status->meta_type);

    NcmContentMetaDatabase db{};
    rc = ncmOpenContentMetaDatabase(&db, static_cast<NcmStorageId>(best_status->storageID));
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to open metadata DB for title %016lx: %x\n", title_id, rc);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }
    ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

    NcmContentStorage cs{};
    rc = ncmOpenContentStorage(&cs, static_cast<NcmStorageId>(best_status->storageID));
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to open content storage for title %016lx: %x\n", title_id, rc);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }
    ON_SCOPE_EXIT(ncmContentStorageClose(&cs));

    NcmContentMetaKey key{};
    rc = ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, best_status->application_id);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to resolve latest content meta key for %016lx: %x\n", title_id, rc);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }

    NcmContentId content_id{};
    rc = ncmContentMetaDatabaseGetContentIdByType(&db, &content_id, &key, NcmContentType_Program);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to resolve Program content ID for %016lx: %x\n", title_id, rc);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }

    fs::FsPath mount_path;
    rc = devoptab::MountNcaNcm(&cs, &content_id, mount_path);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to mount Program NCA for %016lx: %x\n", title_id, rc);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }
    ON_SCOPE_EXIT(devoptab::UmountNeworkDevice(mount_path));

    fs::FsStdio mounted_fs{true, mount_path};
    fs::File file;
    const char* opened_path = nullptr;
    for (const auto* candidate_path : {"/main", "/exeFS/main"}) {
        rc = mounted_fs.OpenFile(candidate_path, FsOpenMode_Read, &file);
        if (R_SUCCEEDED(rc)) {
            opened_path = candidate_path;
            break;
        }
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to open %s for %016lx: %x\n",
                  candidate_path, title_id, rc);
    }
    if (!opened_path) {
        LogMountedDirectoryEntries(mounted_fs, "/");
        LogMountedDirectoryEntries(mounted_fs, "/exeFS");
        LogMountedDirectoryEntries(mounted_fs, "/RomFS");
        LogMountedDirectoryEntries(mounted_fs, "/Logo");
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }

    u8 module_id[0x20]{};
    u64 bytes_read{};
    rc = file.Read(0x40, module_id, sizeof(module_id), FsReadOption_None, &bytes_read);
    if (R_FAILED(rc) || bytes_read < 8) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: failed to read ModuleId from %s for %016lx: %x (read=%lu)\n",
                  opened_path, title_id, rc, bytes_read);
        result.failure_reason = InstalledNcaFailureReason::ContentMissing;
        return result;
    }

    const auto build_id = detail::NormalizeBuildId(detail::BytesToHex(module_id, 8));
    if (!detail::IsValidBuildId(build_id)) {
        log_write("[Cheats] GetBuildIdFromInstalledNca: invalid Build ID for %016lx: %s\n", title_id, build_id.c_str());
        result.failure_reason = InstalledNcaFailureReason::Unavailable;
        return result;
    }

    log_write("[Cheats] GetBuildIdFromInstalledNca: build ID = %s for title %016lx via %s\n",
              build_id.c_str(), title_id, opened_path);
    result.build_id = build_id;
    return result;
}

auto GetBuildIdFromInstalledNca(u64 title_id) -> std::string {
    return GetBuildIdFromInstalledNcaDetailed(title_id).build_id;
}

auto ReadBuildIdFromProgramContent(NcmContentStorage& cs, const NcmContentId& content_id, u64 title_id, const char* source) -> std::string {
    fs::FsPath mount_path;
    Result rc = devoptab::MountNcaNcm(&cs, &content_id, mount_path);
    if (R_FAILED(rc)) {
        log_write("[Cheats] %s: failed to mount Program NCA for %016lx: %x\n", source, title_id, rc);
        return "";
    }
    ON_SCOPE_EXIT(devoptab::UmountNeworkDevice(mount_path));

    fs::FsStdio mounted_fs{true, mount_path};
    fs::File file;
    const char* opened_path = nullptr;
    for (const auto* candidate_path : {"/main", "/exeFS/main"}) {
        rc = mounted_fs.OpenFile(candidate_path, FsOpenMode_Read, &file);
        if (R_SUCCEEDED(rc)) {
            opened_path = candidate_path;
            break;
        }
        log_write("[Cheats] %s: failed to open %s for %016lx: %x\n",
                  source, candidate_path, title_id, rc);
    }
    if (!opened_path) {
        LogMountedDirectoryEntries(mounted_fs, "/");
        LogMountedDirectoryEntries(mounted_fs, "/exeFS");
        return "";
    }

    u8 module_id[0x20]{};
    u64 bytes_read{};
    rc = file.Read(0x40, module_id, sizeof(module_id), FsReadOption_None, &bytes_read);
    if (R_FAILED(rc) || bytes_read < 8) {
        log_write("[Cheats] %s: failed to read ModuleId from %s for %016lx: %x (read=%lu)\n",
                  source, opened_path, title_id, rc, bytes_read);
        return "";
    }

    const auto build_id = detail::NormalizeBuildId(detail::BytesToHex(module_id, 8));
    if (!detail::IsValidBuildId(build_id)) {
        log_write("[Cheats] %s: invalid Build ID for %016lx: %s\n", source, title_id, build_id.c_str());
        return "";
    }

    log_write("[Cheats] %s: build ID = %s for title %016lx via %s\n",
              source, build_id.c_str(), title_id, opened_path);
    return build_id;
}

auto GetBuildIdFromGameCardNca(u64 title_id) -> std::string {
    NcmContentMetaDatabase db{};
    Result rc = ncmOpenContentMetaDatabase(&db, NcmStorageId_GameCard);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromGameCardNca: failed to open game card metadata DB for %016lx: %x\n", title_id, rc);
        return "";
    }
    ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

    NcmContentStorage cs{};
    rc = ncmOpenContentStorage(&cs, NcmStorageId_GameCard);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromGameCardNca: failed to open game card content storage for %016lx: %x\n", title_id, rc);
        return "";
    }
    ON_SCOPE_EXIT(ncmContentStorageClose(&cs));

    std::vector<NcmContentMetaKey> keys(16);
    s32 total = 0;
    s32 written = 0;
    rc = ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), keys.size(),
        NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromGameCardNca: failed to list game card metadata for %016lx: %x\n", title_id, rc);
        return "";
    }

    if (total > written && total > static_cast<s32>(keys.size())) {
        keys.resize(total);
        rc = ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), keys.size(),
            NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
        if (R_FAILED(rc)) {
            log_write("[Cheats] GetBuildIdFromGameCardNca: failed to list all game card metadata for %016lx: %x\n", title_id, rc);
            return "";
        }
    }
    keys.resize(written);

    std::sort(keys.begin(), keys.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type == NcmContentMetaType_Patch;
        }
        return lhs.version > rhs.version;
    });

    for (const auto& key : keys) {
        if (key.type != NcmContentMetaType_Application && key.type != NcmContentMetaType_Patch) {
            continue;
        }
        if (detail::GetBaseApplicationTitleId(ncm::GetAppId(key)) != title_id) {
            continue;
        }

        log_write("[Cheats] GetBuildIdFromGameCardNca: candidate key id=%016lx type=%u version=%u\n",
                  key.id, key.type, key.version);

        NcmContentId content_id{};
        rc = ncmContentMetaDatabaseGetContentIdByType(&db, &content_id, &key, NcmContentType_Program);
        if (R_FAILED(rc)) {
            log_write("[Cheats] GetBuildIdFromGameCardNca: failed to resolve Program content ID for %016lx key=%016lx: %x\n",
                      title_id, key.id, rc);
            continue;
        }

        if (auto build_id = ReadBuildIdFromProgramContent(cs, content_id, title_id, "GetBuildIdFromGameCardNca");
            !build_id.empty()) {
            return build_id;
        }
    }

    log_write("[Cheats] GetBuildIdFromGameCardNca: no readable Program NCA for %016lx\n", title_id);
    return "";
}

auto GetBuildIdFromNso(u64 title_id) -> std::string {
    std::vector<NcmStorageId> storage_ids;
    auto add_storage_id = [&storage_ids](NcmStorageId storage_id) {
        if (std::find(storage_ids.begin(), storage_ids.end(), storage_id) == storage_ids.end()) {
            storage_ids.push_back(storage_id);
        }
    };

    s32 count = 0;
    if (R_SUCCEEDED(nsCountApplicationContentMeta(title_id, &count)) && count > 0) {
        std::vector<NsApplicationContentMetaStatus> statuses(count);
        s32 out = 0;
        if (R_SUCCEEDED(nsListApplicationContentMetaStatus(title_id, 0, statuses.data(), statuses.size(), &out)) && out > 0) {
            statuses.resize(out);
            for (const auto& status : statuses) {
                add_storage_id(static_cast<NcmStorageId>(status.storageID));
                log_write("[Cheats] GetBuildIdFromNso: discovered storage_id=%d for title %016lx\n",
                          static_cast<int>(status.storageID), title_id);
            }
        }
    }

    add_storage_id(NcmStorageId_BuiltInUser);
    add_storage_id(NcmStorageId_SdCard);
    add_storage_id(NcmStorageId_GameCard);
    add_storage_id(NcmStorageId_Any);
    add_storage_id(NcmStorageId_None);

    Result rc = fsldrInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] GetBuildIdFromNso: fsldrInitialize failed: %x\n", rc);
        return "";
    }
    ON_SCOPE_EXIT(fsldrExit());

    for (const auto storage_id : storage_ids) {
        if (auto build_id = TryGetBuildIdFromNsoWithStorage(title_id, storage_id, nullptr); !build_id.empty()) {
            return build_id;
        }
        if (auto build_id = TryGetBuildIdFromNsoWithStorage(title_id, storage_id, ""); !build_id.empty()) {
            return build_id;
        }
    }

    log_write("[Cheats] GetBuildIdFromNso: exhausted all storage/path combinations for title %016lx\n", title_id);
    return "";
}

auto HasProdKeys() -> bool {
    fs::FsNativeSd fs;
    return fs.FileExists("/switch/prod.keys");
}

auto HasApplicationContentMeta(u64 title_id) -> bool {
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] HasApplicationContentMeta: nsInitialize failed for %016lx: %x\n", title_id, rc);
        return false;
    }
    ON_SCOPE_EXIT(nsExit());

    s32 count = 0;
    rc = nsCountApplicationContentMeta(title_id, &count);
    if (R_FAILED(rc)) {
        log_write("[Cheats] HasApplicationContentMeta: nsCountApplicationContentMeta failed for %016lx: %x\n", title_id, rc);
        return false;
    }

    return count > 0;
}

auto LookupBuildIdForCheats(u64 title_id, bool allow_nso_fallback) -> BuildIdLookupResult {
    BuildIdLookupResult result;

    result.build_id = GetBuildIdFromDmnt(title_id);
    if (!result.build_id.empty()) {
        result.build_id = detail::NormalizeBuildId(result.build_id);
        result.source = "dmnt";
        return result;
    }

    const bool has_prod_keys = HasProdKeys();
    bool installed_content_missing = false;
    if (!has_prod_keys) {
        log_write("[Cheats] LookupBuildIdForCheats: /switch/prod.keys not found\n");
    } else {
        result.build_id = GetBuildIdFromGameCardNca(title_id);
        if (!result.build_id.empty()) {
            result.source = "gamecard-nca";
            return result;
        }

        const auto installed_nca = GetBuildIdFromInstalledNcaDetailed(title_id);
        result.build_id = installed_nca.build_id;
        if (!result.build_id.empty()) {
            result.source = "installed-nca";
            return result;
        }

        if (installed_nca.failure_reason == InstalledNcaFailureReason::ContentMissing) {
            log_write("[Cheats] LookupBuildIdForCheats: installed Program NCA content missing for %016lx\n", title_id);
            installed_content_missing = true;
        }
    }

    if (allow_nso_fallback) {
        result.build_id = GetBuildIdFromNso(title_id);
        if (!result.build_id.empty()) {
            result.build_id = detail::NormalizeBuildId(result.build_id);
            result.source = "nso";
            return result;
        }
    }

    if (!has_prod_keys) {
        result.failure_reason = BuildIdFailureReason::ProdKeysMissing;
        return result;
    }

    if (!HasApplicationContentMeta(title_id)) {
        result.failure_reason = BuildIdFailureReason::GameNotFound;
        return result;
    }

    if (installed_content_missing) {
        result.failure_reason = BuildIdFailureReason::GameNotFound;
        return result;
    }

    result.failure_reason = BuildIdFailureReason::ExactBuildIdUnavailable;
    return result;
}

} // namespace sphaira::ui::menu::hats
