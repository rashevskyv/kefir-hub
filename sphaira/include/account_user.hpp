#pragma once

#include "account_link.hpp"

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::account_user {

struct Pack {
    std::string dir;
    std::string folder_name;
    std::string nickname;
    std::string uid_hex;
    std::string created_label;
    bool has_avatar{};
    bool link_valid{};
    bool has_playtime{};
    u64 nas_id{};
};

auto LoadImageJpeg(const AccountUid& uid, std::vector<u8>& out) -> Result;
auto Rename(const AccountUid& uid, const std::string& nickname) -> Result;
auto SetImageJpeg(const AccountUid& uid, const std::vector<u8>& jpeg) -> Result;
auto Create(const std::string& nickname, AccountUid& out_uid, const std::vector<u8>& jpeg = {}) -> Result;
auto Delete(const AccountUid& uid) -> Result;

auto ExportUserPack(const AccountUid& uid, std::string& out_dir) -> Result;
auto ExportUserPacks(const std::vector<AccountUid>& uids, std::vector<std::string>& out_dirs, bool overwrite_existing = false) -> Result;
auto FindUserPack(const std::string& dir) -> Pack;
auto ListUserPacks() -> std::vector<Pack>;
auto ListUserPacks(const std::string& root) -> std::vector<Pack>;
auto DeleteUserPack(const std::string& dir) -> Result;
auto GetUserPacksRoot() -> std::string;
auto FormatPackCreated(const std::string& folder_name, const std::string& json_created) -> std::string;
auto ReadJsonField(const std::string& json, const char* key) -> std::string;

} // namespace sphaira::account_user
