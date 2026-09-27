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
#if ENABLE_NETWORK_INSTALL
#include "ui/menus/dbi_menu.hpp"
#endif

#include <vector>
#include <string>
#include <string_view>
#include <unordered_set>
#include <deque>
#include <algorithm>
#include <cctype>
#include <memory>
#include <cstring>

namespace sphaira::ui::menu::filebrowser {

#if ENABLE_NETWORK_INSTALL

using namespace detail;

static auto ToLowerString(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

static auto PathLeaf(std::string_view p) -> std::string {
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.remove_suffix(1);
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == p.npos) {
        return std::string(p);
    }
    return std::string(p.substr(slash + 1));
}

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
    if (!App::GetInstallEnable()) {
        App::ShowEnableInstallPrompt();
        return;
    }

    const auto root_paths = GetRecursiveInstallTargets();
    if (root_paths.empty()) {
        return;
    }

    PauseRemoteMetadata();

    auto found_paths = std::make_shared<std::vector<fs::FsPath>>();
    auto found_sizes = std::make_shared<std::vector<s64>>();

    App::Push<ProgressBox>(
        0,
        "Scanning..."_i18n,
        "",
        [this, root_paths, found_paths, found_sizes](ProgressBox* pbox) -> Result {
            auto fs = m_fs.get();
            R_UNLESS(fs, Result_FsNotActive);

            std::unordered_set<std::string> seen_dirs;
            std::unordered_set<std::string> seen_files;
            std::deque<fs::FsPath> dir_queue;

            for (const auto& r : root_paths) {
                const auto key = ToLowerString(r.toString());
                if (seen_dirs.insert(key).second) {
                    dir_queue.push_back(r);
                }
            }

            while (!dir_queue.empty()) {
                pbox->Yield();
                R_TRY(pbox->ShouldExitResult());

                const auto curr_dir = std::move(dir_queue.front());
                dir_queue.pop_front();

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
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    if (!std::strcmp(entry.name, ".") || !std::strcmp(entry.name, "..")) {
                        continue;
                    }

                    if (entry.type == FsDirEntryType_Dir) {
                        const auto child_dir = FsView::GetNewPath(curr_dir, entry.name);
                        const auto key = ToLowerString(child_dir.toString());
                        if (seen_dirs.insert(key).second) {
                            dir_queue.push_back(child_dir);
                        }
                    } else if (entry.type == FsDirEntryType_File) {
                        const auto ext = path::Extension(entry.name);
                        if (path::IsAnyOfIC(ext, detail::INSTALL_EXTENSIONS)) {
                            const auto full_path = FsView::GetNewPath(curr_dir, entry.name);
                            const auto key = ToLowerString(full_path.toString());
                            if (seen_files.insert(key).second) {
                                found_paths->push_back(full_path);
                                found_sizes->push_back(std::max<s64>(0, entry.file_size));
                            }
                        }
                    }
                }
            }

            R_SUCCEED();
        },
        [this, found_paths, found_sizes](Result rc) {
            if (rc == Result_TransferCancelled) {
                return;
            }

            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to scan folder"_i18n);
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
                m_fs_entry.type == FsType::Network
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
