#pragma once

// Homebrew menu folders are virtual: a folder is a tag, an app can carry several, and the top level
// ("Homebrew", id ROOT) is a tag too. Nothing on the SD card moves: many apps look for their files at a
// fixed place under /switch. No libnx, so tests/test_homebrew_vfolders.cpp runs on the host.
//
// /config/kefir/homebrew_folders.txt, one record per line, tab separated:
//   F <TAB> id <TAB> name      a folder
//   A <TAB> id <TAB> nro path  the app is in folder id (ROOT = the top level)
// An app with no A line is on the top level only.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::hb_folders {

inline constexpr uint32_t ROOT = 0;
inline constexpr size_t PREVIEW_MAX = 9; // a folder without its own icon shows up to 3x3 app icons

struct Folder {
    uint32_t id{};
    std::string name{};
};

struct Member {
    uint32_t id{};
    std::string path{};
};

struct Store {
    std::vector<Folder> folders{};
    std::vector<Member> members{};
};

struct View {
    std::vector<uint32_t> folders{}; // top level only
    std::vector<size_t> counts{};    // apps in each of `folders`
    std::vector<std::vector<size_t>> previews{}; // the first PREVIEW_MAX apps of each of `folders`
    std::vector<size_t> apps{};      // indices into the path list given to BuildView
};

// a name the file format can hold and the user can read
inline auto IsValidName(std::string_view name) -> bool {
    if (name.empty() || name.find_first_not_of(' ') == std::string_view::npos) {
        return false;
    }
    return name.find_first_of("\t\r\n") == std::string_view::npos;
}

inline auto FindFolder(const Store& s, uint32_t id) -> const Folder* {
    for (const auto& f : s.folders) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

inline auto FindFolderByName(const Store& s, std::string_view name) -> const Folder* {
    for (const auto& f : s.folders) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

// the folders the app is in; ROOT when it has none
inline auto FoldersOf(const Store& s, std::string_view path) -> std::vector<uint32_t> {
    std::vector<uint32_t> out;
    for (const auto& m : s.members) {
        if (m.path == path) {
            out.push_back(m.id);
        }
    }
    if (out.empty()) {
        out.push_back(ROOT);
    }
    return out;
}

inline auto IsIn(const Store& s, std::string_view path, uint32_t id) -> bool {
    const auto f = FoldersOf(s, path);
    return std::find(f.begin(), f.end(), id) != f.end();
}

// an empty set means the top level; the top level alone needs no record
inline void SetFolders(Store& s, std::string_view path, std::vector<uint32_t> ids) {
    std::erase_if(s.members, [&](const Member& m) { return m.path == path; });
    if (ids.empty() || (ids.size() == 1 && ids[0] == ROOT)) {
        return;
    }
    for (const auto id : ids) {
        s.members.push_back({id, std::string{path}});
    }
}

// copy: the app stays where it is and is also in `to`
inline void Add(Store& s, std::string_view path, uint32_t to) {
    auto ids = FoldersOf(s, path);
    if (std::find(ids.begin(), ids.end(), to) == ids.end()) {
        ids.push_back(to);
    }
    SetFolders(s, path, std::move(ids));
}

// out of `from`; an app left in no folder goes back to the top level
inline void Remove(Store& s, std::string_view path, uint32_t from) {
    auto ids = FoldersOf(s, path);
    std::erase(ids, from);
    SetFolders(s, path, std::move(ids));
}

// cut + paste: out of `from`, into `to`
inline void Move(Store& s, std::string_view path, uint32_t from, uint32_t to) {
    auto ids = FoldersOf(s, path);
    std::erase(ids, from);
    if (std::find(ids.begin(), ids.end(), to) == ids.end()) {
        ids.push_back(to);
    }
    SetFolders(s, path, std::move(ids));
}

// returns the id of the new folder, or of the folder that already has this name; 0 for a bad name
inline auto CreateFolder(Store& s, std::string_view name) -> uint32_t {
    if (!IsValidName(name)) {
        return ROOT;
    }
    if (const auto f = FindFolderByName(s, name)) {
        return f->id;
    }
    uint32_t id = 1;
    for (const auto& f : s.folders) {
        id = std::max(id, f.id + 1);
    }
    s.folders.push_back({id, std::string{name}});
    return id;
}

inline auto RenameFolder(Store& s, uint32_t id, std::string_view name) -> bool {
    if (!IsValidName(name)) {
        return false;
    }
    if (const auto other = FindFolderByName(s, name); other && other->id != id) {
        return false;
    }
    for (auto& f : s.folders) {
        if (f.id == id) {
            f.name = name;
            return true;
        }
    }
    return false;
}

// the apps stay; the ones that were only in this folder go back to the top level
inline void DeleteFolder(Store& s, uint32_t id) {
    std::erase_if(s.folders, [&](const Folder& f) { return f.id == id; });
    std::erase_if(s.members, [&](const Member& m) { return m.id == id; });
}

inline auto BuildView(const Store& s, const std::vector<std::string>& paths, uint32_t cwd) -> View {
    View view;
    if (cwd == ROOT) {
        for (const auto& f : s.folders) {
            view.folders.push_back(f.id);
            view.counts.push_back(0);
            view.previews.emplace_back();
        }
    }
    for (size_t i = 0; i < paths.size(); i++) {
        for (const auto id : FoldersOf(s, paths[i])) {
            if (id == cwd) {
                view.apps.push_back(i);
            }
            for (size_t j = 0; j < view.folders.size(); j++) {
                if (view.folders[j] == id) {
                    view.counts[j]++;
                    if (view.previews[j].size() < PREVIEW_MAX) {
                        view.previews[j].push_back(i);
                    }
                }
            }
        }
    }
    return view;
}

inline auto Format(const Store& s) -> std::string {
    std::string out;
    for (const auto& f : s.folders) {
        out += "F\t" + std::to_string(f.id) + "\t" + f.name + "\n";
    }
    for (const auto& m : s.members) {
        out += "A\t" + std::to_string(m.id) + "\t" + m.path + "\n";
    }
    return out;
}

// bad lines are dropped; members of a folder that does not exist are dropped
inline auto Parse(std::string_view text) -> Store {
    Store s;
    std::vector<Member> members;
    while (!text.empty()) {
        const auto eol = text.find('\n');
        auto line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        const auto t1 = line.find('\t');
        const auto t2 = t1 == std::string_view::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string_view::npos) {
            continue;
        }
        const auto kind = line.substr(0, t1);
        const auto id_text = line.substr(t1 + 1, t2 - t1 - 1);
        const auto value = line.substr(t2 + 1);
        if (id_text.empty() || id_text.size() > 9 || id_text.find_first_not_of("0123456789") != std::string_view::npos) {
            continue;
        }
        const auto id = static_cast<uint32_t>(std::stoul(std::string{id_text}));

        if (kind == "F" && id != ROOT && IsValidName(value) && !FindFolder(s, id) && !FindFolderByName(s, value)) {
            s.folders.push_back({id, std::string{value}});
        } else if (kind == "A" && !value.empty() && value.front() == '/') {
            members.push_back({id, std::string{value}});
        }
    }
    for (auto& m : members) {
        if (m.id == ROOT || FindFolder(s, m.id)) {
            s.members.push_back(std::move(m));
        }
    }
    return s;
}

} // namespace sphaira::hb_folders
