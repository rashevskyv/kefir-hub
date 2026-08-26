#include "account_link.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#include <switch/services/pm.h>

namespace sphaira::account_link {
namespace {

constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;
constexpr u64 TID_BCAT = 0x010000000000000CULL;
constexpr u64 TID_ACCOUNT = 0x010000000000001EULL;
constexpr u64 TID_OLSC = 0x010000000000003EULL;
constexpr u64 BAAS_HEADER2 = 0x0000006E00000001ULL;
constexpr u64 BAAS_HEADER3 = 0x0000000100000001ULL;

auto ToLowerCopy(std::string s) -> std::string {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

auto FileStem(const std::string& name) -> std::string {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return name;
    }
    return name.substr(0, dot);
}

auto RandomU64() -> u64 {
    static std::mt19937_64 rng{std::random_device{}()};
    return rng();
}

auto RandomAlnum(size_t len) -> std::string {
    static constexpr char kChars[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, sizeof(kChars) - 2);
    std::string out(len, '\0');
    for (size_t i = 0; i < len; i++) {
        out[i] = kChars[dist(rng)];
    }
    return out;
}

auto NasHex(u64 nas_id) -> std::string {
    char buf[17]{};
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(nas_id));
    return buf;
}

auto NasHexShort(u64 nas_id) -> std::string {
    char buf[17]{};
    std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(nas_id));
    return buf;
}

// Linkalho's baas filename (last 12 nibbles are high32 then mid16).
auto UidDashedLinkalho(const AccountUid& uid) -> std::string {
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%02x%02x-%08x%04x",
        static_cast<unsigned>(uid.uid[0] & 0xffffffffu),
        static_cast<unsigned>((uid.uid[0] >> 32) & 0xffffu),
        static_cast<unsigned>((uid.uid[0] >> 48) & 0xffffu),
        static_cast<unsigned>(uid.uid[1] & 0xffu),
        static_cast<unsigned>((uid.uid[1] >> 8) & 0xffu),
        static_cast<unsigned>((uid.uid[1] >> 32) & 0xffffffffu),
        static_cast<unsigned>((uid.uid[1] >> 16) & 0xffffu));
    return buf;
}

// Official sample / RFC-style UUID (last 12 nibbles are mid16 then high32).
auto UidDashedRfc(const AccountUid& uid) -> std::string {
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
        static_cast<unsigned>(uid.uid[0] & 0xffffffffu),
        static_cast<unsigned>((uid.uid[0] >> 32) & 0xffffu),
        static_cast<unsigned>((uid.uid[0] >> 48) & 0xffffu),
        static_cast<unsigned>(uid.uid[1] & 0xffffu),
        static_cast<unsigned>((uid.uid[1] >> 16) & 0xffffu),
        static_cast<unsigned>((uid.uid[1] >> 32) & 0xffffffffu));
    return buf;
}

auto UidHexRaw(const AccountUid& uid) -> std::string {
    char buf[33]{};
    std::snprintf(buf, sizeof(buf), "%016llX%016llX",
        static_cast<unsigned long long>(uid.uid[0]),
        static_cast<unsigned long long>(uid.uid[1]));
    return buf;
}

auto BaasCandidates(const AccountUid& uid) -> std::vector<std::string> {
    const auto dashed_l = UidDashedLinkalho(uid);
    const auto dashed_r = UidDashedRfc(uid);
    const auto raw = UidHexRaw(uid);
    return {
        "/baas/" + dashed_r + ".dat",
        "/baas/" + dashed_l + ".dat",
        "/baas/" + raw + ".dat",
        "/baas/" + ToLowerCopy(raw) + ".dat",
        "/baas/" + ToLowerCopy(dashed_r) + ".dat",
        "/baas/" + ToLowerCopy(dashed_l) + ".dat",
    };
}

void ShutdownAccountServices() {
    if (R_FAILED(pmshellInitialize())) {
        return;
    }
    pmshellTerminateProgram(TID_BCAT);
    pmshellTerminateProgram(TID_ACCOUNT);
    pmshellTerminateProgram(TID_OLSC);
    pmshellExit();
    svcSleepThread(200000000);
}

auto OpenAccountSave() -> fs::FsNativeSave {
    ShutdownAccountServices();
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
}

auto TryOpenAccountSave() -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
}

auto ListDirFiles(fs::Fs& f, const std::string& dir) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (!f.DirExists(dir.c_str())) {
        return out;
    }
    fs::Dir d;
    if (R_FAILED(f.OpenDirectory(dir.c_str(), FsDirOpenMode_ReadFiles, &d))) {
        return out;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return out;
    }
    for (const auto& e : entries) {
        if (e.type == FsDirEntryType_File) {
            out.emplace_back(e.name);
        }
    }
    return out;
}

auto FindBaasPath(fs::Fs& acc, const AccountUid& uid) -> std::string {
    for (const auto& p : BaasCandidates(uid)) {
        if (acc.FileExists(p.c_str())) {
            return p;
        }
    }
    return {};
}

auto NasPrefixes(u64 nas_id) -> std::vector<std::string> {
    std::vector<std::string> out;
    out.push_back(NasHex(nas_id));
    const auto sh = NasHexShort(nas_id);
    if (sh != out.front()) {
        out.push_back(sh);
    }
    return out;
}

auto NasFileMatches(const std::string& name, u64 nas_id) -> bool {
    const auto lower = ToLowerCopy(name);
    for (const auto& prefix : NasPrefixes(nas_id)) {
        if (lower.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

void DeleteNasForId(fs::Fs& acc, u64 nas_id) {
    for (const auto& name : ListDirFiles(acc, "/nas")) {
        if (!NasFileMatches(name, nas_id)) {
            continue;
        }
        acc.DeleteFile(("/nas/" + name).c_str());
    }
}

auto NasIdFromBaas(fs::Fs& acc, const std::string& baas_path, u64& nas_id) -> bool {
    std::vector<u8> data;
    if (R_FAILED(acc.read_entire_file(baas_path.c_str(), data)) || data.size() < 24) {
        return false;
    }
    std::memcpy(&nas_id, data.data() + 16, sizeof(nas_id));
    return true;
}

auto HasOfficialTokens(fs::Fs& acc, u64 nas_id) -> bool {
    for (const auto& name : ListDirFiles(acc, "/nas")) {
        if (!NasFileMatches(name, nas_id)) {
            continue;
        }
        const auto lower = ToLowerCopy(name);
        if (lower.find("_id.token") != std::string::npos ||
            lower.find("_refresh.token") != std::string::npos) {
            return true;
        }
    }
    return false;
}

auto BackupAccountSave(fs::Fs& acc) -> Result {
    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto root = paths::DATA_ROOT + "/account_backups/" + stamp;

    for (const char* dir : {"/baas", "/nas"}) {
        if (!acc.DirExists(dir)) {
            continue;
        }
        const auto dst_dir = root + dir;
        sd.CreateDirectoryRecursively(dst_dir.c_str());
        for (const auto& name : ListDirFiles(acc, dir)) {
            std::vector<u8> data;
            const auto src = std::string(dir) + "/" + name;
            if (R_SUCCEEDED(acc.read_entire_file(src.c_str(), data))) {
                sd.write_entire_file((dst_dir + "/" + name).c_str(), data);
            }
        }
    }
    log_write("[ACC] backup written to %s\n", root.c_str());
    R_SUCCEED();
}

auto UnlinkOne(fs::Fs& acc, const AccountUid& uid) -> void {
    const auto baas = FindBaasPath(acc, uid);
    if (!baas.empty()) {
        u64 nas_id{};
        if (NasIdFromBaas(acc, baas, nas_id)) {
            DeleteNasForId(acc, nas_id);
        }
    }
    for (const auto& p : BaasCandidates(uid)) {
        if (acc.FileExists(p.c_str())) {
            acc.DeleteFile(p.c_str());
        }
    }
}

auto ProfileJson(const std::string& nas_hex) -> std::string {
    return std::string{"{\"id\":\""} + nas_hex +
        "\",\"language\":\"en-US\",\"timezone\":\"Europe/London\",\"country\":\"US\""
        ",\"analyticsOptedIn\":false,\"gender\":\"male\",\"emailOptedIn\":false"
        ",\"birthday\":\"1980-01-01\",\"isChild\":false,\"email\":\"-\""
        ",\"screenName\":\"-\",\"region\":\"\",\"loginId\":\"-\",\"nickname\":\"-\""
        ",\"isNnLinked\":false,\"isTwitterLinked\":false,\"isFacebookLinked\":false"
        ",\"isGoogleLinked\":false}";
}

auto LinkOneOffline(fs::Fs& acc, const AccountUid& uid) -> Result {
    UnlinkOne(acc, uid);

    R_TRY(acc.CreateDirectoryRecursively("/baas"));
    R_TRY(acc.CreateDirectoryRecursively("/nas"));

    const u64 account_id = RandomU64();
    const u64 nas_id = RandomU64();
    const u64 baas_user = RandomU64();
    const auto password = RandomAlnum(40);
    const auto nas_hex = NasHex(nas_id);

    std::vector<u8> baas(8 * 5 + 40);
    std::memcpy(baas.data() + 0, &account_id, 8);
    std::memcpy(baas.data() + 8, &BAAS_HEADER2, 8);
    std::memcpy(baas.data() + 16, &nas_id, 8);
    std::memcpy(baas.data() + 24, &BAAS_HEADER3, 8);
    std::memcpy(baas.data() + 32, &baas_user, 8);
    std::memcpy(baas.data() + 40, password.data(), 40);
    const auto baas_path = "/baas/" + UidDashedLinkalho(uid) + ".dat";
    R_TRY(acc.write_entire_file(baas_path.c_str(), baas));

    const auto dat = RandomAlnum(128);
    R_TRY(acc.write_entire_file(("/nas/" + nas_hex + ".dat").c_str(),
        std::vector<u8>(dat.begin(), dat.end())));

    const auto json = ProfileJson(nas_hex);
    R_TRY(acc.write_entire_file(("/nas/" + nas_hex + "_user.json").c_str(),
        std::vector<u8>(json.begin(), json.end())));

    R_SUCCEED();
}

auto CopyFile(fs::Fs& from, const std::string& src, fs::Fs& to, const std::string& dst) -> Result {
    std::vector<u8> data;
    R_TRY(from.read_entire_file(src.c_str(), data));
    R_TRY(to.write_entire_file(dst.c_str(), data));
    R_SUCCEED();
}

auto DirHasBaas(fs::Fs& f, const std::string& dir) -> bool {
    if (!f.DirExists(dir.c_str())) {
        return false;
    }
    for (const auto& name : ListDirFiles(f, dir)) {
        const auto lower = ToLowerCopy(name);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".dat") {
            return true;
        }
    }
    return false;
}

struct DumpLayout {
    std::string baas_dir;
    std::string nas_dir;
};

auto ParentDir(const std::string& path) -> std::string {
    auto p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos) {
        return "/";
    }
    if (slash == 0) {
        return "/";
    }
    return p.substr(0, slash);
}

auto BaseName(const std::string& path) -> std::string {
    auto p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos) {
        return p;
    }
    return p.substr(slash + 1);
}

auto ResolveDumpLayout(fs::Fs& sd, const std::string& path) -> DumpLayout {
    const char* tails[] = {
        "",
        "/su",
        "/save/su",
        "/8000000000000010",
        "/8000000000000010/su",
    };
    for (const auto* tail : tails) {
        const auto base = path + tail;
        const auto baas = base + "/baas";
        const auto nas = base + "/nas";
        if (DirHasBaas(sd, baas) && sd.DirExists(nas.c_str())) {
            return {baas, nas};
        }
    }

    const auto name = ToLowerCopy(BaseName(path));
    if (name == "baas") {
        const auto parent = ParentDir(path);
        const auto nas = parent + "/nas";
        if (DirHasBaas(sd, path) && sd.DirExists(nas.c_str())) {
            return {path, nas};
        }
    }
    return {};
}

auto DumpBaasFiles(fs::Fs& sd, const std::string& baas_dir) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const auto& name : ListDirFiles(sd, baas_dir)) {
        const auto lower = ToLowerCopy(name);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".dat") {
            out.push_back(name);
        }
    }
    return out;
}

auto PickDumpBaas(const std::vector<std::string>& files, const AccountUid& uid) -> std::string {
    std::vector<std::string> want;
    for (const auto& p : BaasCandidates(uid)) {
        want.push_back(ToLowerCopy(BaseName(p)));
    }
    for (const auto& name : files) {
        const auto lower = ToLowerCopy(name);
        if (std::find(want.begin(), want.end(), lower) != want.end()) {
            return name;
        }
    }
    if (!files.empty()) {
        return files.front();
    }
    return {};
}

} // namespace

auto UidHex(const AccountUid& uid) -> std::string {
    return UidDashedRfc(uid);
}

auto ListUsers() -> std::vector<User> {
    std::vector<User> out;
    for (const auto& base : App::GetAccountList()) {
        User u;
        u.uid = base.uid;
        u.nickname = base.nickname;
        u.uid_hex = UidHex(base.uid);
        out.push_back(std::move(u));
    }

    auto save = TryOpenAccountSave();
    const bool known = R_SUCCEEDED(save.GetFsOpenResult());
    for (auto& u : out) {
        u.linked_known = known;
        if (!known) {
            continue;
        }
        const auto baas = FindBaasPath(save, u.uid);
        if (baas.empty()) {
            u.kind = LinkKind::None;
            continue;
        }
        u64 nas_id{};
        if (NasIdFromBaas(save, baas, nas_id) && HasOfficialTokens(save, nas_id)) {
            u.kind = LinkKind::Official;
        } else {
            u.kind = LinkKind::Offline;
        }
    }
    return out;
}

auto LinkUsers(const std::vector<AccountUid>& uids) -> Result {
    R_UNLESS(!uids.empty(), Result_FsEmpty);
    auto save = OpenAccountSave();
    R_TRY(save.GetFsOpenResult());
    BackupAccountSave(save);
    for (const auto& uid : uids) {
        R_TRY(LinkOneOffline(save, uid));
    }
    R_TRY(save.Commit());
    R_SUCCEED();
}

auto UnlinkUsers(const std::vector<AccountUid>& uids) -> Result {
    R_UNLESS(!uids.empty(), Result_FsEmpty);
    auto save = OpenAccountSave();
    R_TRY(save.GetFsOpenResult());
    BackupAccountSave(save);
    for (const auto& uid : uids) {
        UnlinkOne(save, uid);
    }
    R_TRY(save.Commit());
    R_SUCCEED();
}

auto ExportAccountSave(std::string& out_dir, bool terminate_if_needed) -> Result {
    auto save = TryOpenAccountSave();
    if (R_FAILED(save.GetFsOpenResult()) && terminate_if_needed) {
        save = OpenAccountSave();
    }
    R_TRY(save.GetFsOpenResult());

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    out_dir = paths::DATA_ROOT + "/account_export/" + stamp;

    bool any{};
    for (const char* dir : {"/baas", "/nas"}) {
        if (!save.DirExists(dir)) {
            continue;
        }
        const auto dst_dir = out_dir + dir;
        sd.CreateDirectoryRecursively(dst_dir.c_str());
        for (const auto& name : ListDirFiles(save, dir)) {
            R_TRY(CopyFile(save, std::string(dir) + "/" + name, sd, dst_dir + "/" + name));
            any = true;
        }
    }
    R_UNLESS(any, Result_FsEmpty);
    log_write("[ACC] export written to %s\n", out_dir.c_str());
    R_SUCCEED();
}

auto ImportOfficialLink(const std::vector<AccountUid>& uids, const std::string& dump_dir, bool& had_tokens) -> Result {
    had_tokens = false;
    R_UNLESS(!uids.empty(), Result_FsEmpty);
    R_UNLESS(!dump_dir.empty(), Result_FsEmpty);

    fs::FsNativeSd sd;
    const auto layout = ResolveDumpLayout(sd, dump_dir);
    R_UNLESS(!layout.baas_dir.empty(), Result_FsInvalidType);

    const auto dump_baas = DumpBaasFiles(sd, layout.baas_dir);
    R_UNLESS(!dump_baas.empty(), Result_FsInvalidType);

    auto save = OpenAccountSave();
    R_TRY(save.GetFsOpenResult());
    BackupAccountSave(save);
    R_TRY(save.CreateDirectoryRecursively("/baas"));
    R_TRY(save.CreateDirectoryRecursively("/nas"));

    for (const auto& uid : uids) {
        const auto src_name = PickDumpBaas(dump_baas, uid);
        R_UNLESS(!src_name.empty(), Result_FsInvalidType);

        std::vector<u8> baas;
        R_TRY(sd.read_entire_file((layout.baas_dir + "/" + src_name).c_str(), baas));
        R_UNLESS(baas.size() >= 24, Result_FsInvalidType);

        u64 nas_id{};
        std::memcpy(&nas_id, baas.data() + 16, sizeof(nas_id));

        UnlinkOne(save, uid);

        auto dest_name = FileStem(src_name);
        bool matched_name{};
        for (const auto& cand : BaasCandidates(uid)) {
            if (ToLowerCopy(BaseName(cand)) == ToLowerCopy(src_name)) {
                dest_name = FileStem(src_name);
                matched_name = true;
                break;
            }
        }
        if (!matched_name) {
            dest_name = UidDashedRfc(uid);
        }
        R_TRY(save.write_entire_file(("/baas/" + dest_name + ".dat").c_str(), baas));

        for (const auto& name : ListDirFiles(sd, layout.nas_dir)) {
            if (!NasFileMatches(name, nas_id)) {
                continue;
            }
            R_TRY(CopyFile(sd, layout.nas_dir + "/" + name, save, "/nas/" + name));
            const auto lower = ToLowerCopy(name);
            if (lower.find("_id.token") != std::string::npos ||
                lower.find("_refresh.token") != std::string::npos) {
                had_tokens = true;
            }
        }
    }

    R_TRY(save.Commit());
    log_write("[ACC] imported official link from %s (tokens=%d)\n", dump_dir.c_str(), had_tokens ? 1 : 0);
    R_SUCCEED();
}

} // namespace sphaira::account_link
