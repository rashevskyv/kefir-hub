#include "ui/menus/filebrowser.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "path_util.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/error_box.hpp"
#include "ui/sidebar.hpp"
#if ENABLE_NETWORK_INSTALL
#include "archive_extract.hpp"
#include "release_choice.hpp"
#include "sd_bundle.hpp"
#include "ui/menus/dbi_menu.hpp"
#endif

#include <vector>
#include <string>
#include <string_view>
#include <deque>
#include <map>
#include <algorithm>
#include <cctype>
#include <memory>
#include <cstring>

namespace sphaira::ui::menu::filebrowser {

#if ENABLE_NETWORK_INSTALL

using namespace detail;

namespace {

auto ToLowerString(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

auto PathLeaf(std::string_view p) -> std::string {
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.remove_suffix(1);
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == p.npos) {
        return std::string(p);
    }
    return std::string(p.substr(slash + 1));
}

// one selected folder, analysed like a release in DBI Backend Qt (release_plan.hpp).
struct Folder {
    fs::FsPath base{};                // the release paths are relative to it
    std::string name{};               // the selected folder's own name
    std::map<std::string, s64> sizes; // every file under it: release path -> size
    release::Release rel{};
    release::Choice choice{};

    auto Full(const std::string& rel_path) const -> fs::FsPath {
        return fs::AppendPath(base, rel_path.c_str());
    }
    auto SizeOf(const std::string& rel_path) const -> s64 {
        const auto it = sizes.find(rel_path);
        return it == sizes.end() ? 0 : it->second;
    }
};
using Folders = std::shared_ptr<std::vector<Folder>>;

// the files of an option as the tar DBI Backend Qt sends (tar_stream_layout.hpp), read from
// where they lie, so sd_bundle reads a folder the way it reads a bundle from the PC.
struct TarSource final : yati::source::Base {
    TarSource(fs::Fs* fs, const Folder& folder, const tar_stream::Layout& layout)
    : m_fs{fs}, m_folder{folder}, m_layout{layout} {}

    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        size = std::clamp<s64>(m_layout.size - off, 0, size);
        R_TRY(m_layout.Read(off, buf, size, [this](size_t member, int64_t file_off, void* out, int64_t n) -> Result {
            if (member != m_open) {
                m_file.Close();
                m_open = SIZE_MAX;
                R_TRY(m_fs->OpenFile(m_folder.Full(m_layout.members[member].src), FsOpenMode_Read, &m_file));
                // the header already says the size the folder listing gave: another size would
                // write a cut or empty file and call it installed.
                s64 file_size{};
                R_TRY(m_file.GetSize(&file_size));
                R_UNLESS(file_size == m_layout.members[member].size, Result_ArchiveSourceRead);
                m_open = member;
            }
            // a short read would shift every header after it: the tar must get every byte.
            for (int64_t done = 0; done < n;) {
                u64 got{};
                R_TRY(m_file.Read(file_off + done, static_cast<u8*>(out) + done, n - done, 0, &got));
                R_UNLESS(got > 0, Result_ArchiveSourceRead);
                done += got;
            }
            R_SUCCEED();
        }));
        *bytes_read = size;
        R_SUCCEED();
    }

    fs::Fs* m_fs;
    const Folder& m_folder;
    const tar_stream::Layout& m_layout;
    fs::File m_file{};
    size_t m_open{SIZE_MAX};
};

// every file under `root`, with its release path, into `f`; packages also into the plain list.
Result ScanFolder(ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& root, const std::string& prefix, Folder& f,
                  std::vector<fs::FsPath>& packages, std::vector<s64>& package_sizes) {
    std::deque<std::pair<fs::FsPath, std::string>> dirs;
    dirs.emplace_back(root, prefix);
    while (!dirs.empty()) {
        pbox->Yield();
        R_TRY(pbox->ShouldExitResult());

        const auto [curr_dir, rel] = std::move(dirs.front());
        dirs.pop_front();
        pbox->SetTitle(PathLeaf(curr_dir.toString()));
        pbox->NewTransfer("Scanning "_i18n + curr_dir.toString());

        fs::Dir dir;
        R_TRY(fs->OpenDirectory(curr_dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir));
        std::vector<FsDirectoryEntry> entries;
        R_TRY(dir.ReadAll(entries));
        std::ranges::sort(entries, [](const FsDirectoryEntry& a, const FsDirectoryEntry& b) {
            return std::strcmp(a.name, b.name) < 0;
        });

        for (const auto& entry : entries) {
            if (!std::strcmp(entry.name, ".") || !std::strcmp(entry.name, "..")) {
                continue;
            }
            if (entry.type == FsDirEntryType_Dir) {
                dirs.emplace_back(fs::AppendPath(curr_dir, entry.name), rel + entry.name + "/");
            } else if (entry.type == FsDirEntryType_File) {
                const auto size = std::max<s64>(0, entry.file_size);
                f.sizes[rel + entry.name] = size;
                if (path::IsAnyOfIC(path::Extension(entry.name), detail::INSTALL_EXTENSIONS)) {
                    packages.push_back(fs::AppendPath(curr_dir, entry.name));
                    package_sizes.push_back(size);
                }
            }
        }
    }
    R_SUCCEED();
}

auto OptionTitle(const release::Option& o) -> std::string {
    switch (o.kind) {
        case release::Kind::Port: return "Port"_i18n + ": " + o.name;
        case release::Kind::Data: return "Game data"_i18n + ": " + o.name;
        case release::Kind::Mod: return "Mod"_i18n + ": " + o.name;
        case release::Kind::Cache: return "Game cache"_i18n + ": " + o.name;
        case release::Kind::Save: return "Save data"_i18n + ": " + o.name;
    }
    return o.name;
}

auto OptionInfo(const release::Option& o) -> std::string {
    auto info = std::to_string(o.files.size()) + " " + "files"_i18n;
    if (!o.archive.empty()) {
        info += " (" + release::detail::Basename(o.archive) + ")";
    }
    if (!o.updates.empty()) {
        info += ". " + "The game update this mod needs is installed with it."_i18n;
    }
    if (o.tid_mismatch) {
        info += ". " + "Made for another version of the game (another title ID)."_i18n;
    }
    if (o.incomplete) {
        info += ". " + "The download is not complete."_i18n;
    }
    return info;
}

// the chosen options of `f` as queue entries, in the order the backend sends its bundles.
void AddJobs(fs::Fs* fs, bool is_sd, const Folders& folders, const Folder& f, std::vector<dbi::LocalJob>& jobs) {
    const auto size_of = [&f](const std::string& src) -> int64_t { return f.SizeOf(src); };
    const auto add = [&](const std::string& game, u64 tid, const std::vector<release::Option>& options) {
        for (const auto& o : options) {
            if (!release::Chosen(f.choice, tid).contains(o.key)) {
                continue;
            }
            const auto name = release::BundleName(game, tid, o);
            if (name.empty()) {
                continue; // a console save without its game: no title to write it to
            }

            std::vector<tar_stream::Member> members;
            if (!o.archive.empty()) {
                const auto rules = release::ArchiveRules(o.files);
                if (is_sd) {
                    jobs.push_back({name, f.SizeOf(o.archive), [path = f.Full(o.archive), rules](InstallProgress* p, const AccountUid&) {
                        return sd_bundle::ExtractLocal(p, path, rules);
                    }});
                    continue;
                }
                // not on the microSD: the archive goes there first, like in a bundle from the PC.
                // ponytail: one file only, a split archive (part1.rar) on a USB drive fails here.
                std::string text;
                for (const auto& r : rules) {
                    text += r.src + "\t" + r.dst + "\n";
                }
                const auto text_size = static_cast<int64_t>(text.size());
                members.push_back({std::string{sd_bundle::RULES}, {}, std::move(text), text_size});
                members.push_back({std::string{sd_bundle::META_DIR} + release::detail::Basename(o.archive), o.archive, {}, f.SizeOf(o.archive)});
            } else {
                members = release::BundleMembers(o, size_of);
                // the folder as a microSD path; empty when it is on another drive.
                std::string_view on_sd{f.base.s};
                if (!is_sd) {
                    on_sd = on_sd.starts_with("sdmc:") ? on_sd.substr(5) : std::string_view{};
                }
                if (!on_sd.empty() && o.kind != release::Kind::Cache && o.kind != release::Kind::Save) {
                    // the folder is already in place (/switch/<port>): writing a file deletes it first.
                    std::erase_if(members, [on_sd](const tar_stream::Member& m) { return release::IsSamePath(on_sd, m.src, m.name); });
                }
                if (members.empty()) {
                    continue;
                }
            }

            const auto layout = std::make_shared<const tar_stream::Layout>(std::move(members));
            const auto kind = save_bundle::KindOf(name);
            jobs.push_back({name, layout->size, [fs, folders, &f, layout, kind, tid](InstallProgress* p, const AccountUid& uid) -> Result {
                TarSource source{fs, f, *layout};
                if (kind == save_bundle::Kind::None) {
                    return sd_bundle::Receive(p, &source, layout->size);
                }
                return sd_bundle::ReceiveSave(p, &source, layout->size, kind == save_bundle::Kind::Cache, tid, uid);
            }});
        }
    };
    for (const auto& g : f.rel.games) {
        add(g.name(), g.tid, g.options);
    }
    add(f.name, release::ORPHANS, f.rel.orphans);
}

// what to install besides the packages: one tick per option, like the release window of the backend.
void ShowChoice(fs::Fs* fs, bool is_sd, bool network, const Folders& folders) {
    auto sidebar = std::make_unique<Sidebar>("Install all from folder"_i18n, Sidebar::Side::RIGHT);
    const auto sb = sidebar.get();

    sidebar->Add<SidebarEntryCallback>("Install"_i18n, [fs, is_sd, network, folders, sb](){
        std::vector<fs::FsPath> paths;
        std::vector<s64> sizes;
        std::vector<dbi::LocalJob> jobs;
        for (const auto& f : *folders) {
            if (release::Blocking(f.rel, f.choice)) {
                App::Push<OptionBox>("The chosen mods need different game updates, or a download is not complete. Change the choice."_i18n, "OK"_i18n);
                return;
            }
            for (const auto& p : release::Packages(f.rel, f.choice)) {
                paths.push_back(f.Full(p));
                sizes.push_back(f.SizeOf(p));
            }
        }
        for (const auto& f : *folders) {
            AddJobs(fs, is_sd, folders, f, jobs);
        }
        if (paths.empty() && jobs.empty()) {
            App::Push<OptionBox>("Nothing to install."_i18n, "OK"_i18n);
            return;
        }
        sb->SetPop();
        App::Push<ui::menu::dbi::Menu>(MenuFlag_None, fs, std::move(paths), std::move(sizes), network, std::move(jobs));
    }, "Open the install queue: the packages first, then the ticked options."_i18n);

    for (auto& f : *folders) {
        const auto add = [&](const std::string& title, u64 tid, const std::vector<release::Option>& options) {
            if (options.empty()) {
                return;
            }
            sidebar->Add<SidebarEntryHeader>(title);
            for (const auto& o : options) {
                sidebar->Add<SidebarEntryCheckbox>(OptionTitle(o),
                    [folders, &f, tid, key = o.key](){ return release::Chosen(f.choice, tid).contains(key); },
                    [folders, &f, tid, key = o.key](bool on){ release::Toggle(f.rel, f.choice, tid, key, on); },
                    OptionInfo(o));
            }
        };
        for (const auto& g : f.rel.games) {
            add(g.name(), g.tid, g.options);
        }
        add(f.name, release::ORPHANS, f.rel.orphans);
    }
    App::Push(std::move(sidebar));
}

} // namespace

auto FsView::GetRecursiveInstallTargets() const -> std::vector<fs::FsPath> {
    std::vector<fs::FsPath> out;
    if (m_fs_entry.type == FsType::Root || m_fs_entry.type == FsType::Archive) {
        return out;
    }

    if (m_selected_count > 0) {
        for (const auto& e : m_entries) {
            if (e.IsSelected() && e.IsDir()) {
                out.emplace_back(GetNewPath(e));
            }
        }
    } else if (m_entries_current.size() && m_index >= 0 && m_index < static_cast<s64>(m_entries_current.size())) {
        if (!IsParentEntry(m_index) && GetEntry().IsDir()) {
            out.emplace_back(GetNewPath(GetEntry()));
        }
    }

    return out;
}

void FsView::InstallFolderRecursively() {
    const auto root_paths = GetRecursiveInstallTargets();
    if (root_paths.empty()) {
        return;
    }

    PauseRemoteMetadata();

    auto found_paths = std::make_shared<std::vector<fs::FsPath>>();
    auto found_sizes = std::make_shared<std::vector<s64>>();
    auto folders = std::make_shared<std::vector<Folder>>();
    const bool network = m_fs_entry.type == FsType::Network;

    App::Push<ProgressBox>(
        0,
        "Scanning..."_i18n,
        "",
        [this, root_paths, found_paths, found_sizes, folders, network](ProgressBox* pbox) -> Result {
            auto fs = m_fs.get();
            R_UNLESS(fs, Result_FsNotActive);
            auto lang = ToLowerString(i18n::GetCurrentLanguageCode()).substr(0, 2);
            if (lang.size() < 2) {
                lang = "en";
            }

            for (const auto& root : root_paths) {
                Folder f;
                const auto full = root.toString();
                f.name = PathLeaf(full);
                // a folder of the microSD layout keeps its name in the paths: switch/<port>/...
                const auto low = ToLowerString(f.name);
                const bool layout = low == "switch" || low == "atmosphere" || release::detail::IsHex16(f.name);
                const auto slash = full.find_last_of('/');
                f.base = !layout ? root : fs::FsPath{slash == full.npos || slash == 0 ? std::string{"/"} : full.substr(0, slash)};
                R_TRY(ScanFolder(pbox, fs, root, layout ? f.name + "/" : std::string{}, f, *found_paths, *found_sizes));

                std::vector<std::string> paths;
                for (const auto& [path, size] : f.sizes) {
                    paths.push_back(path);
                }
                // archives are opened to see what they hold; not over the network (ranged reads hang).
                release::ListArchive lister;
                if (!network) {
                    lister = [&](const std::string& path) -> std::optional<std::vector<std::string>> {
                        std::vector<std::string> names;
                        pbox->NewTransfer("Scanning "_i18n + path);
                        if (R_FAILED(archive::List(pbox, fs, f.Full(path), names))) {
                            return std::nullopt;
                        }
                        return names;
                    };
                }
                f.rel = release::Analyze(paths, lister, layout ? std::string{} : f.name);
                R_TRY(pbox->ShouldExitResult());
                f.choice = release::Defaults(f.rel, lang);
                log_write("[RELEASE] %s: %zu games, %zu shared options, %zu skipped\n", full.c_str(),
                    f.rel.games.size(), f.rel.orphans.size(), f.rel.skipped.size());
                folders->push_back(std::move(f));
            }

            R_SUCCEED();
        },
        [this, found_paths, found_sizes, folders, network](Result rc) {
            if (rc == Result_TransferCancelled) {
                return;
            }

            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to scan folder"_i18n);
                return;
            }

            // a port, a mod, game data or a save in a folder: ask what to install.
            if (std::ranges::any_of(*folders, [](const Folder& f) { return release::IsSmart(f.rel); })) {
                ShowChoice(m_fs.get(), IsSd(), network, folders);
                return;
            }

            if (found_paths->empty()) {
                App::Push<OptionBox>("No packages found."_i18n, "OK"_i18n);
                return;
            }

            App::Push<ui::menu::dbi::Menu>(
                MenuFlag_None,
                m_fs.get(),
                std::move(*found_paths),
                std::move(*found_sizes),
                network
            );
        }
    );
}

#else

auto FsView::GetRecursiveInstallTargets() const -> std::vector<fs::FsPath> {
    return {};
}

void FsView::InstallFolderRecursively() {}

#endif

} // namespace sphaira::ui::menu::filebrowser
