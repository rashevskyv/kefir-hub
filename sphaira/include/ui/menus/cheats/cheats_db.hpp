#pragma once

#include "ui/menus/cheats/cheats_lookup.hpp"
#include <switch.h>
#include <string>
#include <unordered_map>
#include <optional>

namespace sphaira::ui::menu::hats {

struct GameCheatInfo;

struct CachedCheatMetadata {
    u64 title_id{};
    std::string name;
    std::string build_id;
    std::string source;
    u32 version{};
    u64 scanned_at{};
};

struct NxDbVersionInfo {
    std::string title;
    std::string build_id;
    u32 latest_version;
};

// Global metadata cache mutex
extern Mutex g_cheat_metadata_cache_mutex;

// Cache operations
auto LoadCheatMetadataCacheUnlocked() -> std::unordered_map<u64, CachedCheatMetadata>;
auto SaveCheatMetadataCacheUnlocked(const std::unordered_map<u64, CachedCheatMetadata>& entries) -> bool;
auto GetCachedCheatMetadata(u64 title_id) -> std::optional<CachedCheatMetadata>;
auto GetCachedBuildIdForTitle(GameCheatInfo& game) -> std::string;
void SaveDetectedBuildIdToCache(const GameCheatInfo& game, const std::string& build_id, const char* source);

// NxDb versions
auto LoadNxDbVersions() -> std::unordered_map<u64, NxDbVersionInfo>;
auto IsNxDbAvailable() -> bool;

} // namespace sphaira::ui::menu::hats
