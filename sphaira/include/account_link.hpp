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
auto QueryHorizonLinkStatus(const AccountUid& uid, bool& out_linked) -> Result;
auto QueryNintendoAccountId(const AccountUid& uid, u64& out_nas_id) -> Result;
auto ExportAccountSave(std::string& out_dir) -> Result;
auto ValidateLinkPackage(const std::string& pkg_dir, u64& out_nas_id, std::vector<std::string>& out_nas_files) -> Result;
auto PrepareOfficialLinkExport(const AccountUid& uid, std::string& out_pkg_dir) -> Result;
auto PrepareOfficialLinkApply(const AccountUid& target_uid, const std::string& pkg_dir) -> Result;
auto PrepareOfficialLinkLayoutProbe(const AccountUid& uid) -> Result;
auto PrepareAccountSaveDump(bool& out_rebooted) -> Result;

} // namespace sphaira::account_link
