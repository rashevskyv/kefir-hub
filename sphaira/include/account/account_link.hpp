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

struct LinkPackage {
    u64 nas_id{};
    std::vector<u8> baas_data;
    std::vector<DonorNasFile> nas_files;
};

using RomfsDonorPackage = LinkPackage;

struct TargetLink {
    AccountUid uid{};
    LinkPackage pkg;
};

auto UidHex(const AccountUid& uid) -> std::string;
// Linkalho baas filename stem (no .dat): used when staging Restore Backup link for TE.
auto UidDashedLinkalho(const AccountUid& uid) -> std::string;
auto UidDashedRfc(const AccountUid& uid) -> std::string;
auto ListUsers() -> std::vector<User>;
auto QueryHorizonLinkStatus(const AccountUid& uid, bool& out_linked) -> Result;
auto QueryNintendoAccountId(const AccountUid& uid, u64& out_nas_id) -> Result;
auto FindLiveUidByNasId(u64 nas_id, AccountUid& out_uid) -> bool;
auto LoadRomfsDonorPackage(RomfsDonorPackage& out_pkg) -> Result;
auto LoadRomfsDonorPackages(std::vector<RomfsDonorPackage>& out_packages) -> Result;
auto LoadUserPackLinkPackage(const std::string& pack_dir, LinkPackage& out_pkg) -> Result;
auto ApplyLinkPackages(const std::vector<TargetLink>& targets, u32& out_linked_count) -> Result;
auto ExportUserLinkPackage(const AccountUid& uid, const std::string& out_dir, std::string& out_link_status, bool may_terminate_account = true) -> Result;
auto LinkAllFromRomfsDonor(u32& out_linked_count) -> Result;
auto UnlinkLinkedProfiles(const std::vector<AccountUid>& uids, u32& out_unlinked_count) -> Result;
auto CanOfferLaunchLink() -> bool;
auto IsLinkGated(std::string& out_reason) -> bool;
void SetLaunchLinkPrompted(bool val = true);
auto ConsumeAccountDaemonsTerminated() -> bool;

} // namespace sphaira::account_link
