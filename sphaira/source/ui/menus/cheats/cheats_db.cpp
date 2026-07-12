#include "ui/menus/cheats/cheats_db.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats_menu.hpp"

#include "fs.hpp"
#include "log.hpp"
#include "yyjson_helper.hpp"

#include <yyjson.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <optional>
#include <ranges>
#include <sstream>
#include <switch.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>

namespace sphaira::ui::menu::hats {

using namespace detail;

namespace {

constexpr const char* NX_DB_PATH = "/config/hats-tools/cheats-db";
constexpr const char* NX_DB_VERSIONS_FILE = "versions.json";
constexpr const char* CHEAT_METADATA_CACHE_PATH = "/config/hats-tools/cheat-metadata.json";
constexpr u32 CHEAT_METADATA_CACHE_VERSION = 1;

} // namespace

Mutex g_cheat_metadata_cache_mutex{};

auto LoadCheatMetadataCacheUnlocked() -> std::unordered_map<u64, CachedCheatMetadata> {
    std::unordered_map<u64, CachedCheatMetadata> entries;

    fs::FsNativeSd fs;
    if (!fs.FileExists(CHEAT_METADATA_CACHE_PATH)) {
        return entries;
    }

    std::vector<u8> data;
    if (R_FAILED(fs.read_entire_file(CHEAT_METADATA_CACHE_PATH, data))) {
        log_write("[Cheats] Failed to read cheat metadata cache\n");
        return entries;
    }

    data.push_back(0);
    auto* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size() - 1, YYJSON_READ_NOFLAG);
    if (!doc) {
        log_write("[Cheats] Failed to parse cheat metadata cache JSON\n");
        return entries;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return entries;
    }

    auto* titles = yyjson_obj_get(root, "titles");
    if (!titles || !yyjson_is_arr(titles)) {
        return entries;
    }

    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(titles, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            continue;
        }

        CachedCheatMetadata entry;

        if (auto* val = yyjson_obj_get(item, "title_id"); val && yyjson_is_str(val)) {
            const auto* title_id_str = yyjson_get_str(val);
            unsigned long long title_id = 0;
            if (!title_id_str || std::sscanf(title_id_str, "%016llx", &title_id) != 1) {
                continue;
            }
            entry.title_id = title_id;
        } else {
            continue;
        }

        if (auto* val = yyjson_obj_get(item, "name"); val && yyjson_is_str(val)) {
            entry.name = yyjson_get_str(val);
        }
        if (auto* val = yyjson_obj_get(item, "build_id"); val && yyjson_is_str(val)) {
            entry.build_id = NormalizeBuildId(yyjson_get_str(val));
        }
        if (auto* val = yyjson_obj_get(item, "source"); val && yyjson_is_str(val)) {
            entry.source = yyjson_get_str(val);
        }
        if (auto* val = yyjson_obj_get(item, "version"); val && yyjson_is_uint(val)) {
            entry.version = static_cast<u32>(yyjson_get_uint(val));
        }
        if (auto* val = yyjson_obj_get(item, "scanned_at"); val && yyjson_is_uint(val)) {
            entry.scanned_at = yyjson_get_uint(val);
        }

        entries[entry.title_id] = std::move(entry);
    }

    return entries;
}

auto SaveCheatMetadataCacheUnlocked(const std::unordered_map<u64, CachedCheatMetadata>& entries) -> bool {
    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively("/config/hats-tools");

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "cache_version", CHEAT_METADATA_CACHE_VERSION);
    yyjson_mut_obj_add_uint(doc, root, "generated_at", static_cast<u64>(std::time(nullptr)));

    auto* titles = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "titles", titles);

    std::vector<const CachedCheatMetadata*> sorted_entries;
    sorted_entries.reserve(entries.size());
    for (const auto& [title_id, entry] : entries) {
        if (title_id == 0) {
            continue;
        }
        sorted_entries.push_back(&entry);
    }

    std::sort(sorted_entries.begin(), sorted_entries.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->title_id < rhs->title_id;
    });

    for (const auto* entry : sorted_entries) {
        auto* obj = yyjson_mut_obj(doc);
        yyjson_mut_arr_add_val(titles, obj);
        const auto title_id = FormatTitleId(entry->title_id);
        yyjson_mut_obj_add_strncpy(doc, obj, "title_id", title_id.c_str(), title_id.size());
        yyjson_mut_obj_add_strncpy(doc, obj, "name", entry->name.c_str(), entry->name.size());
        yyjson_mut_obj_add_uint(doc, obj, "version", entry->version);
        yyjson_mut_obj_add_strncpy(doc, obj, "build_id", entry->build_id.c_str(), entry->build_id.size());
        yyjson_mut_obj_add_strncpy(doc, obj, "source", entry->source.c_str(), entry->source.size());
        yyjson_mut_obj_add_uint(doc, obj, "scanned_at", entry->scanned_at);
    }

    char* json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE, nullptr);
    if (!json) {
        return false;
    }
    ON_SCOPE_EXIT(free(json));

    const auto json_data = std::vector<u8>(
        reinterpret_cast<const u8*>(json),
        reinterpret_cast<const u8*>(json) + std::strlen(json)
    );
    const auto rc = fs.write_entire_file(CHEAT_METADATA_CACHE_PATH, json_data);
    if (R_FAILED(rc)) {
        log_write("[Cheats] Failed to write cheat metadata cache: %x\n", rc);
        return false;
    }

    return true;
}

auto GetCachedCheatMetadata(u64 title_id) -> std::optional<CachedCheatMetadata> {
    mutexLock(&g_cheat_metadata_cache_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));

    auto entries = LoadCheatMetadataCacheUnlocked();
    if (const auto it = entries.find(title_id); it != entries.end()) {
        return it->second;
    }

    return std::nullopt;
}

auto GetCachedBuildIdForTitle(GameCheatInfo& game) -> std::string {
    const auto cached = GetCachedCheatMetadata(game.title_id);
    if (!cached || !IsValidBuildId(cached->build_id)) {
        return "";
    }

    if (game.name.empty() && !cached->name.empty()) {
        game.name = cached->name;
    }
    if (!game.version && cached->version) {
        game.version = cached->version;
    }

    log_write("[Cheats] Using cached Build ID %s for title %016lx (source=%s)\n",
              cached->build_id.c_str(), game.title_id, cached->source.c_str());
    return cached->build_id;
}

void SaveDetectedBuildIdToCache(const GameCheatInfo& game, const std::string& build_id, const char* source) {
    const auto normalized_build_id = NormalizeBuildId(build_id);
    if (!IsValidBuildId(normalized_build_id)) {
        return;
    }

    mutexLock(&g_cheat_metadata_cache_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));

    auto entries = LoadCheatMetadataCacheUnlocked();
    auto& entry = entries[game.title_id];
    entry.title_id = game.title_id;
    if (!game.name.empty()) {
        entry.name = game.name;
    } else if (entry.name.empty()) {
        entry.name = std::format("Game {:016X}", game.title_id);
    }
    entry.version = game.version;
    entry.build_id = normalized_build_id;
    entry.source = source ? source : "unknown";
    entry.scanned_at = static_cast<u64>(std::time(nullptr));

    if (!SaveCheatMetadataCacheUnlocked(entries)) {
        log_write("[Cheats] Failed to persist detected Build ID cache update for %016lx\n", game.title_id);
    }
}

auto LoadNxDbVersions() -> std::unordered_map<u64, NxDbVersionInfo> {
    std::unordered_map<u64, NxDbVersionInfo> version_map;

    fs::FsNativeSd fs;
    fs::FsPath versions_path;
    std::snprintf(versions_path, sizeof(versions_path), "%s/%s", NX_DB_PATH, NX_DB_VERSIONS_FILE);

    log_write("[Cheats] Loading nx-cheats-db versions from: %s\n", versions_path.s);

    if (!fs.FileExists(versions_path)) {
        log_write("[Cheats] nx-cheats-db versions.json not found\n");
        return version_map;
    }

    std::vector<u8> data;
    Result rc = fs.read_entire_file(versions_path, data);
    if (R_FAILED(rc)) {
        log_write("[Cheats] Failed to read versions.json: %x\n", rc);
        return version_map;
    }

    data.push_back(0); // Null-terminate
    const auto data_len = std::strlen(reinterpret_cast<char*>(data.data()));

    yyjson_doc* doc = yyjson_read(reinterpret_cast<char*>(data.data()), data_len, 0);
    if (!doc) {
        log_write("[Cheats] Failed to parse versions.json\n");
        return version_map;
    }

    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        log_write("[Cheats] versions.json root is not an object\n");
        return version_map;
    }

    // Parse each title entry
    yyjson_val* key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);

    while ((key = yyjson_obj_iter_next(&iter))) {
        const char* title_id_str = yyjson_get_str(key);
        yyjson_val* title_obj = yyjson_obj_iter_get_val(key);

        if (!title_id_str || !yyjson_is_obj(title_obj)) {
            continue;
        }

        // Parse title ID
        u64 title_id = 0;
        if (sscanf(title_id_str, "%016llx", (unsigned long long*)&title_id) != 1) {
            continue;
        }

        NxDbVersionInfo info;

        // Get title name
        yyjson_val* title_val = yyjson_obj_get(title_obj, "title");
        if (title_val && yyjson_is_str(title_val)) {
            info.title = yyjson_get_str(title_val);
        }

        // Get latest version
        yyjson_val* latest_val = yyjson_obj_get(title_obj, "latest");
        if (latest_val && yyjson_is_uint(latest_val)) {
            info.latest_version = yyjson_get_uint(latest_val);

            // Get build ID for latest version
            std::string version_key = std::to_string(info.latest_version);
            yyjson_val* build_id_val = yyjson_obj_get(title_obj, version_key.c_str());
            if (build_id_val && yyjson_is_str(build_id_val)) {
                info.build_id = yyjson_get_str(build_id_val);
            }
        }

        // If no latest version, try version 0
        if (info.build_id.empty()) {
            yyjson_val* build_id_val = yyjson_obj_get(title_obj, "0");
            if (build_id_val && yyjson_is_str(build_id_val)) {
                info.build_id = yyjson_get_str(build_id_val);
                info.latest_version = 0;
            }
        }

        if (!info.build_id.empty()) {
            version_map[title_id] = std::move(info);
            log_write("[Cheats] nx-cheats-db: %016llx - %s (v%u, %s)\n",
                      (unsigned long long)title_id, info.title.c_str(),
                      info.latest_version, info.build_id.c_str());
        }
    }

    log_write("[Cheats] Loaded %zu titles from nx-cheats-db versions.json\n", version_map.size());
    return version_map;
}

auto IsNxDbAvailable() -> bool {
    fs::FsNativeSd fs;
    fs::FsPath versions_path;
    std::snprintf(versions_path, sizeof(versions_path), "%s/%s", NX_DB_PATH, NX_DB_VERSIONS_FILE);
    return fs.FileExists(versions_path);
}

} // namespace sphaira::ui::menu::hats
