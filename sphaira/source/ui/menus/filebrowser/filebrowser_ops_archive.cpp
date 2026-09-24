#include "ui/menus/filebrowser.hpp"
#include "path_util.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "download.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#if ENABLE_NETWORK_INSTALL
#include "ui/menus/dbi_menu.hpp"
#endif
#include "ui/error_box.hpp"
#include "log.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "nro.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "web.hpp"
#include "yati/yati.hpp"
#include "ui/menus/homebrew.hpp"
#include "title_info.hpp"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <memory>
#include <ranges>
#include <algorithm>
#include <vector>
#include <string>

namespace sphaira::ui::menu::filebrowser {

using namespace detail;

static auto DeleteAllCollectionsWithSelected(ProgressBox* pbox, fs::Fs* fs, const SelectedStash& selected, const FsDirCollections& collections, u32 mode = FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles) -> Result;

void FsView::OnDeleteCallback() {
    bool use_progress_box{true};
    if (IsSd()) {
        m_fs->SetIgnoreReadOnly(m_menu->m_ignore_read_only.Get());
    }

    // check if we only have 1 file / folder
    if (m_menu->m_selected.m_files.size() == 1) {
        const auto& entry = m_menu->m_selected.m_files[0];
        const auto full_path = GetNewPath(m_menu->m_selected.m_path, entry.name);

        if (entry.IsDir()) {
            if (!m_fs->IsNative()) {
                bool empty{};
                m_fs->IsDirEmpty(full_path, &empty);
                if (empty) {
                    if (auto rc = m_fs->DeleteDirectory(full_path); R_FAILED(rc)) {
                        App::PushErrorBox(rc, "Failed to delete directory"_i18n);
                    }
                    use_progress_box = false;
                }
            }
        } else {
            if (auto rc = m_fs->DeleteFile(full_path); R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to delete file"_i18n);
            }
            use_progress_box = false;
        }
    }

    if (!use_progress_box) {
        m_menu->RefreshViews();
        log_write("did delete\n");
    } else {
        App::Push<ProgressBox>(0, "Deleting"_i18n, "", [this](auto pbox) -> Result {
            auto& selected = m_menu->m_selected;
            auto src_fs = selected.m_view->GetFs();
            if (selected.m_view->IsSd()) {
                src_fs->SetIgnoreReadOnly(m_menu->m_ignore_read_only.Get());
            }

            if (src_fs->IsNative()) {
                for (const auto& p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto full_path = GetNewPath(selected.m_path, p.name);
                    pbox->SetTitle(p.name);
                    pbox->NewTransfer("Deleting "_i18n + full_path.toString());

                    if (p.IsDir()) {
                        log_write("deleting dir: %s\n", full_path.s);
                        R_TRY(src_fs->DeleteDirectoryRecursively(full_path));
                    } else {
                        log_write("deleting file: %s\n", full_path.s);
                        R_TRY(src_fs->DeleteFile(full_path));
                    }
                }
                R_SUCCEED();
            }

            FsDirCollections collections;
            // build list of dirs / files
            for (const auto& p : selected.m_files) {
                pbox->Yield();
                R_TRY(pbox->ShouldExitResult());

                const auto full_path = GetNewPath(selected.m_path, p.name);
                if (p.IsDir()) {
                    pbox->NewTransfer("Scanning "_i18n + full_path);
                    R_TRY(get_collections(src_fs, full_path, p.name, collections));
                }
            }

            return DeleteAllCollectionsWithSelected(pbox, src_fs, selected, collections);
        }, [this](Result rc){
            App::PushErrorBox(rc, "Failed to delete files"_i18n);

            m_menu->RefreshViews();
            log_write("did delete\n");
        });
    }
}

auto FsView::HasPasteConflicts() -> bool {
    auto& selected = m_menu->m_selected;
    if (selected.Empty()) {
        return false;
    }

    auto src_fs = selected.SrcFs();
    if (!src_fs || !m_fs) {
        return false;
    }

    const auto is_same_fs = selected.SameFs(this);
    const bool is_cut = (selected.m_type == SelectedType::Cut);

    if (is_same_fs && is_cut) {
        for (const auto& p : selected.m_files) {
            const auto src_path = GetNewPath(selected.m_path, p.name);
            const auto dst_path = GetNewPath(m_path, p.name);
            if (src_path == dst_path) {
                continue;
            }
            if (p.IsFile() && m_fs->FileExists(dst_path)) {
                return true;
            }
        }
        return false;
    }

    for (const auto& p : selected.m_files) {
        if (p.IsFile()) {
            const auto dst_path = GetNewPath(m_path, p.name);
            if (m_fs->FileExists(dst_path)) {
                return true;
            }
        } else if (p.IsDir()) {
            const auto full_path = GetNewPath(selected.m_path, p.name);
            FsDirCollections collections;
            if (R_SUCCEEDED(get_collections(src_fs, full_path, p.name, collections, false))) {
                for (const auto& c : collections) {
                    const auto base_dst_path = GetNewPath(m_path, c.parent_name);
                    for (const auto& f : c.files) {
                        const auto dst_path = GetNewPath(base_dst_path, f.name);
                        if (m_fs->FileExists(dst_path)) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

void FsView::OnPasteCallback(bool replace_existing) {
    // check if we only have 1 file / folder and is cut (rename)
    if (m_menu->m_selected.SameFs(this) && m_menu->m_selected.m_files.size() == 1 && m_menu->m_selected.m_type == SelectedType::Cut) {
        const auto& entry = m_menu->m_selected.m_files[0];
        const auto full_path = GetNewPath(m_menu->m_selected.m_path, entry.name);
        const auto dst_path = GetNewPath(entry);

        if (entry.IsDir()) {
            m_fs->RenameDirectory(full_path, dst_path);
        } else {
            if (replace_existing && full_path != dst_path && m_fs->FileExists(dst_path)) {
                m_fs->DeleteFile(dst_path);
            }
            m_fs->RenameFile(full_path, dst_path);
        }

        m_menu->RefreshViews();
    } else {
        App::Push<ProgressBox>(0, "Pasting"_i18n, "", [this, replace_existing](auto pbox) -> Result {
            auto& selected = m_menu->m_selected;
            auto src_fs = selected.SrcFs();
            const auto is_same_fs = selected.SameFs(this);

            if (selected.SameFs(this) && selected.m_type == SelectedType::Cut) {
                for (const auto& p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto src_path = GetNewPath(selected.m_path, p.name);
                    const auto dst_path = GetNewPath(m_path, p.name);

                    pbox->SetTitle(p.name);
                    pbox->NewTransfer("Pasting "_i18n + src_path);

                    if (p.IsDir()) {
                        m_fs->RenameDirectory(src_path, dst_path);
                    } else {
                        if (replace_existing && src_path != dst_path && m_fs->FileExists(dst_path)) {
                            R_TRY(m_fs->DeleteFile(dst_path));
                        }
                        m_fs->RenameFile(src_path, dst_path);
                    }
                }
            } else {
                FsDirCollections collections;

                const auto on_paste_file = [&](auto& src_path, auto& dst_path) -> Result {
                    if (selected.m_type == SelectedType::Cut) {
                        // update timestamp if possible.
                        if (!m_fs->IsNative()) {
                            FsTimeStampRaw ts;
                            if (R_SUCCEEDED(src_fs->GetFileTimeStampRaw(src_path, &ts))) {
                                m_fs->SetTimestamp(dst_path, &ts);
                            }
                        }

                        // delete src file. folders are removed after.
                        R_TRY(src_fs->DeleteFile(src_path));
                    }

                    R_SUCCEED();
                };

                // build list of dirs / files
                for (const auto&p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto full_path = GetNewPath(selected.m_path, p.name);
                    if (p.IsDir()) {
                        pbox->NewTransfer("Scanning "_i18n + full_path);
                        R_TRY(get_collections(src_fs, full_path, p.name, collections));
                    }
                }

                for (const auto& p : selected.m_files) {
                    pbox->Yield();
                    R_TRY(pbox->ShouldExitResult());

                    const auto src_path = GetNewPath(selected.m_path, p.name);
                    const auto dst_path = GetNewPath(p);

                    if (p.IsDir()) {
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Creating "_i18n + dst_path);
                        m_fs->CreateDirectory(dst_path);
                    } else {
                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Copying "_i18n + src_path);
                        if (replace_existing && m_fs->FileExists(dst_path)) {
                            R_TRY(m_fs->DeleteFile(dst_path));
                        }
                        R_TRY(pbox->CopyFile(src_fs, m_fs.get(), src_path, dst_path, is_same_fs));
                        R_TRY(on_paste_file(src_path, dst_path));
                    }
                }

                // copy everything in collections
                for (const auto& c : collections) {
                    const auto base_dst_path = GetNewPath(m_path, c.parent_name);

                    for (const auto& p : c.dirs) {
                        pbox->Yield();
                        R_TRY(pbox->ShouldExitResult());

                        // const auto src_path = GetNewPath(c.path, p.name);
                        const auto dst_path = GetNewPath(base_dst_path, p.name);

                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Creating "_i18n + dst_path);
                        m_fs->CreateDirectory(dst_path);
                    }

                    for (const auto& p : c.files) {
                        pbox->Yield();
                        R_TRY(pbox->ShouldExitResult());

                        const auto src_path = GetNewPath(c.path, p.name);
                        const auto dst_path = GetNewPath(base_dst_path, p.name);

                        pbox->SetTitle(p.name);
                        pbox->NewTransfer("Copying "_i18n + src_path);
                        if (replace_existing && m_fs->FileExists(dst_path)) {
                            R_TRY(m_fs->DeleteFile(dst_path));
                        }
                        R_TRY(pbox->CopyFile(src_fs, m_fs.get(), src_path, dst_path, is_same_fs));
                        R_TRY(on_paste_file(src_path, dst_path));
                    }
                }

                // moving accross fs is not possible, thus files have to be copied.
                // this leaves the files on the src_fs.
                // the files are deleted one by one after a successfull copy (see above)
                // however this leaves the folders.
                // the folders cannot be deleted until the end as they have to be removed in
                // reverse order so that the folder can be deleted (it must be empty).
                if (selected.m_type == SelectedType::Cut) {
                    R_TRY(DeleteAllCollectionsWithSelected(pbox, src_fs, selected, collections, FsDirOpenMode_ReadDirs));
                }
            }

            R_SUCCEED();
        }, [this](Result rc){
            App::PushErrorBox(rc, "Failed to copy or move files"_i18n);

            m_menu->RefreshViews();
            log_write("did paste\n");
        });
    }
}

void FsView::InstallFiles() {
    if (!App::GetInstallEnable()) {
        App::ShowEnableInstallPrompt();
        return;
    }

    const auto targets = GetSelectedEntries();

#if ENABLE_NETWORK_INSTALL
    PauseRemoteMetadata();
    std::vector<fs::FsPath> paths;
    std::vector<s64> source_sizes;
    paths.reserve(targets.size());
    source_sizes.reserve(targets.size());
    for (const auto& entry : targets) {
        paths.emplace_back(GetNewPath(entry));
        source_sizes.emplace_back(entry.file_size);
    }
    App::Push<ui::menu::dbi::Menu>(MenuFlag_None, m_fs.get(), std::move(paths), std::move(source_sizes),
        m_fs_entry.type == FsType::Network);
    return;
#else
    // stop background metadata requests competing with the installer for
    // the remote filesystem. Resumed on focus gained.
    PauseRemoteMetadata();
    auto failures = std::make_shared<std::vector<std::pair<std::string, Result>>>();

    App::Push<OptionBox>("Install Selected files?"_i18n, "No"_i18n, "Yes"_i18n, 0, [this, targets, failures](auto op_index){
        if (op_index && *op_index) {
            App::PopToMenu();

            App::Push<ui::ProgressBox>(0, "Installing "_i18n, "", [this, targets, failures](auto pbox) -> Result {
                for (auto& e : targets) {
                    R_TRY(pbox->ShouldExitResult());
                    pbox->SetTitle(e.GetName());

                    const auto rc = yati::InstallFromFile(pbox, m_fs.get(), GetNewPath(e));
                    if (R_FAILED(rc)) {
                        if (pbox->ShouldExit()) {
                            return rc;
                        }

                        log_write("failed to install %s: 0x%X\n", e.name, rc);
                        failures->emplace_back(e.GetName(), rc);
                        continue;
                    }

                    App::Notify("Installed "_i18n + e.GetName());
                }

                R_SUCCEED();
            }, [this, targets, failures](Result rc){
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "File install failed!"_i18n);
                    return;
                }

                for (auto& entry : m_entries) {
                    const auto is_target = std::ranges::any_of(targets, [&entry](const auto& target){
                        return !std::strcmp(entry.name, target.name);
                    });
                    if (!is_target) {
                        continue;
                    }

                    entry.selected = std::ranges::any_of(*failures, [&entry](const auto& failure){
                        return failure.first == entry.name;
                    });
                }

                m_selected_count = 0;
                for (const auto& entry : m_entries) {
                    if (entry.selected) {
                        m_selected_count++;
                    }
                }
                m_menu->UpdateSubheading();

                if (!failures->empty()) {
                    PopupList::Items items;
                    items.reserve(failures->size());

                    for (const auto& [name, fail_rc] : *failures) {
                        char rc_buf[32]{};
                        std::snprintf(rc_buf, sizeof(rc_buf), "0x%X", fail_rc);
                        items.emplace_back(name + " (" + rc_buf + ")");
                    }

                    App::Push<PopupList>("Install errors"_i18n, std::move(items), [](auto){});
                }
            });
        }
    });
#endif
}

auto FsView::CheckIfUpdateFolder() -> Result {
    R_UNLESS(IsSd(), Result_FileBrowserDirNotDaybreak);
    R_UNLESS(m_fs->IsNative(), Result_FileBrowserDirNotDaybreak);

    // check if we have already tried to find daybreak
    if (m_daybreak_path.has_value() && m_daybreak_path.value().empty()) {
        return FsError_FileNotFound;
    }

    // check that we have daybreak installed
    if (!m_daybreak_path.has_value()) {
        auto daybreak_path = DAYBREAK_PATH;
        if (!m_fs->FileExists(DAYBREAK_PATH)) {
            if (auto e = nro_find(homebrew::GetNroEntries(), "Daybreak", "Atmosphere-NX", {}); e.has_value()) {
                daybreak_path = e.value().path;
            } else {
                log_write("failed to find daybreak\n");
                m_daybreak_path = "";
                return FsError_FileNotFound;
            }
        }
        m_daybreak_path = daybreak_path;
        log_write("found daybreak in: %s\n", m_daybreak_path.value().s);
    }

    // check that we have enough ncas and not too many
    R_UNLESS(m_entries.size() > 150 && m_entries.size() < 300, Result_FileBrowserDirNotDaybreak);

    // check that all entries end in .nca
    for (auto& e : m_entries) {
        // check that we are at the bottom level
        R_UNLESS(e.type == FsDirEntryType_File, Result_FileBrowserDirNotDaybreak);

        const auto ext = std::strrchr(e.name, '.');
        R_UNLESS(ext && path::EqualsIC(ext, ".nca"), Result_FileBrowserDirNotDaybreak);
    }

    R_SUCCEED();
}

auto FsView::get_collection(fs::Fs* fs, const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollection& out, bool inc_file, bool inc_dir, bool inc_size) -> Result {
    out.path = path;
    out.parent_name = parent_name;

    const auto fetch = [fs, &path](std::vector<FsDirectoryEntry>& out, u32 flags) -> Result {
        fs::Dir d;
        R_TRY(fs->OpenDirectory(path, flags, &d));
        return d.ReadAll(out);
    };

    if (inc_file) {
        u32 flags = FsDirOpenMode_ReadFiles;
        if (!inc_size) {
            flags |= FsDirOpenMode_NoFileSize;
        }
        R_TRY(fetch(out.files, flags));
    }

    if (inc_dir) {
        R_TRY(fetch(out.dirs, FsDirOpenMode_ReadDirs));
    }

    R_SUCCEED();
}

auto FsView::get_collections(fs::Fs* fs, const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollections& out, bool inc_size) -> Result {
    // get a list of all the files / dirs
    FsDirCollection collection;
    R_TRY(get_collection(fs, path, parent_name, collection, true, true, inc_size));
    log_write("got collection: %s parent_name: %s files: %zu dirs: %zu\n", path.s, parent_name.s, collection.files.size(), collection.dirs.size());
    out.emplace_back(collection);

    for (const auto&p : collection.dirs) {
        // use heap as to not explode the stack
        const auto new_path = std::make_unique<fs::FsPath>(FsView::GetNewPath(path, p.name));
        const auto new_parent_name = std::make_unique<fs::FsPath>(FsView::GetNewPath(parent_name, p.name));
        log_write("trying to get nested collection: %s parent_name: %s\n", new_path->s, new_parent_name->s);
        R_TRY(get_collections(fs, *new_path, *new_parent_name, out, inc_size));
    }

    R_SUCCEED();
}

auto FsView::get_collection(const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollection& out, bool inc_file, bool inc_dir, bool inc_size) -> Result {
    return get_collection(m_fs.get(), path, parent_name, out, true, true, inc_size);
}

auto FsView::get_collections(const fs::FsPath& path, const fs::FsPath& parent_name, FsDirCollections& out, bool inc_size) -> Result {
    return get_collections(m_fs.get(), path, parent_name, out, inc_size);
}

Result FsView::DeleteAllCollections(ProgressBox* pbox, fs::Fs* fs, const FsDirCollections& collections, u32 mode) {
    // delete everything in collections, reversed
    for (const auto& c : std::views::reverse(collections)) {
        const auto delete_func = [&](auto& array) -> Result {
            for (const auto& p : array) {
                pbox->Yield();
                R_TRY(pbox->ShouldExitResult());

                const auto full_path = FsView::GetNewPath(c.path, p.name);
                pbox->SetTitle(p.name);
                pbox->NewTransfer("Deleting "_i18n + full_path.toString());
                if ((mode & FsDirOpenMode_ReadDirs) && p.type == FsDirEntryType_Dir) {
                    log_write("deleting dir: %s\n", full_path.s);
                    R_TRY(fs->DeleteDirectory(full_path));
                    svcSleepThread(1e+5);
                } else if ((mode & FsDirOpenMode_ReadFiles) && p.type == FsDirEntryType_File) {
                    log_write("deleting file: %s\n", full_path.s);
                    R_TRY(fs->DeleteFile(full_path));
                    svcSleepThread(1e+5);
                }
            }

            R_SUCCEED();
        };

        R_TRY(delete_func(c.files));
        R_TRY(delete_func(c.dirs));
    }

    R_SUCCEED();
}

static Result DeleteAllCollectionsWithSelected(ProgressBox* pbox, fs::Fs* fs, const SelectedStash& selected, const FsDirCollections& collections, u32 mode) {
    R_TRY(FsView::DeleteAllCollections(pbox, fs, collections, mode));

    for (const auto& p : selected.m_files) {
        pbox->Yield();
        R_TRY(pbox->ShouldExitResult());

        const auto full_path = FsView::GetNewPath(selected.m_path, p.name);
        pbox->SetTitle(p.name);
        pbox->NewTransfer("Deleting "_i18n + full_path.toString());

        if ((mode & FsDirOpenMode_ReadDirs) && p.type == FsDirEntryType_Dir) {
            log_write("deleting dir: %s\n", full_path.s);
            R_TRY(fs->DeleteDirectory(full_path));
        } else if ((mode & FsDirOpenMode_ReadFiles) && p.type == FsDirEntryType_File) {
            log_write("deleting file: %s\n", full_path.s);
            R_TRY(fs->DeleteFile(full_path));
        }
    }

    R_SUCCEED();
}

auto FsView::IsReadOnly(const fs::FsPath& path) const -> bool {
    if (m_fs_entry.IsReadOnly()) {
        return true;
    }
    if (m_menu->m_ignore_read_only.Get()) {
        return false;
    }
    return fs::is_read_only(path);
}

auto FsView::AnySelectedReadOnly() const -> bool {
    const auto entries = GetSelectedEntries();
    for (const auto& e : entries) {
        if (IsReadOnly(GetNewPath(e))) {
            return true;
        }
    }
    return false;
}

} // namespace sphaira::ui::menu::filebrowser
