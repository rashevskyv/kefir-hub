#pragma once

#include "account/account_link.hpp"
#include "fs.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace sphaira::account_link {

extern std::atomic<bool> g_daemons_terminated; // set on ProgressBox workers, consumed on the main thread
extern bool g_launch_link_prompted; // main thread only
inline constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;
inline constexpr Result ResultNetworkServiceAccountRegistrationRequired = MAKERESULT(124, 200);

auto ToLowerCopy(std::string s) -> std::string;
auto ToUpperCopy(std::string s) -> std::string;
auto NasHex(u64 nas_id) -> std::string;
auto NasHexShort(u64 nas_id) -> std::string;
auto UidHexRaw(const AccountUid& uid) -> std::string;
auto BaasCandidateNames(const AccountUid& uid) -> std::vector<std::string>;
auto NasPrefixes(u64 nas_id) -> std::vector<std::string>;
auto NasFileMatches(const std::string& name, u64 nas_id) -> bool;

auto TryOpenAccountSave() -> fs::FsNativeSave;
auto OpenAccountSaveWritable() -> fs::FsNativeSave;
void TerminateAccountDaemons();
auto OpenAccountSaveForExport(bool may_terminate_account) -> fs::FsNativeSave;

auto ListDirFiles(fs::Fs& f, const std::string& dir) -> std::vector<std::string>;
auto OpenAccSu(Service* out) -> Result;
auto IsSafeDumpFileName(const std::string& name) -> bool;
auto EndsWith(const std::string& str, const std::string& suffix) -> bool;
auto ResolveSuDirs(fs::Fs& save, std::string& baas_dir, std::string& nas_dir) -> void;
auto BaasStillReferencesNas(fs::Fs& save, const std::string& baas_dir, u64 nas_id) -> bool;

auto QueryIdTokenCacheRaw(Service* srv, u32& out_size) -> Result;
auto QueryIdTokenCache(Service* srv) -> bool;
auto QueryHorizonUserLink(const AccountUid& uid, bool& out_linked, LinkKind& out_kind) -> Result;

auto LoadDonorPackageFromPath(const std::string& base_path, RomfsDonorPackage& out_pkg) -> Result;

} // namespace sphaira::account_link
