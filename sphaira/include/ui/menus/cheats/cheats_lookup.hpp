#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

enum class InstalledNcaFailureReason {
    None,
    NotInstalled,
    ContentMissing,
    Unavailable,
};

struct InstalledNcaLookupResult {
    std::string build_id;
    InstalledNcaFailureReason failure_reason{InstalledNcaFailureReason::None};
};

enum class BuildIdFailureReason {
    None,
    ProdKeysMissing,
    GameNotFound,
    ExactBuildIdUnavailable,
};

struct BuildIdLookupResult {
    std::string build_id;
    std::string source;
    BuildIdFailureReason failure_reason{BuildIdFailureReason::None};
};

auto GetBuildIdFromInstalledNcaDetailed(u64 title_id) -> InstalledNcaLookupResult;
auto GetBuildIdFromInstalledNca(u64 title_id) -> std::string;
auto GetBuildIdFromGameCardNca(u64 title_id) -> std::string;
auto GetBuildIdFromNso(u64 title_id) -> std::string;
auto HasProdKeys() -> bool;
auto HasApplicationContentMeta(u64 title_id) -> bool;
auto LookupBuildIdForCheats(u64 title_id, bool allow_nso_fallback = true) -> BuildIdLookupResult;

namespace detail {
    auto FormatTitleId(u64 title_id) -> std::string;
    auto FormatTitleIdLower(u64 title_id) -> std::string;
    auto GetBaseApplicationTitleId(u64 title_id) -> u64;
    auto BytesToHex(const u8* data, size_t len) -> std::string;
    auto BytesToBuildId(const u8* data, size_t len) -> std::string;
    auto NormalizeBuildId(std::string build_id) -> std::string;
    auto ReverseBuildIdBytes(std::string build_id) -> std::string;
    auto IsValidBuildId(const std::string& build_id) -> bool;
    auto StringsEqualIgnoreCase(const std::string& a, const std::string& b) -> bool;
}

} // namespace sphaira::ui::menu::hats
