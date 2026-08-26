#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace sphaira::account_link {

enum class LinkKind {
    None,
    Offline,   // baas/nas present, no Nintendo tokens (Linkalho-style)
    Official,  // baas/nas plus id.token / refresh.token
};

struct User {
    AccountUid uid{};
    std::string nickname;
    std::string uid_hex;
    LinkKind kind{LinkKind::None};
    bool linked_known{};
    bool horizon_linked{};
};

auto UidHex(const AccountUid& uid) -> std::string;
auto ListUsers() -> std::vector<User>;
auto LinkUsers(const std::vector<AccountUid>& uids) -> Result;
auto UnlinkUsers(const std::vector<AccountUid>& uids) -> Result;
auto ExportAccountSave(std::string& out_dir, bool terminate_if_needed = false) -> Result;
auto ImportOfficialLink(const std::vector<AccountUid>& uids, const std::string& dump_dir, bool& had_tokens) -> Result;

} // namespace sphaira::account_link
