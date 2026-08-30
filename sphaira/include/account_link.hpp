#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace sphaira::account_link {

enum class LinkKind {
    None,
    Offline,   // Horizon-linked account without locally available official-token proof
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

struct DonorNasFile {
    std::string filename;
    std::vector<u8> data;
};

struct RomfsDonorPackage {
    u64 nas_id{};
    std::vector<u8> baas_data;
    std::vector<DonorNasFile> nas_files;
};

auto UidHex(const AccountUid& uid) -> std::string;
auto ListUsers() -> std::vector<User>;
auto QueryHorizonLinkStatus(const AccountUid& uid, bool& out_linked) -> Result;
auto QueryNintendoAccountId(const AccountUid& uid, u64& out_nas_id) -> Result;
auto ExportAccountSave(std::string& out_dir) -> Result;
auto LoadRomfsDonorPackage(RomfsDonorPackage& out_pkg) -> Result;
auto LinkAllFromRomfsDonor(u32& out_linked_count) -> Result;
auto UnlinkLinkedProfiles(const std::vector<AccountUid>& uids, u32& out_unlinked_count) -> Result;
auto CanOfferLaunchLink() -> bool;
auto IsLinkGated(std::string& out_reason) -> bool;
void SetLaunchLinkPrompted(bool val = true);

} // namespace sphaira::account_link
