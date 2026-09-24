#pragma once

#include "account/account_user.hpp"
#include "account/nand_transfer.hpp"
#include "ui/menus/save_menu.hpp"

#include <optional>
#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui::menu::users {

auto CollectSaves(const std::vector<AccountUid>& uids) -> std::vector<save::Entry>;
auto NormUidHex(std::string s) -> std::string;
auto StageNandDump(nand_transfer::Report& report) -> Result;
auto NandDumpLooksComplete(const std::string& dir) -> bool;
auto FindLiveUidForPack(const account_user::Pack& p) -> std::optional<AccountUid>;
auto LiveNameForUid(const AccountUid& uid) -> std::string;
void PickSavesForBackup(std::vector<save::Entry> entries, std::function<void(std::optional<std::vector<save::Entry>>)> cb);
void PickAvatar(std::function<void(std::vector<u8>)> cb);

} // namespace sphaira::ui::menu::users
