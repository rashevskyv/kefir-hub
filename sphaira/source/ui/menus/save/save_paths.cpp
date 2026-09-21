#include "ui/menus/save/save_paths.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "path_util.hpp"
#include "minizip_helper.hpp"
#include "i18n.hpp"
#include <minizip/unzip.h>
#include <minIni.h>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <utility>
#include <set>

namespace sphaira::ui::menu::save {

constexpr const char* BACKUP_PATHS_INI_SECTION = "save_backup_paths";


constexpr std::array<u8, 7> SAVE_TYPE_VALUES{
    FsSaveDataType_System,
    FsSaveDataType_Account,
    FsSaveDataType_Bcat,
    FsSaveDataType_Device,
    FsSaveDataType_Temporary,
    FsSaveDataType_Cache,
    FsSaveDataType_SystemBcat,
};

auto GetSaveFolder(u8 data_type) -> fs::FsPath {
    switch (data_type) {
        case FsSaveDataType_System:     return "Save System";
        case FsSaveDataType_SystemBcat: return "Save System BCAT";
        case FsSaveDataType_Account:    return "Save";
        case FsSaveDataType_Bcat:       return "Save BCAT";
        case FsSaveDataType_Device:     return "Save Device";
        case FsSaveDataType_Temporary:  return "Save Temporary";
        case FsSaveDataType_Cache:      return "Save Cache";
    }
    std::unreachable();
}

auto GetSaveFolder(const Entry& e) -> fs::FsPath {
    return GetSaveFolder(e.save_data_type);
}

auto GetSaveTypeSubdir(u8 data_type) -> fs::FsPath {
    switch (data_type) {
        case FsSaveDataType_Account:   return "Account";
        case FsSaveDataType_Bcat:      return "BCAT";
        case FsSaveDataType_Device:    return "Device";
        case FsSaveDataType_Temporary: return "Temporary";
        case FsSaveDataType_Cache:     return "Cache";
    }
    std::unreachable();
}

auto GetDbiTypeLetter(u8 data_type) -> char {
    switch (data_type) {
        case FsSaveDataType_Account:   return 'A';
        case FsSaveDataType_Bcat:      return 'B';
        case FsSaveDataType_Device:    return 'D';
        case FsSaveDataType_Temporary: return 'T';
        case FsSaveDataType_Cache:     return 'C';
    }
    return '?';
}

auto ParseDbiTypeLetter(char c) -> u8 {
    switch (c) {
        case 'A': case 'a': return FsSaveDataType_Account;
        case 'B': case 'b': return FsSaveDataType_Bcat;
        case 'D': case 'd': return FsSaveDataType_Device;
        case 'T': case 't': return FsSaveDataType_Temporary;
        case 'C': case 'c': return FsSaveDataType_Cache;
        default: return 0xFF;
    }
}

auto ParseDbiBackupIndex(std::string_view name) -> u16 {
    if (!name.ends_with(".zip")) {
        return 0;
    }
    name.remove_suffix(4);
    const auto last_under = name.rfind('_');
    if (last_under == name.npos || last_under + 1 >= name.size()) {
        return 0;
    }
    u32 idx = 0;
    for (size_t i = last_under + 1; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        idx = idx * 10 + (name[i] - '0');
    }
    return static_cast<u16>(idx);
}

auto ParseHex16(std::string_view str) -> u64 {
    if (str.size() != 16) {
        return 0;
    }
    u64 id = 0;
    for (size_t i = 0; i < 16; i++) {
        const char c = str[i];
        int nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
        else return 0;
        id = (id << 4) | static_cast<u64>(nibble);
    }
    return id;
}

auto IsHex16(std::string_view str) -> bool {
    return str.size() == 16 && ParseHex16(str) != 0;
}

auto PosixToTimestamp(u64 posix_sec) -> u64 {
    if (!posix_sec) {
        return 0;
    }
    const time_t t = static_cast<time_t>(posix_sec);
    const auto tm = std::localtime(&t);
    if (!tm) {
        return 0;
    }
    return (u64)(tm->tm_year + 1900) * 10000000000ULL +
           (u64)(tm->tm_mon + 1) * 100000000ULL +
           (u64)(tm->tm_mday) * 1000000ULL +
           (u64)(tm->tm_hour) * 10000ULL +
           (u64)(tm->tm_min) * 100ULL +
           (u64)(tm->tm_sec);
}

auto ParseDbiBackupNameTimestamp(std::string_view name) -> u64 {
    if (name.size() < 34 || name[16] != '_' || name[18] != '_' || name[33] != '_') {
        return 0;
    }

    u64 ts{};
    for (size_t i = 19; i < 33; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        ts = ts * 10 + (name[i] - '0');
    }
    return ts;
}

auto ParseBackupNameTimestamp(std::string_view name) -> u64 {
    if (const auto ts = ParseDbiBackupNameTimestamp(name)) {
        return ts;
    }

    const auto at_pos = name.find(" @ ");
    if (at_pos != name.npos && at_pos >= 10 && at_pos + 11 <= name.size()) {
        u32 year, mon, day, hour, min, sec;
        if (6 == std::sscanf(name.data() + at_pos - 10, "%4u.%2u.%2u @ %2u.%2u.%2u", &year, &mon, &day, &hour, &min, &sec)) {
            return (u64)year * 10000000000ULL + (u64)mon * 100000000ULL + (u64)day * 1000000ULL
                + (u64)hour * 10000ULL + (u64)min * 100ULL + sec;
        }
    }

    for (size_t i = 0; i + 14 <= name.size(); i++) {
        u32 year, mon, day, hour, min, sec;
        if ((i + 15 <= name.size() && 6 == std::sscanf(name.data() + i, "%4u%2u%2u_%2u%2u%2u", &year, &mon, &day, &hour, &min, &sec)) ||
            (6 == std::sscanf(name.data() + i, "%4u%2u%2u%2u%2u%2u", &year, &mon, &day, &hour, &min, &sec))) {
            if (year >= 2000 && year <= 2099 && mon >= 1 && mon <= 12 && day >= 1 && day <= 31 && hour <= 23 && min <= 59 && sec <= 59) {
                return (u64)year * 10000000000ULL + (u64)mon * 100000000ULL + (u64)day * 1000000ULL
                    + (u64)hour * 10000ULL + (u64)min * 100ULL + sec;
            }
        }
    }

    return 0;
}

auto ParseDbiBackupAppId(std::string_view name) -> u64 {
    // must look like "<16 hex>_<letter>_..." (matches ParseDbiBackupNameTimestamp).
    if (name.size() < 19 || name[16] != '_' || name[18] != '_') {
        return 0;
    }

    u64 id{};
    for (size_t i = 0; i < 16; i++) {
        const char c = name[i];
        int nibble;
        if (c >= '0' && c <= '9') {
            nibble = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            nibble = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            nibble = c - 'A' + 10;
        } else {
            return 0;
        }
        id = (id << 4) | static_cast<u64>(nibble);
    }
    return id;
}

auto GetSaveTypeLabel(u8 data_type) -> const char* {
    switch (data_type) {
        case FsSaveDataType_System:     return "System";
        case FsSaveDataType_Account:    return "Account";
        case FsSaveDataType_Bcat:       return "BCAT";
        case FsSaveDataType_Device:     return "Device";
        case FsSaveDataType_Temporary:  return "Temporary";
        case FsSaveDataType_Cache:      return "Cache";
        case FsSaveDataType_SystemBcat: return "System BCAT";
    }
    return "Unknown";
}

auto SaveTypeIndex(u8 data_type) -> size_t {
    for (size_t i = 0; i < SAVE_TYPE_VALUES.size(); i++) {
        if (SAVE_TYPE_VALUES[i] == data_type) {
            return i;
        }
    }
    return 0;
}

auto SaveEntryKey(const FsSaveDataInfo& e) -> std::string {
    char key[0x80];
    std::snprintf(key, sizeof(key), "%u:%u:%016lX:%016lX:%016lX:%016lX:%u:%u",
        e.save_data_space_id, e.save_data_type, e.application_id,
        e.system_save_data_id, e.uid.uid[0], e.uid.uid[1],
        e.save_data_rank, e.save_data_index);
    return key;
}

constexpr std::array<FsSaveDataSpaceId, 7> CONCRETE_SAVE_DATA_SPACES{
    FsSaveDataSpaceId_System,
    FsSaveDataSpaceId_User,
    FsSaveDataSpaceId_SdSystem,
    FsSaveDataSpaceId_Temporary,
    FsSaveDataSpaceId_SdUser,
    FsSaveDataSpaceId_ProperSystem,
    FsSaveDataSpaceId_SafeMode,
};

auto DiscoverSaveDataInfo(const AccountUid* uid_filter, const std::optional<u8>& type_filter) -> std::vector<FsSaveDataInfo> {
    std::vector<FsSaveDataInfo> out;
    std::set<std::string> seen_keys;

    for (const auto space : CONCRETE_SAVE_DATA_SPACES) {
        FsSaveDataInfoReader reader;
        const auto open_rc = fsOpenSaveDataInfoReader(&reader, space);
        if (R_FAILED(open_rc)) {
            log_write("[SAVE] fsOpenSaveDataInfoReader failed for space %d: 0x%x\n", static_cast<int>(space), open_rc);
            continue;
        }

        ON_SCOPE_EXIT(fsSaveDataInfoReaderClose(&reader));

        std::vector<FsSaveDataInfo> staged;
        std::vector<FsSaveDataInfo> chunk(256);
        bool read_failed = false;

        while (true) {
            s64 count = 0;
            const auto read_rc = fsSaveDataInfoReaderRead(&reader, chunk.data(), chunk.size(), &count);
            if (R_FAILED(read_rc)) {
                log_write("[SAVE] fsSaveDataInfoReaderRead failed for space %d: 0x%x\n", static_cast<int>(space), read_rc);
                read_failed = true;
                break;
            }
            if (count <= 0) {
                break;
            }
            staged.insert(staged.end(), chunk.begin(), chunk.begin() + count);
        }

        if (read_failed) {
            continue;
        }

        for (const auto& info : staged) {
            if (type_filter.has_value() && info.save_data_type != *type_filter) {
                continue;
            }
            if (uid_filter != nullptr && std::memcmp(&info.uid, uid_filter, sizeof(AccountUid)) != 0) {
                continue;
            }

            const auto key = SaveEntryKey(info);
            if (seen_keys.insert(key).second) {
                out.emplace_back(info);
            }
        }
    }

    return out;
}

auto IsSystemLikeSave(u8 data_type) -> bool {
    return data_type == FsSaveDataType_System || data_type == FsSaveDataType_SystemBcat;
}

auto DisplayEntryKey(const Entry& e) -> std::string {
    if (e.is_backup) {
        return BackupGroupKey(e);
    }
    char key[0x80];
    if (IsSystemLikeSave(e.save_data_type)) {
        std::snprintf(key, sizeof(key), "system:%u:%016lX", e.save_data_type, e.system_save_data_id);
    } else {
        std::snprintf(key, sizeof(key), "app:%016lX", e.application_id);
    }
    return key;
}

auto BuildSaveName(const Entry& e) -> fs::FsPath {
    fs::FsPath name_buf = e.GetName();
    title::utilsReplaceIllegalCharacters(name_buf, true);
    return name_buf;
}

auto BuildSavePathName(const Entry& e, bool force_id_path) -> fs::FsPath {
    fs::FsPath name;
    if (e.save_data_type == FsSaveDataType_System || e.save_data_type == FsSaveDataType_SystemBcat) {
        std::snprintf(name, sizeof(name), "%016lX", e.system_save_data_id);
    } else if (force_id_path || !strcasecmp(e.GetName(), "corrupted")) {
        std::snprintf(name, sizeof(name), "%016lX", e.application_id);
    } else {
        name = BuildSaveName(e);
    }

    return name;
}

auto BuildSaveBasePathLegacy(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath {
    return fs::AppendPath(fs::AppendPath(backup_root, GetSaveFolder(e)), BuildSavePathName(e, force_id_path));
}

auto BuildSaveBasePath(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath {
    if (IsSystemLikeSave(e.save_data_type)) {
        return BuildSaveBasePathLegacy(e, force_id_path, backup_root);
    }

    return fs::AppendPath(fs::AppendPath(backup_root, BuildSavePathName(e, force_id_path)), GetSaveTypeSubdir(e.save_data_type));
}

auto BuildDbiGameFolderName(const Entry& e) -> fs::FsPath {
    fs::FsPath out{};
    size_t len{};

    if (strcasecmp(e.GetName(), "corrupted")) {
        for (const char* p = e.GetName(); *p && len < sizeof(out) - 1; p++) {
            const char c = *p;
            const bool keep = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ';
            if (keep && !(len == 0 && c == ' ')) {
                out.s[len++] = c;
            }
        }

        while (len && out.s[len - 1] == ' ') {
            len--;
        }
        out.s[len] = '\0';
    }

    if (!len) {
        std::snprintf(out, sizeof(out), "%016lX", e.application_id);
    }

    return out;
}

auto BuildDbiSavePath(const Entry& e, const struct tm& tm, const fs::FsPath& base) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s/%04d%02d%02d/%016lX_%c_%04d%02d%02d%02d%02d%02d_%u.zip",
        base.s, BuildDbiGameFolderName(e).s,
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        e.application_id, GetDbiTypeLetter(e.save_data_type),
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
        (u32)e.save_data_index);
    return path;
}

auto IsDbiBackupName(const Entry& e, const char* name) -> bool {
    char prefix[0x20];
    std::snprintf(prefix, sizeof(prefix), "%016lX_%c_", e.application_id, GetDbiTypeLetter(e.save_data_type));

    if (strncasecmp(name, prefix, std::strlen(prefix))) {
        return false;
    }

    const auto len = std::strlen(name);
    return len > 4 && !strcasecmp(name + len - 4, ".zip");
}

inline auto ReadU8(const u8* p) -> u8 {
    return p[0];
}
inline auto ReadU16LE(const u8* p) -> u16 {
    return static_cast<u16>(p[0]) |
          (static_cast<u16>(p[1]) << 8);
}
inline auto ReadU32LE(const u8* p) -> u32 {
    return static_cast<u32>(p[0]) |
          (static_cast<u32>(p[1]) << 8) |
          (static_cast<u32>(p[2]) << 16) |
          (static_cast<u32>(p[3]) << 24);
}
inline auto ReadU64LE(const u8* p) -> u64 {
    return static_cast<u64>(p[0]) |
          (static_cast<u64>(p[1]) << 8) |
          (static_cast<u64>(p[2]) << 16) |
          (static_cast<u64>(p[3]) << 24) |
          (static_cast<u64>(p[4]) << 32) |
          (static_cast<u64>(p[5]) << 40) |
          (static_cast<u64>(p[6]) << 48) |
          (static_cast<u64>(p[7]) << 56);
}
inline auto ReadS64LE(const u8* p) -> s64 {
    return static_cast<s64>(ReadU64LE(p));
}

struct DecodedSaveMetaInternal {
    u64 application_id{};
    AccountUid uid{};
    u64 system_save_data_id{};
    u8 save_data_type{0xFF};
    u8 save_data_rank{};
    u16 save_data_index{};
    u64 owner_id{};
    u64 timestamp{};
    u32 flags{};
    u32 unk_x54{};
    s64 data_size{};
    s64 journal_size{};
    u64 commit_id{};
    u64 raw_size{};
    std::optional<u8> source_space{};
};

inline auto ValidateDecodedSaveMeta(const DecodedSaveMetaInternal& m, bool is_86_layout) -> bool {
    // type 0..6
    if (m.save_data_type > FsSaveDataType_SystemBcat) {
        return false;
    }
    // rank 0..1
    if (m.save_data_rank > FsSaveDataRank_Secondary) {
        return false;
    }
    // data/journal signed s64 >= 0
    if (m.data_size < 0 || m.journal_size < 0) {
        return false;
    }

    if (m.save_data_type == FsSaveDataType_Account) {
        // Account: nonzero app, systemID 0, full 128-bit UID nonzero
        if (m.application_id == 0) {
            return false;
        }
        if (m.system_save_data_id != 0) {
            return false;
        }
        if (m.uid.uid[0] == 0 && m.uid.uid[1] == 0) {
            return false;
        }
    } else if (m.save_data_type == FsSaveDataType_System || m.save_data_type == FsSaveDataType_SystemBcat) {
        // System/SystemBcat: nonzero systemID
        if (m.system_save_data_id == 0) {
            return false;
        }
    } else {
        // Other types: nonzero app
        if (m.application_id == 0) {
            return false;
        }
    }

    // source space 86 allow 0,1,2,3,4,100,101; reject 255/All/unknown
    if (is_86_layout) {
        if (!m.source_space.has_value()) {
            return false;
        }
        const auto sp = *m.source_space;
        const bool valid_space = (sp == FsSaveDataSpaceId_System ||
                                  sp == FsSaveDataSpaceId_User ||
                                  sp == FsSaveDataSpaceId_SdSystem ||
                                  sp == FsSaveDataSpaceId_Temporary ||
                                  sp == FsSaveDataSpaceId_SdUser ||
                                  sp == FsSaveDataSpaceId_ProperSystem ||
                                  sp == FsSaveDataSpaceId_SafeMode);
        if (!valid_space) {
            return false;
        }
    }

    return true;
}

inline auto DecodeJksv85(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    const u32 magic = ReadU32LE(p + 0);
    if (magic != JKSV_SAVE_META_MAGIC) {
        return false;
    }
    const u8 revision = ReadU8(p + 4);
    if (revision != JKSV_SAVE_META_REVISION) {
        return false;
    }
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 5);
    out.uid.uid[0] = ReadU64LE(p + 13);
    out.uid.uid[1] = ReadU64LE(p + 21);
    out.system_save_data_id = ReadU64LE(p + 29);
    out.save_data_type = ReadU8(p + 37);
    out.save_data_rank = ReadU8(p + 38);
    out.save_data_index = ReadU16LE(p + 39);
    out.owner_id = ReadU64LE(p + 41);
    out.timestamp = ReadU64LE(p + 49);
    out.flags = ReadU32LE(p + 57);
    out.data_size = ReadS64LE(p + 61);
    out.journal_size = ReadS64LE(p + 69);
    out.commit_id = ReadU64LE(p + 77);
    out.source_space = std::nullopt;
    out.raw_size = 0;
    out.unk_x54 = 0;

    return ValidateDecodedSaveMeta(out, false);
}

inline auto DecodeJksvTail86(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    if (!DecodeJksv85(p, out)) {
        return false;
    }
    out.source_space = ReadU8(p + 85);
    return ValidateDecodedSaveMeta(out, true);
}

inline auto DecodeJksvMiddle86(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    const u32 magic = ReadU32LE(p + 0);
    if (magic != JKSV_SAVE_META_MAGIC) {
        return false;
    }
    const u8 revision = ReadU8(p + 4);
    if (revision != JKSV_SAVE_META_REVISION) {
        return false;
    }
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 5);
    out.uid.uid[0] = ReadU64LE(p + 13);
    out.uid.uid[1] = ReadU64LE(p + 21);
    out.system_save_data_id = ReadU64LE(p + 29);
    out.save_data_type = ReadU8(p + 37);
    out.save_data_rank = ReadU8(p + 38);
    out.save_data_index = ReadU16LE(p + 39);
    out.source_space = ReadU8(p + 41);
    out.owner_id = ReadU64LE(p + 42);
    out.timestamp = ReadU64LE(p + 50);
    out.flags = ReadU32LE(p + 58);
    out.data_size = ReadS64LE(p + 62);
    out.journal_size = ReadS64LE(p + 70);
    out.commit_id = ReadU64LE(p + 78);
    out.raw_size = 0;
    out.unk_x54 = 0;

    return ValidateDecodedSaveMeta(out, true);
}

inline auto CompareCommonSourceFields(const DecodedSaveMetaInternal& a, const DecodedSaveMetaInternal& b) -> bool {
    return (a.application_id == b.application_id) &&
           (a.uid.uid[0] == b.uid.uid[0] && a.uid.uid[1] == b.uid.uid[1]) &&
           (a.system_save_data_id == b.system_save_data_id) &&
           (a.save_data_type == b.save_data_type) &&
           (a.save_data_rank == b.save_data_rank) &&
           (a.save_data_index == b.save_data_index) &&
           (a.owner_id == b.owner_id) &&
           (a.timestamp == b.timestamp) &&
           (a.flags == b.flags) &&
           (a.data_size == b.data_size) &&
           (a.journal_size == b.journal_size) &&
           (a.commit_id == b.commit_id);
}

inline auto DecodeJksv86WithAmbiguityCheck(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    DecodedSaveMetaInternal tail{};
    DecodedSaveMetaInternal mid{};
    const bool tail_valid = DecodeJksvTail86(p, tail);
    const bool mid_valid = DecodeJksvMiddle86(p, mid);

    if (!tail_valid && !mid_valid) {
        return false;
    }
    if (tail_valid && !mid_valid) {
        out = tail;
        return true;
    }
    if (!tail_valid && mid_valid) {
        out = mid;
        return true;
    }

    // Both valid: accept only identical decoded source semantics including space
    if (CompareCommonSourceFields(tail, mid) && tail.source_space == mid.source_space) {
        out = tail;
        return true;
    }
    // Different valid interpretations -> fail closed
    return false;
}

inline auto DecodeSphaira128(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    const u32 magic = ReadU32LE(p + 0);
    if (magic != NX_SAVE_META_MAGIC) {
        return false;
    }
    const u32 version = ReadU32LE(p + 4);
    if (version != NX_SAVE_META_VERSION) {
        return false;
    }
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 8);
    out.uid.uid[0] = ReadU64LE(p + 16);
    out.uid.uid[1] = ReadU64LE(p + 24);
    out.system_save_data_id = ReadU64LE(p + 32);
    out.save_data_type = ReadU8(p + 40);
    out.save_data_rank = ReadU8(p + 41);
    out.save_data_index = ReadU16LE(p + 42);
    // bytes 44..47 pad, 48..71 unk ignored (no blanket zero requirement)
    out.owner_id = ReadU64LE(p + 72);
    out.timestamp = ReadU64LE(p + 80);
    out.flags = ReadU32LE(p + 88);
    out.unk_x54 = ReadU32LE(p + 92);
    out.data_size = ReadS64LE(p + 96);
    out.journal_size = ReadS64LE(p + 104);
    out.commit_id = ReadU64LE(p + 112);
    out.raw_size = ReadU64LE(p + 120);
    out.source_space = std::nullopt;

    return ValidateDecodedSaveMeta(out, false);
}

inline auto DecodeDbiRaw512(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 0);
    out.uid.uid[0] = ReadU64LE(p + 8);
    out.uid.uid[1] = ReadU64LE(p + 16);
    out.system_save_data_id = ReadU64LE(p + 24);
    out.save_data_type = ReadU8(p + 32);
    out.save_data_rank = ReadU8(p + 33);
    out.save_data_index = ReadU16LE(p + 34);
    // 36..39 pad, 40..63 unk ignored
    out.owner_id = ReadU64LE(p + 64);
    out.timestamp = ReadU64LE(p + 72);
    out.flags = ReadU32LE(p + 80);
    out.unk_x54 = ReadU32LE(p + 84);
    out.data_size = ReadS64LE(p + 88);
    out.journal_size = ReadS64LE(p + 96);
    out.commit_id = ReadU64LE(p + 104);
    // 112..511 unused ignored
    out.raw_size = 0;
    out.source_space = std::nullopt;

    return ValidateDecodedSaveMeta(out, false);
}

inline auto DecodeNxSaveMeta(const u8* p, size_t size, DecodedSaveMetaInternal& out) -> bool {
    if (size == 85) {
        return DecodeJksv85(p, out);
    } else if (size == 86) {
        return DecodeJksv86WithAmbiguityCheck(p, out);
    } else if (size == 128) {
        return DecodeSphaira128(p, out);
    }
    return false;
}

inline auto ToNXSaveMeta(const DecodedSaveMetaInternal& d) -> NXSaveMeta {
    NXSaveMeta m{};
    m.magic = NX_SAVE_META_MAGIC;
    m.version = NX_SAVE_META_VERSION;
    m.attr.application_id = d.application_id;
    m.attr.uid = d.uid;
    m.attr.system_save_data_id = d.system_save_data_id;
    m.attr.save_data_type = d.save_data_type;
    m.attr.save_data_rank = d.save_data_rank;
    m.attr.save_data_index = d.save_data_index;
    m.owner_id = d.owner_id;
    m.timestamp = d.timestamp;
    m.flags = d.flags;
    m.unk_x54 = d.unk_x54;
    m.data_size = d.data_size;
    m.journal_size = d.journal_size;
    m.commit_id = d.commit_id;
    m.raw_size = d.raw_size;
    return m;
}

namespace {

struct SaveReaderContext {
    zlib_filefunc64_def base_funcs{};
    bool io_error{false};
    bool close_error{false};

    [[nodiscard]] bool HasError() const {
        return io_error || close_error;
    }

    void InitFileFunc(zlib_filefunc64_def* funcs) {
        mz::FileFuncStdio(&base_funcs);
        *funcs = base_funcs;
        funcs->opaque = this;
        funcs->zopen64_file = [](voidpf opaque, const void* filename, int mode) -> voidpf {
            auto self = static_cast<SaveReaderContext*>(opaque);
            return self->base_funcs.zopen64_file(self->base_funcs.opaque, filename, mode);
        };
        funcs->zread_file = [](voidpf opaque, voidpf stream, void* buf, uLong size) -> uLong {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zread_file(self->base_funcs.opaque, stream, buf, size);
            if (res < size) {
                if (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                    self->io_error = true;
                }
            }
            return res;
        };
        funcs->zseek64_file = [](voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) -> long {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zseek64_file(self->base_funcs.opaque, stream, offset, origin);
            if (res != 0) {
                self->io_error = true;
            }
            return res;
        };
        funcs->ztell64_file = [](voidpf opaque, voidpf stream) -> ZPOS64_T {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.ztell64_file(self->base_funcs.opaque, stream);
            if (res == static_cast<ZPOS64_T>(-1)) {
                self->io_error = true;
            } else if (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                self->io_error = true;
            }
            return res;
        };
        funcs->zclose_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = 0;
            if (self->base_funcs.zclose_file) {
                res = self->base_funcs.zclose_file(self->base_funcs.opaque, stream);
                if (res != 0) {
                    self->close_error = true;
                }
            }
            return res;
        };
        funcs->zerror_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = 0;
            if (self->base_funcs.zerror_file) {
                res = self->base_funcs.zerror_file(self->base_funcs.opaque, stream);
                if (res != 0) {
                    self->io_error = true;
                }
            }
            return res;
        };
    }
};

} // namespace

auto ReadArchiveSaveMetadata(void* zfile, ui::ProgressBox* pbox, DecodedSaveMetadata& out, Result* out_rc) -> ArchiveMetaStatus {
    out = DecodedSaveMetadata{};
    if (out_rc) {
        *out_rc = 0;
    }

    if (!zfile) {
        if (out_rc) *out_rc = Result_UnzOpen2_64;
        return ArchiveMetaStatus::Invalid;
    }

    unz_global_info64 ginfo{};
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        if (out_rc) *out_rc = Result_UnzGetGlobalInfo64;
        return ArchiveMetaStatus::Invalid;
    }

    if (ginfo.number_entry == 0) {
        return ArchiveMetaStatus::NoMetadata;
    }
    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        if (out_rc) *out_rc = FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        if (out_rc) *out_rc = Result_UnzGoToFirstFile;
        return ArchiveMetaStatus::Invalid;
    }

    bool success = false;
    ON_SCOPE_EXIT({
        if (!success && zfile) {
            unzGoToFirstFile(zfile);
        }
    });

    bool seen_nx_meta = false;
    bool seen_dbi_extra = false;
    bool seen_dbi_info = false;
    bool seen_dbi_root_marker = false;
    DecodedSaveMetaInternal nx_meta{};
    DecodedSaveMetaInternal dbi_extra_meta{};
    bool has_valid_nx = false;
    bool has_valid_dbi_extra = false;

    for (s64 i = 0; i < entry_count; i++) {
        if (pbox) {
            const auto exit_rc = pbox->ShouldExitResult();
            if (R_FAILED(exit_rc)) {
                if (out_rc) *out_rc = exit_rc;
                return ArchiveMetaStatus::Invalid;
            }
        }

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                if (out_rc) *out_rc = Result_UnzGoToNextFile;
                return ArchiveMetaStatus::Invalid;
            }
        }

        unz_file_info64 info{};
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            if (out_rc) *out_rc = Result_UnzGetCurrentFileInfo64;
            return ArchiveMetaStatus::Invalid;
        }

        if (info.size_filename == 0) {
            if (out_rc) *out_rc = FsError_InvalidCharacter;
            return ArchiveMetaStatus::Invalid;
        }
        if (info.size_filename >= sizeof(name_buf)) {
            if (out_rc) *out_rc = FsError_TooLongPath;
            return ArchiveMetaStatus::Invalid;
        }
        if (std::strlen(name_buf) != info.size_filename) {
            if (out_rc) *out_rc = FsError_TooLongPath;
            return ArchiveMetaStatus::Invalid;
        }

        const std::string_view raw{name_buf, info.size_filename};
        if (raw == "//") {
            if (!path::IsDbiRootMarkerEntry(raw, info.uncompressed_size, info.external_fa)) {
                if (out_rc) *out_rc = FsError_InvalidCharacter;
                return ArchiveMetaStatus::Invalid;
            }
            if (seen_dbi_root_marker) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_dbi_root_marker = true;

            if (UNZ_OK != unzOpenCurrentFile(zfile)) {
                if (out_rc) *out_rc = Result_UnzOpenCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            char drain_chunk[64];
            int drain_res = unzReadCurrentFile(zfile, drain_chunk, sizeof(drain_chunk));
            if (drain_res < 0) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = Result_UnzReadCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res != UNZ_OK) {
                if (out_rc) *out_rc = (close_res == UNZ_CRCERROR) ? 0x8 : Result_UnzOpenCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            continue;
        }

        const auto norm = path::NormalizeSaveArchiveEntry(raw);
        if (!norm.has_value()) {
            if (out_rc) *out_rc = FsError_InvalidCharacter;
            return ArchiveMetaStatus::Invalid;
        }

        std::string_view norm_view = *norm;
        const bool has_dir_attr = (info.external_fa & 0x10) != 0 ||
                                  ((info.external_fa >> 16) & 0xF000) == 0x4000;
        const bool is_dir = (!norm_view.empty() && norm_view.back() == '/') || has_dir_attr;
        std::string_view clean_view = norm_view;
        if (!clean_view.empty() && clean_view.back() == '/') {
            while (clean_view.size() > 1 && clean_view.back() == '/') {
                clean_view.remove_suffix(1);
            }
        }

        // Check if reserved metadata root is used as a parent directory for payload
        const auto slash_pos = clean_view.find('/');
        if (slash_pos != std::string_view::npos) {
            const auto first_segment = clean_view.substr(0, slash_pos);
            if (ClassifySaveReservedMetadataRoot(first_segment) != SaveReservedMetaKind::None) {
                if (out_rc) *out_rc = FsError_InvalidCharacter;
                return ArchiveMetaStatus::Invalid;
            }
            out.payload_count++;
            continue;
        }

        const auto reserved_kind = ClassifySaveReservedMetadataRoot(clean_view);
        if (reserved_kind == SaveReservedMetaKind::None) {
            out.payload_count++;
            continue;
        }

        // Reject directory kind aliases (trailing slash or directory attributes)
        if (is_dir) {
            if (out_rc) *out_rc = FsError_InvalidCharacter;
            return ArchiveMetaStatus::Invalid;
        }

        // Duplicate rejection
        if (reserved_kind == SaveReservedMetaKind::NxMeta) {
            if (seen_nx_meta) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_nx_meta = true;
            // Known NX declared unsupported sizes fail upfront
            if (info.uncompressed_size != 85 && info.uncompressed_size != 86 && info.uncompressed_size != 128) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
        } else if (reserved_kind == SaveReservedMetaKind::DbiExtra) {
            if (seen_dbi_extra) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_dbi_extra = true;
            // Known DBI extra declared unsupported size fails upfront
            if (info.uncompressed_size != 512) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
        } else if (reserved_kind == SaveReservedMetaKind::DbiInfo) {
            if (seen_dbi_info) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_dbi_info = true;
            // Opaque INI: no speculative content-size ceiling
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            if (out_rc) *out_rc = Result_UnzOpenCurrentFile;
            return ArchiveMetaStatus::Invalid;
        }

        u8 meta_read_buf[512]{};
        u64 bytes_drained = 0;
        int read_res = 0;
        do {
            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    unzCloseCurrentFile(zfile);
                    if (out_rc) *out_rc = exit_rc;
                    return ArchiveMetaStatus::Invalid;
                }
            }
            u8 drain_chunk[512];
            void* target_dest = (bytes_drained < sizeof(meta_read_buf))
                ? static_cast<void*>(meta_read_buf + bytes_drained)
                : static_cast<void*>(drain_chunk);
            const uLong target_cap = (bytes_drained < sizeof(meta_read_buf))
                ? static_cast<uLong>(sizeof(meta_read_buf) - bytes_drained)
                : static_cast<uLong>(sizeof(drain_chunk));

            read_res = unzReadCurrentFile(zfile, target_dest, target_cap);
            if (read_res < 0) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = Result_UnzReadCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            if (static_cast<uLong>(read_res) > target_cap) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            if (std::numeric_limits<u64>::max() - bytes_drained < static_cast<u64>(read_res)) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            bytes_drained += static_cast<u64>(read_res);
            if (bytes_drained > info.uncompressed_size) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
        } while (read_res > 0);

        const int close_res = unzCloseCurrentFile(zfile);
        if (close_res == UNZ_CRCERROR) {
            if (out_rc) *out_rc = 0x8;
            return ArchiveMetaStatus::Invalid;
        }
        if (close_res != UNZ_OK) {
            if (out_rc) *out_rc = Result_UnzReadCurrentFile;
            return ArchiveMetaStatus::Invalid;
        }
        if (bytes_drained != info.uncompressed_size) {
            if (out_rc) *out_rc = FsError_InvalidSize;
            return ArchiveMetaStatus::Invalid;
        }

        if (reserved_kind == SaveReservedMetaKind::NxMeta) {
            if (!DecodeNxSaveMeta(meta_read_buf, bytes_drained, nx_meta)) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            has_valid_nx = true;
        } else if (reserved_kind == SaveReservedMetaKind::DbiExtra) {
            if (!DecodeDbiRaw512(meta_read_buf, dbi_extra_meta)) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            has_valid_dbi_extra = true;
        }
    }

    // After declared entry count require expected end-of-list; unexpected extra entry/error -> Invalid
    const int end_rc = unzGoToNextFile(zfile);
    if (end_rc != UNZ_END_OF_LIST_OF_FILE) {
        if (out_rc) *out_rc = (end_rc < 0) ? Result_UnzGoToNextFile : FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }

    // Explicit checked success rewind
    const int rewind_rc = unzGoToFirstFile(zfile);
    if (rewind_rc != UNZ_OK) {
        if (out_rc) *out_rc = Result_UnzGoToFirstFile;
        return ArchiveMetaStatus::Invalid;
    }

    if (!seen_nx_meta && !seen_dbi_extra && !seen_dbi_info) {
        success = true;
        return ArchiveMetaStatus::NoMetadata;
    }

    if (seen_nx_meta && !has_valid_nx) {
        if (out_rc) *out_rc = FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }
    if (seen_dbi_extra && !has_valid_dbi_extra) {
        if (out_rc) *out_rc = FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }

    DecodedSaveMetadata local_out{};

    if (has_valid_nx && has_valid_dbi_extra) {
        if (!CompareCommonSourceFields(nx_meta, dbi_extra_meta)) {
            if (out_rc) *out_rc = FsError_InvalidSize;
            return ArchiveMetaStatus::Invalid;
        }
        local_out.meta = ToNXSaveMeta(nx_meta);
        local_out.source_space = nx_meta.source_space;
        local_out.has_nx_meta = true;
        local_out.has_dbi_extra = true;
        local_out.has_dbi_info = seen_dbi_info;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::Valid;
    }

    if (has_valid_nx) {
        local_out.meta = ToNXSaveMeta(nx_meta);
        local_out.source_space = nx_meta.source_space;
        local_out.has_nx_meta = true;
        local_out.has_dbi_info = seen_dbi_info;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::Valid;
    }

    if (has_valid_dbi_extra) {
        local_out.meta = ToNXSaveMeta(dbi_extra_meta);
        local_out.source_space = std::nullopt;
        local_out.has_dbi_extra = true;
        local_out.has_dbi_info = seen_dbi_info;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::Valid;
    }

    if (seen_dbi_info && !seen_nx_meta && !seen_dbi_extra) {
        local_out.has_dbi_info = true;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::NoMetadata;
    }

    if (out_rc) *out_rc = FsError_InvalidSize;
    return ArchiveMetaStatus::Invalid;
}

auto DbiBackupMatchesEntry(const fs::FsPath& zip_path, const Entry& e) -> bool {
    SaveReaderContext reader_ctx;
    zlib_filefunc64_def file_func;
    reader_ctx.InitFileFunc(&file_func);

    auto zfile = unzOpen2_64(zip_path.s, &file_func);
    if (!zfile) {
        return false;
    }
    bool zfile_open = true;
    ON_SCOPE_EXIT({
        if (zfile_open && zfile) {
            unzClose(zfile);
        }
    });

    DecodedSaveMetadata archive_meta{};
    Result meta_rc = 0;
    const auto meta_status = ReadArchiveSaveMetadata(zfile, nullptr, archive_meta, &meta_rc);

    zfile_open = false;
    const int close_res = unzClose(zfile);
    if (close_res != UNZ_OK || reader_ctx.HasError()) {
        return false;
    }

    if (meta_status == ArchiveMetaStatus::Invalid) {
        return false;
    }
    if (meta_status == ArchiveMetaStatus::Valid) {
        const auto& meta = archive_meta.meta;
        if (meta.attr.save_data_type != e.save_data_type) {
            return false;
        }
        if (meta.attr.application_id != e.application_id) {
            return false;
        }
        if (e.save_data_type == FsSaveDataType_Account) {
            if (std::memcmp(&meta.attr.uid, &e.uid, sizeof(e.uid)) != 0) {
                return false;
            }
        }
        if (e.save_data_type == FsSaveDataType_Cache) {
            if (meta.attr.save_data_index != e.save_data_index) {
                return false;
            }
        }
        return true;
    }

    // Absent metadata -> existing metadata-free DBI behavior preserved
    if (e.save_data_type != FsSaveDataType_Account && e.save_data_type != FsSaveDataType_Cache) {
        return true;
    }
    return true;
}


auto CollectDbiBackups(fs::Fs* fs, const Entry& e) -> std::vector<fs::FsPath> {
    std::vector<fs::FsPath> out;

    const auto sort_desc = [](std::vector<FsDirectoryEntry>& entries) {
        std::ranges::sort(entries, [](const FsDirectoryEntry& a, const FsDirectoryEntry& b) {
            return strcasecmp(a.name, b.name) > 0;
        });
    };

    const auto scan_game_dir = [&](const fs::FsPath& game_dir) {
        filebrowser::FsDirCollection direct{};
        filebrowser::FsView::get_collection(fs, game_dir, "", direct, true, true, false);
        sort_desc(direct.files);
        for (const auto& file : direct.files) {
            if (IsDbiBackupName(e, file.name)) {
                const auto p = fs::AppendPath(direct.path, file.name);
                bool duplicate = false;
                for (const auto& existing : out) {
                    if (path::EqualsIC(existing.s, p.s)) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    out.emplace_back(p);
                }
            }
        }
        sort_desc(direct.dirs);
        for (const auto& date : direct.dirs) {
            filebrowser::FsDirCollection files{};
            filebrowser::FsView::get_collection(fs, fs::AppendPath(game_dir, date.name), "", files, true, false, false);
            sort_desc(files.files);

            for (const auto& file : files.files) {
                if (IsDbiBackupName(e, file.name)) {
                    const auto p = fs::AppendPath(files.path, file.name);
                    bool duplicate = false;
                    for (const auto& existing : out) {
                        if (path::EqualsIC(existing.s, p.s)) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        out.emplace_back(p);
                    }
                }
            }
        }
    };

    std::vector<fs::FsPath> dbi_roots;
    auto add_root = [&](std::string_view r) {
        if (r.empty()) return;
        for (const auto& existing : dbi_roots) {
            if (path::EqualsIC(existing.s, r)) return;
        }
        dbi_roots.emplace_back(r);
    };
    add_root(DBI_SAVES_PATH);
    add_root(DBI_SAVES_ROOT_PATH);
    for (const auto& custom : GetBackupSearchPaths()) {
        add_root(custom);
    }

    const auto game_folder = BuildDbiGameFolderName(e);
    for (const auto& root_str : dbi_roots) {
        const auto dbi_root = fs::AppendPath(fs->Root(), root_str);
        scan_game_dir(fs::AppendPath(dbi_root, game_folder));
    }

    if (out.empty()) {
        for (const auto& root_str : dbi_roots) {
            const auto dbi_root = fs::AppendPath(fs->Root(), root_str);
            filebrowser::FsDirCollection root{};
            filebrowser::FsView::get_collection(fs, dbi_root, "", root, false, true, false);

            for (const auto& dir : root.dirs) {
                if (!strcasecmp(dir.name, game_folder)) {
                    continue;
                }
                scan_game_dir(fs::AppendPath(dbi_root, dir.name));
            }
        }
    }

    return out;
}

auto IsDisaSaveFile(fs::Fs* fs, const fs::FsPath& path) -> bool {
    fs::File file;
    if (R_FAILED(fs->OpenFile(path, FsOpenMode_Read, &file))) {
        return false;
    }
    s64 size{};
    if (R_FAILED(file.GetSize(&size)) || size < 0x200) {
        return false;
    }
    char buf[0x104]{};
    u64 bytes_read{};
    if (R_FAILED(file.Read(0, buf, sizeof(buf), FsReadOption_None, &bytes_read)) || bytes_read < 0x104) {
        return false;
    }
    return std::memcmp(buf + 0x100, "DISF", 4) == 0;
}

auto IsRawSaveCandidate(fs::Fs* fs, const fs::FsPath& path, std::string_view name) -> bool {
    if (name.ends_with(".disa") || name.ends_with(".bin")) {
        return IsDisaSaveFile(fs, path);
    }
    if (name.size() == 16) {
        bool all_hex = true;
        for (char c : name) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            return IsDisaSaveFile(fs, path);
        }
    }
    return false;
}

auto NormalizeBackupSearchPath(std::string_view path) -> std::optional<std::string> {
    const auto normalized = path::NormalizeAbsoluteSdPath(path);
    if (!normalized) {
        return std::nullopt;
    }
    if (*normalized == "/" || path::EqualsIC(*normalized, DEFAULT_BACKUP_ROOT) || path::EqualsIC(*normalized, DBI_SAVES_PATH) || path::EqualsIC(*normalized, DBI_SAVES_ROOT_PATH)) {
        return std::nullopt;
    }
    if (normalized->size() >= FS_MAX_PATH) {
        return std::nullopt;
    }
    return normalized;
}

auto GetBackupSearchPaths() -> std::vector<std::string> {
    std::vector<std::string> search_paths;

    ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        if (Section && Value && path::EqualsIC(Section, BACKUP_PATHS_INI_SECTION)) {
            auto* paths = static_cast<std::vector<std::string>*>(UserData);
            const auto normalized = NormalizeBackupSearchPath(Value);
            if (normalized) {
                const bool duplicate = std::any_of(paths->begin(), paths->end(), [&](const std::string& existing) {
                    return path::EqualsIC(existing, *normalized);
                });
                if (!duplicate) {
                    paths->push_back(*normalized);
                }
            }
        }
        return 1;
    }, &search_paths, App::CONFIG_PATH);

    return search_paths;
}

auto GetShareableSaveBackupRoots() -> std::vector<std::string> {
    std::vector<std::string> roots;
    auto add_unique = [&](const std::string& p) {
        if (p.empty()) {
            return;
        }
        for (const auto& existing : roots) {
            if (path::EqualsIC(existing, p)) {
                return;
            }
        }
        roots.push_back(p);
    };

    add_unique(DEFAULT_BACKUP_ROOT);
    add_unique(DBI_SAVES_PATH);
    add_unique(DBI_SAVES_ROOT_PATH);
    for (const auto& extra : GetBackupSearchPaths()) {
        add_unique(extra);
    }
    return roots;
}

static auto SaveBackupSearchPaths(const std::vector<std::string>& search_paths) -> bool {
    if (!ini_puts(BACKUP_PATHS_INI_SECTION, nullptr, nullptr, App::CONFIG_PATH)) {
        return false;
    }

    char key[32];
    for (size_t i = 0; i < search_paths.size(); i++) {
        std::snprintf(key, sizeof(key), "path_%zu", i);
        if (!ini_puts(BACKUP_PATHS_INI_SECTION, key, search_paths[i].c_str(), App::CONFIG_PATH)) {
            return false;
        }
    }

    return true;
}

auto AddBackupSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeBackupSearchPath(path.toString());
    if (!normalized) {
        return false;
    }

    auto paths = GetBackupSearchPaths();
    for (const auto& existing : paths) {
        if (path::EqualsIC(existing, *normalized)) {
            return true;
        }
    }

    paths.push_back(*normalized);
    if (!SaveBackupSearchPaths(paths)) {
        return false;
    }

    SignalChange();
    return true;
}

auto RemoveBackupSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeBackupSearchPath(path.toString());
    if (!normalized) {
        return false;
    }

    auto paths = GetBackupSearchPaths();
    const auto it = std::remove_if(paths.begin(), paths.end(), [&](const std::string& existing) {
        return path::EqualsIC(existing, *normalized);
    });

    if (it == paths.end()) {
        return false;
    }

    paths.erase(it, paths.end());
    if (!SaveBackupSearchPaths(paths)) {
        return false;
    }

    SignalChange();
    return true;
}

auto NormalizeBackupRoot(const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> fs::FsPath {
    auto out = path.toString();
    const auto root = fs_entry.root.toString();

    if (fs_entry.type == filebrowser::FsType::Stdio && out.starts_with(root)) {
        out.erase(0, root.size());
    }

    if (out.empty()) {
        out = "/";
    } else if (out.front() != '/') {
        out.insert(out.begin(), '/');
    }

    return out;
}

auto InferBackupIdFromPath(std::string_view full_path) -> u64 {
    u64 parent_hex = 0;
    u64 filename_hex = 0;

    size_t start = 0;
    while (start < full_path.size()) {
        auto slash = full_path.find_first_of("/\\", start);
        auto part = (slash == full_path.npos) ? full_path.substr(start) : full_path.substr(start, slash - start);
        const bool is_last = (slash == full_path.npos);

        if (is_last) {
            if (part.ends_with(".zip") || part.ends_with(".bin") || part.ends_with(".disa")) {
                part = part.substr(0, part.rfind('.'));
            }
            if (part.size() == 16) {
                filename_hex = ParseHex16(part);
            }
        } else {
            if (part.size() == 16) {
                const auto hex = ParseHex16(part);
                if (hex != 0) {
                    parent_hex = hex;
                }
            }
        }

        if (slash == full_path.npos) break;
        start = slash + 1;
    }

    return (parent_hex != 0) ? parent_hex : filename_hex;
}

auto InspectBackupArchive(fs::Fs* fs, const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, BackupArchiveInfo& out) -> bool {
    const bool is_zip = path::EndsWithIC(filename, ".zip");
    const bool is_raw = !is_zip && IsRawSaveCandidate(fs, path, filename);
    if (!is_zip && !is_raw) {
        return false;
    }

    out = BackupArchiveInfo{};
    out.path = path;
    out.dbi_game_dir = std::string{dbi_game_dir_name};
    out.timestamp = ParseBackupNameTimestamp(filename);

    bool loaded = false;

    if (is_zip) {
        // Precedence 1: valid embedded archive metadata
        SaveReaderContext reader_ctx;
        zlib_filefunc64_def file_func;
        reader_ctx.InitFileFunc(&file_func);
        auto zfile = unzOpen2_64(path.s, &file_func);
        if (!zfile) {
            return false;
        }
        bool zfile_open = true;
        ON_SCOPE_EXIT({
            if (zfile_open && zfile) {
                unzClose(zfile);
            }
        });

        DecodedSaveMetadata archive_meta{};
        Result meta_rc = 0;
        const auto meta_status = ReadArchiveSaveMetadata(zfile, nullptr, archive_meta, &meta_rc);

        zfile_open = false;
        const int close_res = unzClose(zfile);
        if (close_res != UNZ_OK || reader_ctx.HasError()) {
            return false;
        }

        if (meta_status == ArchiveMetaStatus::Invalid) {
            // Present invalid metadata: fail closed, NO fallback!
            return false;
        }

        if (archive_meta.payload_count == 0) {
            // Metadata-only archive without kept payload: fail closed, omit from library!
            return false;
        }

        out.payload_count = archive_meta.payload_count;

        if (meta_status == ArchiveMetaStatus::Valid) {
            const auto& meta = archive_meta.meta;
            out.application_id = meta.attr.application_id;
            out.system_save_data_id = meta.attr.system_save_data_id;
            out.save_data_type = meta.attr.save_data_type;
            out.uid = meta.attr.uid;
            out.save_data_index = meta.attr.save_data_index;
            out.save_data_rank = meta.attr.save_data_rank;
            out.rank_known = true;
            out.commit_id = meta.commit_id;
            out.source_timestamp = meta.timestamp;
            if (out.timestamp == 0 && meta.timestamp != 0) {
                out.timestamp = PosixToTimestamp(meta.timestamp);
            }
            loaded = true;
        }

        if (!loaded) {
            // Precedence 2: DBI filename fields
            if (out.application_id == 0 && out.system_save_data_id == 0) {
                out.application_id = ParseDbiBackupAppId(filename);
            }
            if (out.save_data_type == 0xFF) {
                if (filename.size() >= 19 && filename[16] == '_' && filename[18] == '_') {
                    out.save_data_type = ParseDbiTypeLetter(filename[17]);
                }
            }
            if (out.save_data_index == 0) {
                out.save_data_index = ParseDbiBackupIndex(filename);
            }
        }
    }

    if (!loaded) {
        // Precedence 3: explicit DBI directory/folder information
        if (out.application_id == 0 && out.system_save_data_id == 0) {
            if (!dbi_game_dir_name.empty()) {
                out.application_id = ParseHex16(dbi_game_dir_name);
            }
        }

        // Precedence 4: 16-hex component inferred from the full path
        if (out.application_id == 0 && out.system_save_data_id == 0) {
            const auto hex = InferBackupIdFromPath(path.s);
            if (hex != 0) {
                std::string_view p{path.s};
                const bool is_system = (hex & 0x8000000000000000ULL) ||
                                       p.find("Save System") != p.npos ||
                                       (out.save_data_type != 0xFF && IsSystemLikeSave(out.save_data_type));
                if (is_system) {
                    out.system_save_data_id = hex;
                } else {
                    out.application_id = hex;
                }
            }
        }

        // Save-folder context for save_data_type
        if (out.save_data_type == 0xFF) {
            std::string_view p{path.s};
            if (p.find("Save System BCAT") != p.npos) {
                out.save_data_type = FsSaveDataType_SystemBcat;
            } else if (p.find("Save System") != p.npos) {
                out.save_data_type = FsSaveDataType_System;
            } else if (p.find("Save BCAT") != p.npos || p.find("/BCAT/") != p.npos) {
                out.save_data_type = FsSaveDataType_Bcat;
            } else if (p.find("Save Device") != p.npos || p.find("/Device/") != p.npos) {
                out.save_data_type = FsSaveDataType_Device;
            } else if (p.find("Save Temporary") != p.npos || p.find("/Temporary/") != p.npos) {
                out.save_data_type = FsSaveDataType_Temporary;
            } else if (p.find("Save Cache") != p.npos || p.find("/Cache/") != p.npos) {
                out.save_data_type = FsSaveDataType_Cache;
            } else if (p.find("/Save/") != p.npos || p.find("/Account/") != p.npos) {
                out.save_data_type = FsSaveDataType_Account;
            }
        }
    }

    if (out.application_id == 0 && out.system_save_data_id == 0) {
        return false;
    }

    if (IsSystemLikeSave(out.save_data_type)) {
        if (out.system_save_data_id == 0 && out.application_id != 0) {
            out.system_save_data_id = out.application_id;
            out.application_id = 0;
        }
    } else if (out.save_data_type == 0xFF) {
        if (out.system_save_data_id != 0) {
            out.save_data_type = FsSaveDataType_System;
        } else {
            out.save_data_type = FsSaveDataType_Account;
        }
    }

    return true;
}

auto FormatBackupAccount(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string {
    if (e.save_data_type != FsSaveDataType_Account) {
        return "Not account-bound";
    }

    for (const auto& acc : accounts) {
        if (!std::memcmp(&e.uid, &acc.uid, sizeof(e.uid))) {
            return acc.nickname;
        }
    }

    if (e.uid.uid[0] != 0 || e.uid.uid[1] != 0) {
        char buf[16];
        const u64 display_id = e.uid.uid[0] ? e.uid.uid[0] : e.uid.uid[1];
        std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(display_id & 0xFFFFFFFF));
        return buf;
    }

    return "00000000";
}

auto FormatBackupTimestamp(u64 ts, bool compact) -> std::string {
    if (ts == 0) {
        return "Unknown date";
    }
    const u32 year = static_cast<u32>(ts / 10000000000ULL);
    const u32 mon = static_cast<u32>((ts / 100000000ULL) % 100);
    const u32 day = static_cast<u32>((ts / 1000000ULL) % 100);
    const u32 hour = static_cast<u32>((ts / 10000ULL) % 100);
    const u32 min = static_cast<u32>((ts / 100ULL) % 100);
    const u32 sec = static_cast<u32>(ts % 100);

    char buf[32];
    if (compact) {
        std::snprintf(buf, sizeof(buf), "%04u.%02u.%02u %02u:%02u", year, mon, day, hour, min);
    } else {
        std::snprintf(buf, sizeof(buf), "%04u.%02u.%02u %02u:%02u:%02u", year, mon, day, hour, min, sec);
    }
    return buf;
}

namespace {

auto FormatBackupRankMarker(const Entry& e) -> std::string {
    if (!e.backup_rank_known) {
        return "rk:?";
    }
    return (e.save_data_rank == FsSaveDataRank_Secondary) ? "rk:1" : "rk:0";
}

} // namespace

auto GetBackupSecondaryColumns(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> BackupSecondaryColumns {
    const u64 id = IsSystemLikeSave(e.save_data_type) ? e.system_save_data_id : e.application_id;
    char id_str[33];
    std::snprintf(id_str, sizeof(id_str), "%016lX", id);

    BackupSecondaryColumns cols;
    cols.title_id = id_str;
    cols.account = "  •  " + FormatBackupAccount(e, accounts) + "  •  " + FormatBackupRankMarker(e);
    const auto date_str = FormatBackupTimestamp(e.backup_timestamp, false);
    if (!date_str.empty()) {
        cols.timestamp = "  •  " + date_str;
    }
    if (e.backup_count > 1) {
        cols.archive_count = "  •  " + std::to_string(e.backup_count) + " archives";
    }
    if (!e.dbi_game_dir.empty()) {
        cols.archive_count += "  •  DBI";
    }
    return cols;
}

auto FormatBackupSecondaryText(const Entry& e, const std::vector<AccountProfileBase>& accounts) -> std::string {
    const std::string account = FormatBackupAccount(e, accounts);
    const std::string rank_str = FormatBackupRankMarker(e);
    const std::string date_str = FormatBackupTimestamp(e.backup_timestamp, true);

    std::string out = account + "  •  " + rank_str + "  •  " + date_str;
    if (e.backup_count > 1) {
        out += " (" + std::to_string(e.backup_count) + ")";
    }
    if (!e.dbi_game_dir.empty()) {
        out += "  •  DBI";
    }
    return out;
}

static auto FormatRankKeyPart(bool rank_known, u8 rank) -> const char* {
    if (!rank_known) {
        return "rk:?";
    }
    return (rank == FsSaveDataRank_Secondary) ? "rk:1" : "rk:0";
}

auto BackupGroupKey(const BackupArchiveInfo& info) -> std::string {
    char key[0x80];
    const char* rk = FormatRankKeyPart(info.rank_known, info.save_data_rank);
    if (IsSystemLikeSave(info.save_data_type)) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u:%s",
            info.save_data_type, info.system_save_data_id, info.save_data_index, rk);
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u:%s",
            info.application_id, info.save_data_type, info.uid.uid[0], info.uid.uid[1], info.save_data_index, rk);
    }
    return key;
}

auto BackupGroupKey(const Entry& e) -> std::string {
    char key[0x80];
    const bool rank_known = e.is_backup ? e.backup_rank_known : true;
    const char* rk = FormatRankKeyPart(rank_known, e.save_data_rank);
    if (IsSystemLikeSave(e.save_data_type)) {
        std::snprintf(key, sizeof(key), "backup:system:%u:%016lX:%u:%s",
            e.save_data_type, e.system_save_data_id, e.save_data_index, rk);
    } else {
        std::snprintf(key, sizeof(key), "backup:app:%016lX:%u:%016lX%016lX:%u:%s",
            e.application_id, e.save_data_type, e.uid.uid[0], e.uid.uid[1], e.save_data_index, rk);
    }
    return key;
}

auto VerifyZipIntegrity(const fs::FsPath& path) -> bool {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    auto zfile = unzOpen2_64(path.s, &file_func);
    if (!zfile) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zfile));

    unz_global_info64 gi{};
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &gi) || gi.number_entry == 0) {
        return false;
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        return false;
    }

    std::vector<u8> buffer(64 * 1024);
    u64 entries_read = 0;
    while (true) {
        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            return false;
        }

        int read_bytes = 0;
        do {
            read_bytes = unzReadCurrentFile(zfile, buffer.data(), buffer.size());
            if (read_bytes < 0) {
                unzCloseCurrentFile(zfile);
                return false;
            }
        } while (read_bytes > 0);

        if (UNZ_OK != unzCloseCurrentFile(zfile)) {
            return false;
        }

        entries_read++;

        const int next_rc = unzGoToNextFile(zfile);
        if (next_rc == UNZ_END_OF_LIST_OF_FILE) {
            break;
        }
        if (next_rc != UNZ_OK) {
            return false;
        }
    }

    return entries_read == gi.number_entry && entries_read > 0;
}

auto GetRawRestoreUnsupportedMessage() -> std::string {
    return "RAW container restore is unsupported."_i18n;
}

} // namespace sphaira::ui::menu::save
