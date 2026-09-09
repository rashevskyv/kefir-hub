#include "ui/menus/users/users_internal.hpp"

#include "account/account_link.hpp"
#include "account/account_restore.hpp"
#include "app_paths.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "title_info.hpp"
#include "ui/menus/save/save_menu_detail.hpp"

#include <cctype>
#include <ctime>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {

auto CollectSaves(const std::vector<AccountUid>& uids) -> std::vector<save::Entry> {
    title::Init();
    std::vector<save::Entry> out;
    for (const auto& uid : uids) {
        auto part = save::Menu::ListAccountSaves(uid);
        out.insert(out.end(), part.begin(), part.end());
    }
    for (auto& e : out) {
        save::detail::LoadControlEntry(e);
        e.selected = true;
    }
    title::Exit();
    return out;
}

auto NormUidHex(std::string s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c != '-') {
            out += static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

auto StageNandDump(nand_transfer::Report& report) -> Result {
    fs::FsNativeSd sd;
    if (report.dir.empty()) {
        char stamp[32]{};
        const auto t = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
        report.dir = std::string(paths::DATA_ROOT) + "/nand_transfer/_staging_" + stamp;
    }
    R_TRY(sd.CreateDirectoryRecursively(report.dir.c_str()));
    R_TRY(sd.CreateDirectoryRecursively(account_restore::PendingDir()));
    sd.DeleteFile(account_restore::DumpedOkPath());
    sd.DeleteFile(account_restore::NandRestoredOkPath());
    R_TRY(account_restore::WriteNandFlag());
    {
        const std::vector<u8> body(report.dir.begin(), report.dir.end());
        R_TRY(sd.write_entire_file(account_restore::NandPackPath(), body));
    }
    R_TRY(account_restore::SavePending({report.dir}, "wait_nand_dump", report.save_0010));
    account_restore::WriteStartupTe(account_restore::NandDumpTeName());
    log_write("[NAND] staged TE dump pack=%s 0010=%d F0=%d\n",
        report.dir.c_str(), report.save_0010 ? 1 : 0, report.save_00F0 ? 1 : 0);
    R_SUCCEED();
}

auto NandDumpLooksComplete(const std::string& dir) -> bool {
    if (dir.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (sd.FileExists(account_restore::DumpedOkPath())) {
        return sd.DirExists((dir + "/8000000000000010").c_str()) ||
               sd.DirExists((dir + "/80000000000000F0").c_str());
    }
    return sd.DirExists((dir + "/8000000000000010").c_str()) &&
           sd.DirExists((dir + "/80000000000000F0").c_str());
}

auto FindLiveUidForPack(const account_user::Pack& p) -> std::optional<AccountUid> {
    const auto users = account_link::ListUsers();
    if (!p.uid_hex.empty()) {
        const auto want = NormUidHex(p.uid_hex);
        for (const auto& u : users) {
            if (!u.uid_hex.empty() && NormUidHex(u.uid_hex) == want) {
                log_write("[USER] Replace: pack uid %s is already on this console\n", p.uid_hex.c_str());
                return u.uid;
            }
        }
    }
    if (p.nas_id != 0) {
        AccountUid by_nas{};
        if (account_link::FindLiveUidByNasId(p.nas_id, by_nas)) {
            log_write("[USER] Replace: pack nas %llx proven on live uid\n",
                static_cast<unsigned long long>(p.nas_id));
            return by_nas;
        }
        log_write("[USER] Create: pack nas %llx not proven on this console\n",
            static_cast<unsigned long long>(p.nas_id));
    }
    return std::nullopt;
}

auto LiveNameForUid(const AccountUid& uid) -> std::string {
    for (const auto& u : account_link::ListUsers()) {
        if (u.uid.uid[0] == uid.uid[0] && u.uid.uid[1] == uid.uid[1]) {
            return u.nickname;
        }
    }
    return {};
}

} // namespace sphaira::ui::menu::users
