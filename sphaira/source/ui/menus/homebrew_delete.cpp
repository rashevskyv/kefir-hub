// Deleting homebrew with what belongs to it: its own folder, and the HOME tiles (forwarders) that start it.

#include "ui/menus/homebrew_delete.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "owo.hpp"
#include "path_util.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <vector>
#include <string>

namespace sphaira::ui::menu::homebrew {
namespace {

// bytes of everything inside a folder, files only; stops at the depth limit instead of following a runaway tree
auto FolderSize(fs::FsNativeSd& fs, const fs::FsPath& path, int depth) -> s64 {
    if (depth > 8) {
        return 0;
    }
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(path, FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &dir))) {
        return 0;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return 0;
    }
    s64 total = 0;
    for (const auto& e : entries) {
        if (e.type == FsDirEntryType_Dir) {
            total += FolderSize(fs, fs::AppendPath(path, e.name), depth + 1);
        } else {
            total += e.file_size;
        }
    }
    return total;
}

// true when the folder holds no other .nro anywhere inside (so deleting it takes nothing else with it)
auto HoldsOnlyThisNro(fs::FsNativeSd& fs, const fs::FsPath& folder, const fs::FsPath& nro, int depth) -> bool {
    if (depth > 8) {
        return false;
    }
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(folder, FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &dir))) {
        return false;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return false;
    }
    for (const auto& e : entries) {
        const auto child = fs::AppendPath(folder, e.name);
        if (e.type == FsDirEntryType_Dir) {
            if (!HoldsOnlyThisNro(fs, child, nro, depth + 1)) {
                return false;
            }
        } else if (path::EndsWithIC(e.name, ".nro") && strcasecmp(child.s, nro.s)) {
            return false;
        }
    }
    return true;
}

} // namespace

auto PlanDelete(const fs::FsPath& nro) -> DeletePlan {
    DeletePlan plan;
    fs::FsNativeSd fs;

    // /switch/<folder>/<anything>.nro: the folder is the app's own when nothing else lives in it
    const std::string p = nro.s;
    constexpr std::string_view root = "/switch/";
    if (p.starts_with(root)) {
        const auto slash = p.find('/', root.size());
        if (slash != std::string::npos) {
            const fs::FsPath folder{p.substr(0, slash)};
            const fs::FsPath exe = App::GetExePath();
            const std::string folder_slash = std::string{folder.s} + "/";
            // never the folder of the running Hub
            if (!std::string_view{exe.s}.starts_with(folder_slash) && HoldsOnlyThisNro(fs, folder, nro, 0)) {
                plan.folder = folder;
                plan.folder_bytes = FolderSize(fs, folder, 0);
            }
        }
    }

    // a tile made by Kefir Hub takes its title id from the path (with or without "sdmc:") and the args that equal it
    for (const auto& spelling : {p, "sdmc:" + p, "\"" + p + "\"", "\"sdmc:" + p + "\""}) {
        const u64 tid = owo_default_title_id(spelling, spelling);
        bool installed = false;
        if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(tid, &installed)) && installed) {
            if (std::find(plan.tiles.begin(), plan.tiles.end(), tid) == plan.tiles.end()) {
                plan.tiles.push_back(tid);
            }
        }
    }
    return plan;
}

void DeleteTiles(const DeletePlan& plan) {
    for (const auto tid : plan.tiles) {
        const auto rc = nsDeleteApplicationCompletely(tid);
        log_write("[Homebrew] deleted HOME tile %016lX: 0x%X\n", tid, rc);
    }
}

} // namespace sphaira::ui::menu::homebrew
