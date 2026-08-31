#include "account_user.hpp"
#include "account_playtime.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "image.hpp"
#include "log.hpp"
#include "title_info.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

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

auto ReadJsonField(const std::string& json, const char* key) -> std::string {
    const auto needle = std::string{"\""} + key + "\":\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    auto i = pos + needle.size();
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            i++;
        }
        out += json[i++];
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

auto WriteUserReadme(fs::FsNativeSd& sd, const std::string& dir, const std::string& nickname, const std::string& hex) -> Result {
    const std::string readme =
        "User Profile Backup\n"
        "===================\n\n"
        "This folder contains the exported backup for user profile: " + nickname + " (" + hex + ").\n\n"
        "Contents:\n"
        "- profile.json: Profile metadata (nickname, original UID, link status).\n"
        "- avatar.jpg: User profile avatar icon.\n"
        "- baas/, nas/: Nintendo Account link tokens and credentials (if linked and complete).\n"
        "- playtime.tsv: Readable play statistics for installed titles (inspection).\n"
        "- pdm/PlayEvent.dat: This user's play-hour events, restored into 00F0.\n\n"
        "Notes:\n"
        "1. Restore Backup creates a new profile and restores avatar, Nintendo link if complete, and play hours.\n"
        "2. Horizon generates a new UID; play events are remapped to that UID and appended. Other users' hours are not replaced.\n"
        "3. playtime.tsv is inspection-only. System hours come from pdm/PlayEvent.dat.\n"
        "4. Game saves are not included. Back those up separately through Backup saves.\n"
        "5. To replace every profile and the whole play log (same UIDs), use Backup profiles & play hours.\n";

    R_TRY(sd.write_entire_file((dir + "/README.txt").c_str(),
        std::vector<u8>(readme.begin(), readme.end())));
    R_SUCCEED();
}

auto WriteUserPlaytimeTsv(fs::FsNativeSd& sd, const std::string& dir, const AccountUid& uid, const std::vector<AppInfo>& apps, bool pdm_ok) -> Result {
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

    R_TRY(sd.write_entire_file((dir + "/playtime.tsv").c_str(),
        std::vector<u8>(tsv.begin(), tsv.end())));
    R_SUCCEED();
}

auto ExportUserPacks(const std::vector<AccountUid>& uids, std::vector<std::string>& out_dirs, bool overwrite_existing) -> Result {
    R_UNLESS(!uids.empty(), Result_FsEmpty);
    log_write("[USER] backup start count=%zu overwrite=%d\n", uids.size(), overwrite_existing ? 1 : 0);

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

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));

    for (const auto& r : ready) {
        if (overwrite_existing) {
            for (const auto& p : ListUserPacks()) {
                if (!p.uid_hex.empty() && p.uid_hex == r.hex) {
                    sd.DeleteDirectoryRecursively(p.dir.c_str());
                }
            }
        }
        const auto dir = paths::DATA_ROOT + "/user_packs/" + stamp + "_" + SanitizeName(r.nickname) + "_" + r.hex;
        R_TRY(sd.CreateDirectoryRecursively(dir.c_str()));

        if (!r.jpeg.empty()) {
            R_TRY(sd.write_entire_file((dir + "/avatar.jpg").c_str(), r.jpeg));
        }

        std::string link_status = "none";
        R_TRY(account_link::ExportUserLinkPackage(r.uid, dir, link_status));

        const auto json = std::string{"{\"nickname\":\""} + JsonEscape(r.nickname) +
            "\",\"uid\":\"" + r.hex +
            "\",\"created\":\"" + stamp +
            "\",\"link_status\":\"" + link_status + "\"}";
        R_TRY(sd.write_entire_file((dir + "/profile.json").c_str(),
            std::vector<u8>(json.begin(), json.end())));

        R_TRY(WriteUserReadme(sd, dir, r.nickname, r.hex));
        R_TRY(WriteUserPlaytimeTsv(sd, dir, r.uid, apps, pdm_ok));

        std::vector<PdmPlayEvent> play_events;
        const auto play_rc = account_playtime::CollectUserPlayEvents(r.uid, play_events);
        if (R_FAILED(play_rc)) {
            log_write("[USER] play events collect 0x%X for %s\n", play_rc, r.hex.c_str());
        } else {
            R_TRY(account_playtime::WritePackPlayEvents(dir, play_events));
        }

        out_dirs.push_back(dir);
        log_write("[USER] pack written to %s (link=%s play_events=%zu)\n",
            dir.c_str(), link_status.c_str(), play_events.size());
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

auto FindUserPack(const std::string& dir) -> Pack {
    Pack p;
    fs::FsNativeSd sd;
    auto root = dir;
    if (sd.FileExists((root + "/profile.json").c_str())) {
        p.dir = root;
    } else if (sd.FileExists((root + "/avatar.jpg").c_str()) &&
               (sd.DirExists((root + "/baas").c_str()) || sd.DirExists((root + "/nas").c_str()))) {
        p.dir = root;
    }
    if (p.dir.empty()) {
        return p;
    }

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
    }

    return p;
}

auto ListUserPacks() -> std::vector<Pack> {
    std::vector<Pack> packs;
    fs::FsNativeSd sd;
    const auto root = paths::DATA_ROOT + "/user_packs";
    if (!sd.DirExists(root.c_str())) {
        return packs;
    }
    fs::Dir d;
    if (R_FAILED(sd.OpenDirectory(root.c_str(), FsDirOpenMode_ReadDirs, &d))) {
        return packs;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return packs;
    }
    for (const auto& e : entries) {
        if (e.type != FsDirEntryType_Dir) {
            continue;
        }
        const auto pack_path = root + "/" + e.name;
        const auto pack = FindUserPack(pack_path);
        if (!pack.dir.empty()) {
            packs.push_back(pack);
        }
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) {
        return a.folder_name > b.folder_name;
    });
    return packs;
}

auto DeleteUserPack(const std::string& dir) -> Result {
    const auto root = paths::DATA_ROOT + "/user_packs/";
    R_UNLESS(!dir.empty() && dir.find(root) == 0, Result_FsInvalidType);
    fs::FsNativeSd sd;
    R_TRY(sd.DeleteDirectoryRecursively(dir.c_str()));
    log_write("[USER] deleted pack %s\n", dir.c_str());
    R_SUCCEED();
}

} // namespace sphaira::account_user
