#include "account_link.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <string>

#include <switch/services/pm.h>

namespace sphaira::account_link {
namespace {

constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;
constexpr u64 TID_BCAT = 0x010000000000000CULL;
constexpr u64 TID_ACCOUNT = 0x010000000000001EULL;
constexpr u64 TID_OLSC = 0x010000000000003EULL;
constexpr u64 BAAS_HEADER2 = 0x0000006E00000001ULL;
constexpr u64 BAAS_HEADER3 = 0x0000000100000001ULL;

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
    attr.save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
}

auto TryOpenAccountSave() -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
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
        fs::Dir d;
        if (R_FAILED(acc.OpenDirectory(dir, FsDirOpenMode_ReadFiles, &d))) {
            continue;
        }
        std::vector<FsDirectoryEntry> entries;
        if (R_FAILED(d.ReadAll(entries))) {
            continue;
        }
        const auto dst_dir = root + dir;
        sd.CreateDirectoryRecursively(dst_dir.c_str());
        for (const auto& e : entries) {
            if (e.type != FsDirEntryType_File) {
                continue;
            }
            std::vector<u8> data;
            const auto src = std::string(dir) + "/" + e.name;
            if (R_SUCCEEDED(acc.read_entire_file(src.c_str(), data))) {
                sd.write_entire_file((dst_dir + "/" + e.name).c_str(), data);
            }
        }
    }
    log_write("[ACC] backup written to %s\n", root.c_str());
    R_SUCCEED();
}

auto DeleteNasForBaas(fs::Fs& acc, const std::string& baas_path) -> void {
    std::vector<u8> data;
    if (R_FAILED(acc.read_entire_file(baas_path.c_str(), data)) || data.size() < 24) {
        return;
    }
    u64 nas_id{};
    std::memcpy(&nas_id, data.data() + 16, sizeof(nas_id));
    const auto hex = NasHex(nas_id);
    const auto dat = "/nas/" + hex + ".dat";
    const auto json = "/nas/" + hex + "_user.json";
    if (acc.FileExists(dat.c_str())) {
        acc.DeleteFile(dat.c_str());
    }
    if (acc.FileExists(json.c_str())) {
        acc.DeleteFile(json.c_str());
    }
}

auto UnlinkOne(fs::Fs& acc, const std::string& uid_hex) -> void {
    const auto baas = "/baas/" + uid_hex + ".dat";
    if (acc.FileExists(baas.c_str())) {
        DeleteNasForBaas(acc, baas);
        acc.DeleteFile(baas.c_str());
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

auto LinkOne(fs::Fs& acc, const std::string& uid_hex) -> Result {
    UnlinkOne(acc, uid_hex);

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
    R_TRY(acc.write_entire_file(("/baas/" + uid_hex + ".dat").c_str(), baas));

    const auto dat = RandomAlnum(128);
    R_TRY(acc.write_entire_file(("/nas/" + nas_hex + ".dat").c_str(),
        std::vector<u8>(dat.begin(), dat.end())));

    const auto json = ProfileJson(nas_hex);
    R_TRY(acc.write_entire_file(("/nas/" + nas_hex + "_user.json").c_str(),
        std::vector<u8>(json.begin(), json.end())));

    R_SUCCEED();
}

} // namespace

auto UidHex(const AccountUid& uid) -> std::string {
    char buf[33]{};
    std::snprintf(buf, sizeof(buf), "%016lX%016lX", uid.uid[0], uid.uid[1]);
    return buf;
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
        if (known) {
            u.linked = save.FileExists(("/baas/" + u.uid_hex + ".dat").c_str());
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
        R_TRY(LinkOne(save, UidHex(uid)));
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
        UnlinkOne(save, UidHex(uid));
    }
    R_TRY(save.Commit());
    R_SUCCEED();
}

} // namespace sphaira::account_link
