#pragma once

#include "account_link.hpp"

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::account_user {

struct Pack {
    std::string dir;
    std::string nickname;
    std::string uid_hex;
    bool has_tokens{};
};

auto LoadImageJpeg(const AccountUid& uid, std::vector<u8>& out) -> Result;
auto Rename(const AccountUid& uid, const std::string& nickname) -> Result;
auto SetImageJpeg(const AccountUid& uid, const std::vector<u8>& jpeg) -> Result;
auto Create(const std::string& nickname, AccountUid& out_uid) -> Result;
auto Delete(const AccountUid& uid) -> Result;

auto ExportUserPack(const AccountUid& uid, std::string& out_dir, bool terminate_if_needed = false) -> Result;
auto ExportUserPacks(const std::vector<AccountUid>& uids, std::vector<std::string>& out_dirs, bool terminate_if_needed = false) -> Result;
auto ImportUserPack(const std::string& dir, AccountUid& out_uid) -> Result;
auto FindUserPack(const std::string& dir) -> Pack;

} // namespace sphaira::account_user
