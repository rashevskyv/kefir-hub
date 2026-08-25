#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace sphaira::account_link {

struct User {
    AccountUid uid{};
    std::string nickname;
    std::string uid_hex;
    bool linked{};
    bool linked_known{};
};

auto UidHex(const AccountUid& uid) -> std::string;
auto ListUsers() -> std::vector<User>;
auto LinkUsers(const std::vector<AccountUid>& uids) -> Result;
auto UnlinkUsers(const std::vector<AccountUid>& uids) -> Result;

} // namespace sphaira::account_link
