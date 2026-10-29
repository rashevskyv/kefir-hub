#pragma once

// Homebrew menu folders: which rows the menu shows for one folder of /switch. No libnx, so
// tests/test_homebrew_tree.cpp runs on the host.
//
// A folder that holds an .nro itself is an app folder: the app is listed in the folder above it and the
// folder is never entered (its data folders stay unread). A folder without an .nro of its own is a group:
// the menu shows it as a folder and the user can enter it.

#include "files_transfer_plan.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sphaira::homebrew_tree {

inline constexpr const char* ROOT = "/switch";

struct Folder {
    std::string path{}; // "/switch/games"
    std::string name{}; // "games"
    size_t apps{};      // nros below it
};

struct View {
    std::vector<Folder> folders{};
    std::vector<size_t> apps{}; // indices into the path list given to BuildView
};

inline auto IsUnder(std::string_view path, std::string_view dir) -> bool {
    return path.size() > dir.size() && path.starts_with(dir) && path[dir.size()] == '/';
}

// `paths` are the full paths of every nro found. `cwd` is ROOT or a group folder below it.
// Paths outside ROOT (extra search folders) show as apps in the root view only.
inline auto BuildView(const std::vector<std::string>& paths, std::string_view cwd) -> View {
    View view;
    const std::string_view root{ROOT};

    const auto has_nro_of_its_own = [&](std::string_view dir) {
        for (const auto& p : paths) {
            if (files_transfer::SplitPath(p).first == dir) {
                return true;
            }
        }
        return false;
    };

    for (size_t i = 0; i < paths.size(); i++) {
        const auto dir = files_transfer::SplitPath(paths[i]).first;
        if (dir != root && !IsUnder(dir, root)) {
            if (cwd == root) {
                view.apps.push_back(i);
            }
            continue;
        }
        if (dir == cwd) {
            view.apps.push_back(i);
            continue;
        }
        if (!IsUnder(dir, cwd)) {
            continue;
        }

        const auto rest = std::string_view{dir}.substr(cwd.size() + 1);
        const auto slash = rest.find('/');
        if (slash == std::string_view::npos) {
            view.apps.push_back(i); // the app folder just below cwd
            continue;
        }

        const auto name = std::string{rest.substr(0, slash)};
        const auto folder = std::string{cwd} + "/" + name;
        if (has_nro_of_its_own(folder)) {
            continue; // an app inside an app folder: not shown
        }
        auto it = view.folders.begin();
        while (it != view.folders.end() && it->path != folder) {
            ++it;
        }
        if (it == view.folders.end()) {
            view.folders.push_back({folder, name, 0});
            it = view.folders.end() - 1;
        }
        it->apps++;
    }
    return view;
}

// the folder above `cwd`; ROOT stays ROOT.
inline auto Parent(std::string_view cwd) -> std::string {
    if (!IsUnder(cwd, ROOT)) {
        return ROOT;
    }
    return files_transfer::SplitPath(cwd).first;
}

} // namespace sphaira::homebrew_tree
