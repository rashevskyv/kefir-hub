#pragma once

#include "account/account_user.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <switch.h>

namespace sphaira::ui::menu::users {

struct RemotePackEntry {
    std::string name;
    std::string remote_path;
    bool is_archive{};
    s64 size{};
};

struct ManifestFile {
    std::string remote_path;
    std::string rel_path;
    s64 size{};
};

struct RemoteUserPacksEntry {
    account_user::Pack pack;
    std::vector<u8> avatar_bytes;
    int image{};
    bool selected{};
    bool avatar_decoded{};
};

auto ParseRemoteListResponse(const std::string& json) -> std::optional<std::pair<std::string, std::vector<RemotePackEntry>>>;
auto ParseManifestResponse(const std::string& json, const std::string& remote_pack_root) -> std::optional<std::vector<ManifestFile>>;

void OpenRemoteUserPacks(
    std::string base_url,
    std::vector<RemoteUserPacksEntry> entries,
    std::function<void(std::vector<account_user::Pack>)> on_restore);

} // namespace sphaira::ui::menu::users
