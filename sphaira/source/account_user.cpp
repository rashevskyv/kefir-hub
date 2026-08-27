#include "account_user.hpp"
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

auto BaasMatchesUid(const char* name, const std::string& uid_hex) -> bool {
    std::string stem = name;
    const auto dot = stem.find('.');
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    std::string compact;
    for (char c : stem) {
        if (c != '-') {
            compact += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    std::string want;
    for (char c : uid_hex) {
        if (c != '-') {
            want += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return compact == want;
}

auto CopyLinkFiles(fs::FsNativeSd& sd, const std::string& dump_dir, const std::string& out_dir, const std::string& uid_hex) -> void {
    const auto baas_src = dump_dir + "/baas";
    const auto nas_src = dump_dir + "/nas";
    sd.CreateDirectoryRecursively((out_dir + "/baas").c_str());
    sd.CreateDirectoryRecursively((out_dir + "/nas").c_str());
    fs::Dir d;
    if (!sd.DirExists(baas_src.c_str()) || R_FAILED(sd.OpenDirectory(baas_src.c_str(), FsDirOpenMode_ReadFiles, &d))) {
        return;
    }
    std::vector<FsDirectoryEntry> entries;
    d.ReadAll(entries);
    for (const auto& e : entries) {
        if (e.type != FsDirEntryType_File || !BaasMatchesUid(e.name, uid_hex)) {
            continue;
        }
        std::vector<u8> buf;
        if (R_FAILED(sd.read_entire_file((baas_src + "/" + e.name).c_str(), buf))) {
            continue;
        }
        sd.write_entire_file((out_dir + "/baas/" + e.name).c_str(), buf);
        if (buf.size() < 24) {
            continue;
        }
        u64 nas_id{};
        std::memcpy(&nas_id, buf.data() + 16, sizeof(nas_id));
        char nas_hex[17]{};
        std::snprintf(nas_hex, sizeof(nas_hex), "%016llx", static_cast<unsigned long long>(nas_id));
        fs::Dir nd;
        if (!sd.DirExists(nas_src.c_str()) || R_FAILED(sd.OpenDirectory(nas_src.c_str(), FsDirOpenMode_ReadFiles, &nd))) {
            continue;
        }
        std::vector<FsDirectoryEntry> nas_ents;
        nd.ReadAll(nas_ents);
        for (const auto& n : nas_ents) {
            if (n.type != FsDirEntryType_File) {
                continue;
            }
            std::string nn = n.name;
            for (auto& c : nn) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (nn.rfind(nas_hex, 0) != 0) {
                continue;
            }
            std::vector<u8> nb;
            if (R_SUCCEEDED(sd.read_entire_file((nas_src + "/" + n.name).c_str(), nb))) {
                sd.write_entire_file((out_dir + "/nas/" + n.name).c_str(), nb);
            }
        }
    }
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
                NsApplicationControlData control{};
                u64 actual_size = 0;
                if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, &control, sizeof(control), &actual_size))) {
                    NacpLanguageEntry* lang = nullptr;
                    if (R_SUCCEEDED(nacpGetLanguageEntry(&control.nacp, &lang)) && lang) {
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

void WriteUserReadme(fs::FsNativeSd& sd, const std::string& dir, const std::string& nickname, const std::string& hex) {
    const std::string readme =
        "User Backup Pack\n"
        "================\n\n"
        "This folder contains the exported backup for user profile: " + nickname + " (" + hex + ").\n\n"
        "Contents:\n"
        "- profile.json: Profile metadata (nickname and original UID).\n"
        "- avatar.jpg: User profile avatar icon.\n"
        "- baas/, nas/: Official or offline Nintendo Account link tokens and registration data (if linked).\n"
        "- playtime.tsv: Exported play statistics (playtime, launch counts, timestamps) for installed applications.\n"
        "- saves/: User-selected game save data backups.\n\n"
        "Notes:\n"
        "1. Game saves are stored as ordinary compatible ZIP backups under the saves/ folder.\n"
        "2. playtime.tsv is an inspection export only in this phase.\n"
        "3. This phase exports data only; it does not yet restore game saves or playtime statistics to a new user account ID.\n"
        "4. Unavailable or failing PDM play statistics queries are non-fatal and do not prevent profile or save backup.\n";

    sd.write_entire_file((dir + "/README.txt").c_str(),
        std::vector<u8>(readme.begin(), readme.end()));
}

void WriteUserPlaytimeTsv(fs::FsNativeSd& sd, const std::string& dir, const AccountUid& uid, const std::vector<AppInfo>& apps, bool pdm_ok) {
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

    sd.write_entire_file((dir + "/playtime.tsv").c_str(),
        std::vector<u8>(tsv.begin(), tsv.end()));
}

auto ExportUserPacks(const std::vector<AccountUid>& uids, std::vector<std::string>& out_dirs) -> Result {
    R_UNLESS(!uids.empty(), Result_FsEmpty);

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

    std::string dump_dir;
    account_link::ExportAccountSave(dump_dir);

    title::Init();
    const auto apps = CollectInstalledApps();
    title::Exit();

    const bool pdm_ok = R_SUCCEEDED(pdmqryInitialize());

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));

    for (const auto& r : ready) {
        const auto dir = paths::DATA_ROOT + "/user_packs/" + stamp + "_" + SanitizeName(r.nickname) + "_" + r.hex;
        sd.CreateDirectoryRecursively(dir.c_str());
        const auto json = std::string{"{\"nickname\":\""} + JsonEscape(r.nickname) +
            "\",\"uid\":\"" + r.hex + "\"}";
        sd.write_entire_file((dir + "/profile.json").c_str(),
            std::vector<u8>(json.begin(), json.end()));
        if (!r.jpeg.empty()) {
            sd.write_entire_file((dir + "/avatar.jpg").c_str(), r.jpeg);
        }
        if (!dump_dir.empty()) {
            CopyLinkFiles(sd, dump_dir, dir, r.hex);
        }
        WriteUserReadme(sd, dir, r.nickname, r.hex);
        WriteUserPlaytimeTsv(sd, dir, r.uid, apps, pdm_ok);

        out_dirs.push_back(dir);
        log_write("[USER] pack written to %s\n", dir.c_str());
    }

    if (pdm_ok) {
        pdmqryExit();
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

    std::vector<u8> json;
    if (R_SUCCEEDED(sd.read_entire_file((p.dir + "/profile.json").c_str(), json))) {
        const std::string s(json.begin(), json.end());
        p.nickname = ReadJsonField(s, "nickname");
        p.uid_hex = ReadJsonField(s, "uid");
    }
    fs::Dir nd;
    if (sd.DirExists((p.dir + "/nas").c_str()) &&
        R_SUCCEEDED(sd.OpenDirectory((p.dir + "/nas").c_str(), FsDirOpenMode_ReadFiles, &nd))) {
        std::vector<FsDirectoryEntry> ents;
        nd.ReadAll(ents);
        for (const auto& e : ents) {
            std::string n = e.name;
            for (auto& c : n) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (n.find("_id.token") != std::string::npos || n.find("_refresh.token") != std::string::npos) {
                p.has_tokens = true;
                break;
            }
        }
    }
    return p;
}

auto ImportUserPack(const std::string& dir, AccountUid& out_uid) -> Result {
    const auto pack = FindUserPack(dir);
    R_UNLESS(!pack.dir.empty(), Result_FsInvalidType);

    fs::FsNativeSd sd;
    std::string nickname = pack.nickname;
    if (nickname.empty()) {
        nickname = "User";
    }
    R_TRY(Create(nickname, out_uid));

    std::vector<u8> jpeg;
    if (R_SUCCEEDED(sd.read_entire_file((pack.dir + "/avatar.jpg").c_str(), jpeg)) && !jpeg.empty()) {
        auto normalized = ImageNormalizeIcon(jpeg);
        if (normalized.empty()) {
            normalized = jpeg;
        }
        SetImageJpeg(out_uid, normalized);
    }

    R_SUCCEED();
}

} // namespace sphaira::account_user
