#include "account/account_user.hpp"
#include "account/account_playtime.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "image.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "path_util.hpp"
#include "threaded_file_transfer.hpp"
#include "title_info.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::account_user {
namespace {

constexpr u32 CMD_BEGIN_REG = 200;
constexpr u32 CMD_COMPLETE_REG = 201;
constexpr u32 CMD_DELETE_USER = 203;
constexpr u32 CMD_GET_PROFILE_EDITOR = 205;
constexpr u32 CMD_EDITOR_STORE = 100;
constexpr u32 CMD_EDITOR_STORE_IMAGE = 101;

constexpr u32 BUF_IN_PTR = SfBufferAttr_In | SfBufferAttr_HipcPointer;
constexpr u32 BUF_IN_MAP = SfBufferAttr_In | SfBufferAttr_HipcMapAlias;

auto OpenAccSu(Service* out) -> Result {
    R_TRY(smGetService(out, "acc:su"));
    R_SUCCEED();
}

auto SanitizeName(std::string name) -> std::string {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            out += static_cast<char>(c);
        } else if (c == ' ' && !out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    if (out.size() > 24) {
        out.resize(24);
    }
    if (out.empty()) {
        out = "user";
    }
    return out;
}

} // namespace

auto FormatPackCreated(const std::string& folder_name, const std::string& json_created) -> std::string {
    std::string stamp = json_created;
    if (stamp.size() < 15 && folder_name.size() >= 15) {
        stamp = folder_name.substr(0, 15);
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(stamp.c_str(), "%4d%2d%2d_%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return {};
    }
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d, %02d:%02d", d, mo, y, h, mi);
    return buf;
}

auto ReadJsonField(const std::string& json, const char* key) -> std::string {
    const auto needle = std::string{"\""} + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    auto i = pos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size() || json[i] != ':') {
        return {};
    }
    i++;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size() || json[i] != '"') {
        return {};
    }
    i++;
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            i++;
        }
        out += json[i++];
    }
    return out;
}

auto ReadJsonIntField(const std::string& json, const char* key) -> std::optional<s64> {
    const auto needle = std::string{"\""} + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    auto i = pos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size() || json[i] != ':') {
        return std::nullopt;
    }
    i++;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size()) {
        return std::nullopt;
    }
    char* endptr = nullptr;
    const char* start = json.c_str() + i;
    const long long val = std::strtoll(start, &endptr, 10);
    if (endptr == start) {
        return std::nullopt;
    }
    return static_cast<s64>(val);
}

namespace {

auto JsonEscape(const std::string& s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        if (c != '\n' && c != '\r') {
            out += c;
        }
    }
    return out;
}

auto LoadProfile(const AccountUid& uid, AccountProfileBase& base, AccountUserData& data) -> Result {
    AccountProfile profile{};
    R_TRY(accountGetProfile(&profile, uid));
    ON_SCOPE_EXIT(accountProfileClose(&profile));
    R_TRY(accountProfileGet(&profile, &data, &base));
    base.uid = uid;
    R_SUCCEED();
}

auto StoreProfile(const AccountUid& uid, const AccountProfileBase& base, const AccountUserData& data,
    const u8* jpeg, u64 jpeg_size) -> Result
{
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service editor{};
    R_TRY(serviceDispatchIn(&accsu, CMD_GET_PROFILE_EDITOR, uid,
        .out_num_objects = 1,
        .out_objects = &editor));
    ON_SCOPE_EXIT(serviceClose(&editor));

    if (jpeg && jpeg_size) {
        R_TRY(serviceDispatchIn(&editor, CMD_EDITOR_STORE_IMAGE, base,
            .buffer_attrs = { BUF_IN_PTR, BUF_IN_MAP },
            .buffers = { { &data, sizeof(data) }, { jpeg, jpeg_size } }));
    } else {
        R_TRY(serviceDispatchIn(&editor, CMD_EDITOR_STORE, base,
            .buffer_attrs = { BUF_IN_PTR },
            .buffers = { { &data, sizeof(data) } }));
    }
    R_SUCCEED();
}

} // namespace

auto LoadImageJpeg(const AccountUid& uid, std::vector<u8>& out) -> Result {
    AccountProfile profile{};
    R_TRY(accountGetProfile(&profile, uid));
    ON_SCOPE_EXIT(accountProfileClose(&profile));

    u32 size{};
    R_TRY(accountProfileGetImageSize(&profile, &size));
    R_UNLESS(size > 0, Result_FsEmpty);
    out.resize(size);
    u32 actual{};
    R_TRY(accountProfileLoadImage(&profile, out.data(), out.size(), &actual));
    out.resize(actual);
    R_SUCCEED();
}

auto Rename(const AccountUid& uid, const std::string& nickname) -> Result {
    AccountProfileBase base{};
    AccountUserData data{};
    R_TRY(LoadProfile(uid, base, data));
    std::memset(base.nickname, 0, sizeof(base.nickname));
    const auto n = std::min(nickname.size(), sizeof(base.nickname) - 1);
    std::memcpy(base.nickname, nickname.data(), n);
    R_TRY(StoreProfile(uid, base, data, nullptr, 0));
    R_SUCCEED();
}

auto SetImageJpeg(const AccountUid& uid, const std::vector<u8>& jpeg) -> Result {
    R_UNLESS(!jpeg.empty(), Result_FsEmpty);
    AccountProfileBase base{};
    AccountUserData data{};
    R_TRY(LoadProfile(uid, base, data));
    R_TRY(StoreProfile(uid, base, data, jpeg.data(), jpeg.size()));
    R_SUCCEED();
}

auto Create(const std::string& nickname, AccountUid& out_uid, const std::vector<u8>& jpeg) -> Result {
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    AccountUid uid{};
    R_TRY(serviceDispatchOut(&accsu, CMD_BEGIN_REG, uid));

    auto cancel = [&]() {
        serviceDispatchIn(&accsu, 202, uid);
    };

    AccountProfileBase base{};
    base.uid = uid;
    const auto n = std::min(nickname.size(), sizeof(base.nickname) - 1);
    std::memcpy(base.nickname, nickname.data(), n);

    AccountUserData data{};
    std::vector<u8> icon = jpeg;
    if (!icon.empty()) {
        auto normalized = ImageNormalizeAvatar(icon);
        if (normalized.empty()) {
            normalized = ImageNormalizeIcon(icon);
        }
        if (!normalized.empty()) {
            icon = std::move(normalized);
        }
    }
    auto store = StoreProfile(uid, base, data,
        icon.empty() ? nullptr : icon.data(), icon.size());
    if (R_FAILED(store)) {
        log_write("[USER] create store failed 0x%X\n", store);
        cancel();
        R_TRY(store);
    }

    auto complete = serviceDispatchIn(&accsu, CMD_COMPLETE_REG, uid);
    if (R_FAILED(complete)) {
        complete = serviceDispatchIn(&accsu, 206, uid); // CompleteUserRegistrationForcibly
    }
    if (R_FAILED(complete)) {
        log_write("[USER] create complete failed 0x%X\n", complete);
        cancel();
        R_TRY(complete);
    }

    out_uid = uid;
    log_write("[USER] created %s\n", nickname.c_str());
    R_SUCCEED();
}

auto Delete(const AccountUid& uid) -> Result {
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));
    const auto rc = serviceDispatchIn(&accsu, CMD_DELETE_USER, uid);
    if (R_FAILED(rc)) {
        log_write("[USER] DeleteUser 0x%X uid %s\n", rc, account_link::UidHex(uid).c_str());
        return rc;
    }
    log_write("[USER] deleted %s\n", account_link::UidHex(uid).c_str());
    R_SUCCEED();
}

struct AppInfo {
    u64 app_id{};
    std::string name;
};

auto CollectInstalledApps() -> std::vector<AppInfo> {
    std::vector<AppInfo> apps;
    std::vector<NsApplicationRecord> records(32);
    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count)) || count <= 0) {
            break;
        }
        for (s32 i = 0; i < count; i++) {
            const auto app_id = records[i].application_id;
            if (!app_id) {
                continue;
            }
            if ((app_id & 0x0500000000000000) == 0x0500000000000000) {
                continue;
            }
            title::MetaEntries installed_content;
            if (R_FAILED(title::GetMetaEntries(app_id, installed_content)) || installed_content.empty()) {
                continue;
            }
            std::string name;
            if (auto* data = title::Get(app_id); data && data->status == title::NacpLoadStatus::Loaded) {
                name = data->lang.name;
            }
            if (name.empty()) {
                auto control = std::make_unique<NsApplicationControlData>();
                u64 actual_size = 0;
                if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control.get(), sizeof(NsApplicationControlData), &actual_size))) {
                    NacpLanguageEntry* lang = nullptr;
                    if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &lang)) && lang) {
                        name = lang->name;
                    }
                }
            }
            if (name.empty()) {
                name = "Unknown";
            }
            for (char& c : name) {
                if (c == '\t' || c == '\r' || c == '\n') {
                    c = ' ';
                }
            }
            apps.push_back({app_id, std::move(name)});
        }
        offset += count;
    }
    return apps;
}

auto BuildUserReadme(const std::string& nickname, const std::string& hex) -> std::string {
    return
        "User Profile Backup\n"
        "===================\n\n"
        "This archive contains the exported backup for user profile: " + nickname + " (" + hex + ").\n\n"
        "Contents:\n"
        "- manifest.json: KefirHub backup manifest.\n"
        "- profile.json: Profile metadata (nickname, original UID, link status).\n"
        "- avatar.jpg: User profile avatar icon.\n"
        "- baas/, nas/: Nintendo Account link tokens and credentials (if linked and complete).\n"
        "- playtime.tsv: Readable play statistics for installed titles (inspection).\n"
        "- pdm/PlayEvent.dat: This user's play-hour events (exported; not applied by Restore Backup).\n\n"
        "Notes:\n"
        "1. Restore Backup restores nickname, avatar, and Nintendo Account link if complete.\n"
        "2. Play hours (00F0) are not restored by Restore Backup. Use Backup/Restore profiles & play hours for a full console move.\n"
        "3. playtime.tsv is inspection-only.\n"
        "4. Game saves are not included. Back those up separately through Backup saves.\n"
        "5. To replace every profile and the whole play log (same UIDs), use Backup profiles & play hours.\n";
}

auto BuildUserPlaytimeTsv(const AccountUid& uid, const std::vector<AppInfo>& apps, bool pdm_ok) -> std::string {
    std::string tsv = "title_id\ttitle_name\tplaytime_ns\ttotal_launches\tfirst_timestamp_user\tlast_timestamp_user\tfirst_timestamp_network\tlast_timestamp_network\n";

    for (const auto& app : apps) {
        PdmPlayStatistics stats{};
        if (pdm_ok) {
            pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(app.app_id, uid, true, &stats);
        }
        char id_buf[17]{};
        std::snprintf(id_buf, sizeof(id_buf), "%016lX", app.app_id);
        tsv.append(id_buf);
        tsv.push_back('\t');
        tsv.append(app.name);
        tsv.push_back('\t');
        tsv.append(std::to_string(static_cast<unsigned long long>(stats.playtime)));
        tsv.push_back('\t');
        tsv.append(std::to_string(static_cast<unsigned long long>(stats.total_launches)));
        tsv.push_back('\t');
        tsv.append(std::to_string(static_cast<unsigned long long>(stats.first_timestamp_user)));
        tsv.push_back('\t');
        tsv.append(std::to_string(static_cast<unsigned long long>(stats.last_timestamp_user)));
        tsv.push_back('\t');
        tsv.append(std::to_string(static_cast<unsigned long long>(stats.first_timestamp_network)));
        tsv.push_back('\t');
        tsv.append(std::to_string(static_cast<unsigned long long>(stats.last_timestamp_network)));
        tsv.push_back('\n');
    }

    return tsv;
}

auto ZipWriteBuffer(zipFile zf, const char* filename_in_zip, const void* data, size_t size, int compress_level = Z_DEFAULT_COMPRESSION) -> Result {
    zip_fileinfo zi{};
    if (ZIP_OK != zipOpenNewFileInZip(zf, filename_in_zip, &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, compress_level)) {
        log_write("[USER] zipOpenNewFileInZip failed for %s\n", filename_in_zip);
        return Result_FsInvalidType;
    }
    ON_SCOPE_EXIT(zipCloseFileInZip(zf));
    if (size > 0 && data) {
        if (ZIP_OK != zipWriteInFileInZip(zf, data, size)) {
            log_write("[USER] zipWriteInFileInZip failed for %s\n", filename_in_zip);
            return Result_FsInvalidType;
        }
    }
    R_SUCCEED();
}

auto ReadZipFileEntry(unzFile zf, const char* filename, std::vector<u8>& out, u64 max_size = 64 * 1024) -> bool {
    if (UNZ_OK != unzLocateFile(zf, filename, 0)) {
        return false;
    }
    unz_file_info64 info{};
    if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, nullptr, 0, nullptr, 0, nullptr, 0)) {
        return false;
    }
    if (info.uncompressed_size > max_size) {
        return false;
    }
    if (UNZ_OK != unzOpenCurrentFile(zf)) {
        return false;
    }
    ON_SCOPE_EXIT(unzCloseCurrentFile(zf));
    out.resize(info.uncompressed_size);
    if (info.uncompressed_size > 0) {
        u64 total_read = 0;
        while (total_read < info.uncompressed_size) {
            int read_bytes = unzReadCurrentFile(zf, out.data() + total_read, static_cast<unsigned int>(info.uncompressed_size - total_read));
            if (read_bytes <= 0) {
                out.clear();
                return false;
            }
            total_read += read_bytes;
        }
    }
    return true;
}

auto ReadPackAvatar(const Pack& pack, std::vector<u8>& out_jpeg) -> bool {
    out_jpeg.clear();
    fs::FsNativeSd sd;
    if (!pack.is_archive) {
        return R_SUCCEEDED(sd.read_entire_file((pack.dir + "/avatar.jpg").c_str(), out_jpeg)) && !out_jpeg.empty();
    }
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(pack.dir.c_str(), &file_func);
    if (!zf) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zf));
    return ReadZipFileEntry(zf, "avatar.jpg", out_jpeg, 1024 * 1024) && !out_jpeg.empty();
}

auto ExtractPackToDirectory(const Pack& pack, const std::string& out_dir, ui::ProgressBox* pbox) -> Result {
    R_UNLESS(pack.is_archive && std::string_view{pack.dir}.ends_with(".kefir-user.zip"), Result_FsInvalidType);
    const auto validated = FindUserPack(pack.dir);
    R_UNLESS(!validated.dir.empty(), Result_FsInvalidType);

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(out_dir.c_str()));
    R_TRY(thread::TransferUnzipAll(pbox, pack.dir.c_str(), &sd, out_dir.c_str()));
    if (!sd.FileExists((out_dir + "/profile.json").c_str()) || !sd.FileExists((out_dir + "/manifest.json").c_str())) {
        log_write("[USER] Extracted pack missing profile.json or manifest.json in %s\n", out_dir.c_str());
        return Result_FsInvalidType;
    }
    R_SUCCEED();
}

auto EnsureRootsMigrated() -> void {
    const auto old_root = paths::DATA_ROOT + "/user_packs";
    const auto new_root = paths::DATA_ROOT + "/account_backups";
    fs::FsNativeSd sd;
    if (!sd.DirExists(new_root.c_str()) && !sd.FileExists(new_root.c_str()) && sd.DirExists(old_root.c_str())) {
        const auto rc = sd.RenameDirectory(old_root.c_str(), new_root.c_str());
        log_write("[USER] Migrated legacy user_packs to account_backups: 0x%X\n", rc);
    }
}

auto ExportUserPacks(const std::vector<AccountUid>& uids, std::vector<std::string>& out_dirs, bool overwrite_existing) -> Result {
    R_UNLESS(!uids.empty(), Result_FsEmpty);
    log_write("[USER] backup start count=%zu overwrite=%d\n", uids.size(), overwrite_existing ? 1 : 0);

    EnsureRootsMigrated();
    const std::string root = paths::DATA_ROOT + "/account_backups";
    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively(root.c_str()));

    struct Ready {
        AccountUid uid{};
        std::string nickname;
        std::string hex;
        std::vector<u8> jpeg;
    };
    std::vector<Ready> ready;
    for (const auto& uid : uids) {
        Ready r;
        r.uid = uid;
        r.hex = account_link::UidHex(uid);
        AccountProfileBase base{};
        AccountUserData data{};
        if (R_SUCCEEDED(LoadProfile(uid, base, data))) {
            r.nickname = base.nickname;
        }
        LoadImageJpeg(uid, r.jpeg);
        ready.push_back(std::move(r));
    }

    title::Init();
    const auto apps = CollectInstalledApps();
    title::Exit();

    const bool pdm_ok = R_SUCCEEDED(pdmqryInitialize());
    ON_SCOPE_EXIT(if (pdm_ok) { pdmqryExit(); });

    char stamp_buf[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp_buf, sizeof(stamp_buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    const std::string stamp = stamp_buf;

    for (const auto& r : ready) {
        if (overwrite_existing) {
            for (const auto& p : ListUserPacks()) {
                if (!p.uid_hex.empty() && p.uid_hex == r.hex) {
                    DeleteUserPack(p.dir);
                }
            }
        }

        const std::string temp_dir = root + "/.temp_export_" + r.hex;
        sd.DeleteDirectoryRecursively(temp_dir.c_str());
        R_TRY(sd.CreateDirectoryRecursively(temp_dir.c_str()));
        ON_SCOPE_EXIT(sd.DeleteDirectoryRecursively(temp_dir.c_str()));

        std::string link_status = "none";
        R_TRY(account_link::ExportUserLinkPackage(r.uid, temp_dir, link_status));

        std::string status = "link-unavailable";
        if (link_status == "complete") {
            status = "linked";
        } else if (link_status == "none") {
            status = "unlinked";
        } else if (link_status == "unavailable" || link_status == "incomplete") {
            status = "link-unavailable";
        }

        const std::string baas_dir = temp_dir + "/baas";
        const std::string nas_dir = temp_dir + "/nas";
        std::vector<std::string> baas_files;
        std::vector<std::string> nas_files;
        if (sd.DirExists(baas_dir.c_str())) {
            fs::Dir bd;
            R_TRY(sd.OpenDirectory(baas_dir.c_str(), FsDirOpenMode_ReadFiles, &bd));
            std::vector<FsDirectoryEntry> ents;
            R_TRY(bd.ReadAll(ents));
            for (const auto& e : ents) {
                if (e.type == FsDirEntryType_File) {
                    baas_files.push_back(e.name);
                }
            }
        }
        if (sd.DirExists(nas_dir.c_str())) {
            fs::Dir nd;
            R_TRY(sd.OpenDirectory(nas_dir.c_str(), FsDirOpenMode_ReadFiles, &nd));
            std::vector<FsDirectoryEntry> ents;
            R_TRY(nd.ReadAll(ents));
            for (const auto& e : ents) {
                if (e.type == FsDirEntryType_File) {
                    nas_files.push_back(e.name);
                }
            }
        }

        std::vector<PdmPlayEvent> play_events;
        const auto play_rc = account_playtime::CollectUserPlayEvents(r.uid, play_events);
        bool has_play_events = false;
        if (R_FAILED(play_rc)) {
            log_write("[USER] play events collect 0x%X for %s\n", play_rc, r.hex.c_str());
        } else if (!play_events.empty()) {
            R_TRY(account_playtime::WritePackPlayEvents(temp_dir, play_events));
            has_play_events = true;
        }

        const bool has_avatar = !r.jpeg.empty();
        const bool has_baas = !baas_files.empty();
        const bool has_nas = !nas_files.empty();

        const std::string manifest_json = "{\n"
            "  \"generator\": \"KefirHub\",\n"
            "  \"type\": \"account_backup\",\n"
            "  \"version\": 1,\n"
            "  \"created\": \"" + stamp + "\",\n"
            "  \"nickname\": \"" + JsonEscape(r.nickname) + "\",\n"
            "  \"uid\": \"" + r.hex + "\",\n"
            "  \"link_status\": \"" + status + "\",\n"
            "  \"payload\": {\n"
            "    \"profile\": true,\n"
            "    \"avatar\": " + (has_avatar ? "true" : "false") + ",\n"
            "    \"readme\": true,\n"
            "    \"playtime_tsv\": true,\n"
            "    \"play_events\": " + (has_play_events ? "true" : "false") + ",\n"
            "    \"baas\": " + (has_baas ? "true" : "false") + ",\n"
            "    \"nas\": " + (has_nas ? "true" : "false") + "\n"
            "  }\n"
            "}\n";

        const std::string profile_json = "{\"nickname\":\"" + JsonEscape(r.nickname) +
            "\",\"uid\":\"" + r.hex +
            "\",\"created\":\"" + stamp +
            "\",\"link_status\":\"" + link_status + "\"}";

        const std::string readme_txt = BuildUserReadme(r.nickname, r.hex);
        const std::string playtime_tsv = BuildUserPlaytimeTsv(r.uid, apps, pdm_ok);

        const std::string sanitized_name = SanitizeName(r.nickname);
        const std::string base_filename = stamp + "_" + sanitized_name + "_" + status;
        std::string final_path = root + "/" + base_filename + ".kefir-user.zip";
        int suffix = 1;
        while (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str()) ||
               std::ranges::find(out_dirs, final_path) != out_dirs.end()) {
            final_path = root + "/" + base_filename + "_" + std::to_string(suffix++) + ".kefir-user.zip";
        }
        const std::string part_path = final_path + ".part";

        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);
        zipFile zf = zipOpen2_64(part_path.c_str(), APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zf, Result_FsInvalidType);

        bool zip_closed = false;
        bool export_success = false;
        ON_SCOPE_EXIT({
            if (zf && !zip_closed) {
                zipClose(zf, nullptr);
            }
            if (!export_success) {
                sd.DeleteFile(part_path.c_str());
            }
        });

        R_TRY(ZipWriteBuffer(zf, "manifest.json", manifest_json.data(), manifest_json.size()));
        R_TRY(ZipWriteBuffer(zf, "profile.json", profile_json.data(), profile_json.size()));
        R_TRY(ZipWriteBuffer(zf, "README.txt", readme_txt.data(), readme_txt.size()));
        R_TRY(ZipWriteBuffer(zf, "playtime.tsv", playtime_tsv.data(), playtime_tsv.size()));

        if (has_avatar) {
            R_TRY(ZipWriteBuffer(zf, "avatar.jpg", r.jpeg.data(), r.jpeg.size()));
        }

        if (has_play_events) {
            std::vector<u8> pdata;
            R_TRY(sd.read_entire_file((temp_dir + "/pdm/PlayEvent.dat").c_str(), pdata));
            R_UNLESS(!pdata.empty(), Result_FsInvalidType);
            R_TRY(ZipWriteBuffer(zf, "pdm/PlayEvent.dat", pdata.data(), pdata.size()));
        }

        for (const auto& bf : baas_files) {
            std::vector<u8> bdata;
            R_TRY(sd.read_entire_file((baas_dir + "/" + bf).c_str(), bdata));
            R_UNLESS(!bdata.empty(), Result_FsInvalidType);
            R_TRY(ZipWriteBuffer(zf, ("baas/" + bf).c_str(), bdata.data(), bdata.size()));
        }

        for (const auto& nf : nas_files) {
            std::vector<u8> ndata;
            R_TRY(sd.read_entire_file((nas_dir + "/" + nf).c_str(), ndata));
            R_UNLESS(!ndata.empty(), Result_FsInvalidType);
            R_TRY(ZipWriteBuffer(zf, ("nas/" + nf).c_str(), ndata.data(), ndata.size()));
        }

        if (zipClose(zf, "KefirHub user backup") != ZIP_OK) {
            zip_closed = true;
            log_write("[USER] zipClose failed for %s\n", part_path.c_str());
            return Result_FsInvalidType;
        }
        zip_closed = true;

        R_TRY(sd.RenameFile(part_path.c_str(), final_path.c_str()));
        export_success = true;

        out_dirs.push_back(final_path);
        log_write("[USER] backup archive written to %s (link=%s status=%s play_events=%d)\n",
            final_path.c_str(), link_status.c_str(), status.c_str(), has_play_events ? 1 : 0);
    }

    R_SUCCEED();
}

auto ExportUserPack(const AccountUid& uid, std::string& out_dir) -> Result {
    std::vector<std::string> dirs;
    R_TRY(ExportUserPacks({uid}, dirs));
    R_UNLESS(!dirs.empty(), Result_FsEmpty);
    out_dir = dirs.front();
    R_SUCCEED();
}

auto FindUserPack(const std::string& source_path) -> Pack {
    Pack p;
    fs::FsNativeSd sd;
    auto root = source_path;
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) {
        root.pop_back();
    }

    // Check if path is a .kefir-user.zip file
    std::string_view root_view{root};
    if (root_view.ends_with(".kefir-user.zip")) {
        if (!sd.FileExists(root.c_str())) {
            return p;
        }

        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);
        unzFile zf = unzOpen2_64(root.c_str(), &file_func);
        if (!zf) {
            return p;
        }
        ON_SCOPE_EXIT(unzClose(zf));

        unz_global_info64 ginfo{};
        if (UNZ_OK != unzGetGlobalInfo64(zf, &ginfo) || ginfo.number_entry == 0 || ginfo.number_entry > 200) {
            return p;
        }

        if (UNZ_OK != unzGoToFirstFile(zf)) {
            return p;
        }

        std::vector<u8> manifest_bytes;
        std::vector<u8> profile_bytes;
        std::vector<u8> baas_data;
        bool has_manifest = false;
        bool has_profile = false;
        bool has_avatar = false;
        bool has_playtime = false;

        constexpr u64 MAX_META_SIZE = 64 * 1024;

        for (u64 i = 0; i < ginfo.number_entry; i++) {
            if (i > 0) {
                if (UNZ_OK != unzGoToNextFile(zf)) {
                    return p;
                }
            }
            char filename[256]{};
            unz_file_info64 finfo{};
            if (UNZ_OK != unzGetCurrentFileInfo64(zf, &finfo, filename, sizeof(filename), nullptr, 0, nullptr, 0)) {
                return p;
            }
            std::string_view fname{filename};
            if (!sphaira::path::IsSafeArchiveEntry(fname)) {
                log_write("[USER] Unsafe archive entry in %s: %s\n", root.c_str(), filename);
                return p;
            }

            if (fname == "manifest.json") {
                if (finfo.uncompressed_size > MAX_META_SIZE) {
                    return p;
                }
                if (UNZ_OK == unzOpenCurrentFile(zf)) {
                    manifest_bytes.resize(finfo.uncompressed_size);
                    u64 total_read = 0;
                    bool read_ok = true;
                    while (total_read < finfo.uncompressed_size) {
                        int r = unzReadCurrentFile(zf, manifest_bytes.data() + total_read, static_cast<unsigned int>(finfo.uncompressed_size - total_read));
                        if (r <= 0) {
                            read_ok = false;
                            break;
                        }
                        total_read += r;
                    }
                    unzCloseCurrentFile(zf);
                    if (read_ok) {
                        has_manifest = true;
                    }
                }
            } else if (fname == "profile.json") {
                if (finfo.uncompressed_size > MAX_META_SIZE) {
                    return p;
                }
                if (UNZ_OK == unzOpenCurrentFile(zf)) {
                    profile_bytes.resize(finfo.uncompressed_size);
                    u64 total_read = 0;
                    bool read_ok = true;
                    while (total_read < finfo.uncompressed_size) {
                        int r = unzReadCurrentFile(zf, profile_bytes.data() + total_read, static_cast<unsigned int>(finfo.uncompressed_size - total_read));
                        if (r <= 0) {
                            read_ok = false;
                            break;
                        }
                        total_read += r;
                    }
                    unzCloseCurrentFile(zf);
                    if (read_ok) {
                        has_profile = true;
                    }
                }
            } else if (fname == "avatar.jpg") {
                has_avatar = true;
            } else if (fname == "pdm/PlayEvent.dat" || fname == "PlayEvent.dat") {
                has_playtime = true;
            } else if (fname.starts_with("baas/")) {
                if (baas_data.empty() && finfo.uncompressed_size >= 24 && finfo.uncompressed_size <= MAX_META_SIZE) {
                    if (UNZ_OK == unzOpenCurrentFile(zf)) {
                        baas_data.resize(finfo.uncompressed_size);
                        u64 total_read = 0;
                        while (total_read < finfo.uncompressed_size) {
                            int r = unzReadCurrentFile(zf, baas_data.data() + total_read, static_cast<unsigned int>(finfo.uncompressed_size - total_read));
                            if (r <= 0) {
                                baas_data.clear();
                                break;
                            }
                            total_read += r;
                        }
                        unzCloseCurrentFile(zf);
                    }
                }
            }
        }

        if (!has_manifest || !has_profile) {
            log_write("[USER] Archive %s missing manifest.json or profile.json\n", root.c_str());
            return p;
        }

        const std::string man_str(manifest_bytes.begin(), manifest_bytes.end());
        const std::string man_type = ReadJsonField(man_str, "type");
        const auto man_ver = ReadJsonIntField(man_str, "version");
        if (man_type != "account_backup" || !man_ver || *man_ver != 1) {
            log_write("[USER] Archive %s invalid manifest type='%s' version='%lld'\n",
                root.c_str(), man_type.c_str(), man_ver ? static_cast<long long>(*man_ver) : -1LL);
            return p;
        }

        const std::string prof_str(profile_bytes.begin(), profile_bytes.end());
        const std::string nickname = ReadJsonField(prof_str, "nickname");
        const std::string uid_hex = ReadJsonField(prof_str, "uid");
        if (nickname.empty() || uid_hex.empty()) {
            log_write("[USER] Archive %s profile.json missing nickname or uid\n", root.c_str());
            return p;
        }

        p.dir = root;
        p.is_archive = true;
        p.folder_name = root;
        if (const auto slash = p.folder_name.find_last_of("/\\"); slash != std::string::npos) {
            p.folder_name.erase(0, slash + 1);
        }

        p.nickname = nickname;
        p.uid_hex = uid_hex;
        p.created_label = FormatPackCreated(p.folder_name, ReadJsonField(prof_str, "created"));
        if (p.created_label.empty()) {
            p.created_label = FormatPackCreated(p.folder_name, ReadJsonField(man_str, "created"));
        }
        if (p.created_label.empty()) {
            p.created_label = FormatPackCreated(p.folder_name, {});
        }

        p.has_avatar = has_avatar;
        p.has_playtime = has_playtime;

        if (baas_data.size() >= 24) {
            u64 nas = 0;
            std::memcpy(&nas, baas_data.data() + 16, sizeof(u64));
            if (nas != 0) {
                p.nas_id = nas;
                p.link_valid = true;
            }
        }
        if (!p.link_valid) {
            const std::string man_link = ReadJsonField(man_str, "link_status");
            if (man_link == "linked") {
                p.link_valid = true;
            }
        }
        if (!p.link_valid) {
            const std::string prof_link = ReadJsonField(prof_str, "link_status");
            if (prof_link == "complete" || prof_link == "linked") {
                p.link_valid = true;
            }
        }

        return p;
    }

    // Legacy directory-based pack
    if (sd.FileExists((root + "/profile.json").c_str())) {
        p.dir = root;
    } else if (sd.FileExists((root + "/avatar.jpg").c_str()) &&
               (sd.DirExists((root + "/baas").c_str()) || sd.DirExists((root + "/nas").c_str()))) {
        p.dir = root;
    }
    if (p.dir.empty()) {
        return p;
    }

    p.is_archive = false;
    p.folder_name = p.dir;
    while (!p.folder_name.empty() && (p.folder_name.back() == '/' || p.folder_name.back() == '\\')) {
        p.folder_name.pop_back();
    }
    if (const auto slash = p.folder_name.find_last_of("/\\"); slash != std::string::npos) {
        p.folder_name.erase(0, slash + 1);
    }

    std::vector<u8> json;
    if (R_SUCCEEDED(sd.read_entire_file((p.dir + "/profile.json").c_str(), json))) {
        const std::string s(json.begin(), json.end());
        p.nickname = ReadJsonField(s, "nickname");
        p.uid_hex = ReadJsonField(s, "uid");
        p.created_label = FormatPackCreated(p.folder_name, ReadJsonField(s, "created"));
    }
    if (p.created_label.empty()) {
        p.created_label = FormatPackCreated(p.folder_name, {});
    }
    if (p.nickname.empty()) {
        p.nickname = "User";
    }

    p.has_avatar = sd.FileExists((p.dir + "/avatar.jpg").c_str());
    p.has_playtime = account_playtime::PackHasPlayEvents(p.dir);

    account_link::LinkPackage pkg;
    if (R_SUCCEEDED(account_link::LoadUserPackLinkPackage(p.dir, pkg))) {
        p.link_valid = true;
        p.nas_id = pkg.nas_id;
    }

    return p;
}

auto ListUserPacks(const std::string& root) -> std::vector<Pack> {
    std::vector<Pack> packs;
    if (root.empty()) {
        return packs;
    }
    auto base = root;
    while (base.size() > 1 && (base.back() == '/' || base.back() == '\\')) {
        base.pop_back();
    }
    const auto single = FindUserPack(base);
    if (!single.dir.empty()) {
        packs.push_back(single);
        return packs;
    }
    fs::FsNativeSd sd;
    if (!sd.DirExists(base.c_str())) {
        return packs;
    }
    fs::Dir d;
    if (R_FAILED(sd.OpenDirectory(base.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) {
        return packs;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return packs;
    }
    for (const auto& e : entries) {
        if (e.type == FsDirEntryType_Dir) {
            const auto pack_path = base + "/" + e.name;
            const auto pack = FindUserPack(pack_path);
            if (!pack.dir.empty()) {
                packs.push_back(pack);
            }
        } else if (e.type == FsDirEntryType_File) {
            std::string_view name_view{e.name};
            if (name_view.ends_with(".kefir-user.zip")) {
                const auto pack_path = base + "/" + e.name;
                const auto pack = FindUserPack(pack_path);
                if (!pack.dir.empty()) {
                    packs.push_back(pack);
                }
            }
        }
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) {
        return a.folder_name > b.folder_name;
    });
    return packs;
}

auto ListUserPacks() -> std::vector<Pack> {
    EnsureRootsMigrated();
    const auto new_root = paths::DATA_ROOT + "/account_backups";
    const auto old_root = paths::DATA_ROOT + "/user_packs";
    auto packs = ListUserPacks(new_root);
    fs::FsNativeSd sd;
    if (sd.DirExists(old_root.c_str())) {
        auto legacy_packs = ListUserPacks(old_root);
        for (auto& lp : legacy_packs) {
            if (std::ranges::none_of(packs, [&](const Pack& p) { return p.dir == lp.dir; })) {
                packs.push_back(std::move(lp));
            }
        }
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) {
        return a.folder_name > b.folder_name;
    });
    return packs;
}

auto DeleteUserPack(const std::string& dir) -> Result {
    const auto root_new = paths::DATA_ROOT + "/account_backups/";
    const auto root_old = paths::DATA_ROOT + "/user_packs/";
    R_UNLESS(!dir.empty() && (dir.find(root_new) == 0 || dir.find(root_old) == 0), Result_FsInvalidType);
    fs::FsNativeSd sd;
    if (sd.FileExists(dir.c_str())) {
        R_TRY(sd.DeleteFile(dir.c_str()));
    } else if (sd.DirExists(dir.c_str())) {
        R_TRY(sd.DeleteDirectoryRecursively(dir.c_str()));
    } else {
        return Result_FsInvalidType;
    }
    log_write("[USER] deleted pack %s\n", dir.c_str());
    R_SUCCEED();
}

auto GetUserPacksRoot() -> std::string {
    EnsureRootsMigrated();
    return paths::DATA_ROOT + "/account_backups";
}

auto GetShareableUserBackupRoots() -> std::vector<std::string> {
    EnsureRootsMigrated();
    std::vector<std::string> roots;
    const auto new_root = paths::DATA_ROOT + "/account_backups";
    const auto old_root = paths::DATA_ROOT + "/user_packs";
    roots.push_back(new_root);
    fs::FsNativeSd sd;
    if (sd.DirExists(old_root.c_str())) {
        roots.push_back(old_root);
    }
    return roots;
}

} // namespace sphaira::account_user
