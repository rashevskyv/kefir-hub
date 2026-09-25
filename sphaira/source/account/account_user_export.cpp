#include "account/account_user_internal.hpp"
#include "account/account_user.hpp"
#include "account/account_playtime.hpp"
#include "account/account_link.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "title_info.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <minizip/zip.h>

namespace sphaira::account_user {
namespace {

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
            auto* p = title::Get(app_id);
            if (p && p->status == title::NacpLoadStatus::Loaded) {
                name = p->lang.name;
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
        return Result_FsInvalidType;
    }
    ON_SCOPE_EXIT(zipCloseFileInZip(zf));
    if (size > 0 && data) {
        if (ZIP_OK != zipWriteInFileInZip(zf, data, size)) {
            return Result_FsInvalidType;
        }
    }
    R_SUCCEED();
}

} // namespace

auto ExportUserPacks(const std::vector<AccountUid>& uids, std::vector<std::string>& out_dirs, bool overwrite_existing, bool may_terminate_account) -> Result {
    R_UNLESS(!uids.empty(), Result_FsEmpty);
    log_write("[USER] backup start count=%zu overwrite=%d may_terminate=%d\n",
        uids.size(), overwrite_existing ? 1 : 0, may_terminate_account ? 1 : 0);

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
        R_TRY(account_link::ExportUserLinkPackage(r.uid, temp_dir, link_status, may_terminate_account));

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

} // namespace sphaira::account_user
