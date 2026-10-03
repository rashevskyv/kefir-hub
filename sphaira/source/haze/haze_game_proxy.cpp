#include "haze/haze_game_proxy_internal.hpp"

namespace sphaira::haze {

Result FsGameProxy::GetTotalSpace(const char *path, s64 *out) {
    *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
    R_SUCCEED();
}

Result FsGameProxy::GetFreeSpace(const char *path, s64 *out) {
    // only the per-game mods folders take writes, and they live on the SD card.
    return m_sd.GetFreeSpace("/", out);
}

Result FsGameProxy::GetEntryType(const char *path, FsDirEntryType *out_entry_type) {
    const auto pp = Parse(path);

    switch (pp.kind) {
        case sphaira::mtp::PathKind::Root:
        case sphaira::mtp::PathKind::MergedDir:
        case sphaira::mtp::PathKind::SeparateDir:
        case sphaira::mtp::PathKind::ForwardersDir:
            *out_entry_type = FsDirEntryType_Dir;
            R_SUCCEED();

        case sphaira::mtp::PathKind::SeparateGameDir: {
            R_UNLESS(m_games.count(pp.game), FsError_PathNotFound);
            *out_entry_type = FsDirEntryType_Dir;
            R_SUCCEED();
        }

        case sphaira::mtp::PathKind::InfoFile:
            *out_entry_type = FsDirEntryType_File;
            R_SUCCEED();

        case sphaira::mtp::PathKind::ModsPath: {
            fs::FsPath sd_path;
            R_TRY(ModsSdPath(pp, sd_path));
            if (pp.filename.empty()) {
                // shown even before the folder exists on the SD card.
                *out_entry_type = FsDirEntryType_Dir;
                R_SUCCEED();
            }
            return m_sd.GetEntryType(sd_path, out_entry_type);
        }

        case sphaira::mtp::PathKind::MergedFile: {
            std::shared_ptr<GameNsp> nsp;
            size_t index{};
            R_TRY(FindMergedFile(pp.filename, nsp, index));
            *out_entry_type = FsDirEntryType_File;
            R_SUCCEED();
        }

        case sphaira::mtp::PathKind::ForwardersFile: {
            std::shared_ptr<GameNsp> nsp;
            size_t index{};
            R_TRY(FindForwarderFile(pp.filename, nsp, index));
            *out_entry_type = FsDirEntryType_File;
            R_SUCCEED();
        }

        case sphaira::mtp::PathKind::SeparateFile: {
            std::shared_ptr<GameNsp> nsp;
            size_t index{};
            R_TRY(FindFile(pp.game, pp.filename, nsp, index));
            *out_entry_type = FsDirEntryType_File;
            R_SUCCEED();
        }

        default:
            R_THROW(FsError_PathNotFound);
    }
}

Result FsGameProxy::CreateFile(const char* path, s64 size, u32 option) {
    fs::FsPath sd_path;
    R_TRY(ModsSdItem(path, sd_path));
    R_TRY(m_sd.CreateDirectoryRecursivelyWithPath(sd_path));
    return m_sd.CreateFile(sd_path, size, option);
}

Result FsGameProxy::DeleteFile(const char* path) {
    fs::FsPath sd_path;
    R_TRY(ModsSdItem(path, sd_path));
    return m_sd.DeleteFile(sd_path);
}

Result FsGameProxy::RenameFile(const char *old_path, const char *new_path) {
    fs::FsPath sd_old, sd_new;
    R_TRY(ModsSdItem(old_path, sd_old));
    R_TRY(ModsSdItem(new_path, sd_new));
    return m_sd.RenameFile(sd_old, sd_new);
}

Result FsGameProxy::CreateDirectory(const char* path) {
    fs::FsPath sd_path;
    R_TRY(ModsSdItem(path, sd_path));
    return m_sd.CreateDirectoryRecursively(sd_path);
}

Result FsGameProxy::DeleteDirectoryRecursively(const char* path) {
    fs::FsPath sd_path;
    R_TRY(ModsSdItem(path, sd_path));
    return m_sd.DeleteDirectoryRecursively(sd_path);
}

Result FsGameProxy::RenameDirectory(const char *old_path, const char *new_path) {
    fs::FsPath sd_old, sd_new;
    R_TRY(ModsSdItem(old_path, sd_old));
    R_TRY(ModsSdItem(new_path, sd_new));
    return m_sd.RenameDirectory(sd_old, sd_new);
}

Result FsGameProxy::SetFileSize(FsFile *file, s64 size) {
    FileHandle* h;
    std::memcpy(&h, &file->s, sizeof(h));
    R_UNLESS(h->is_sd, FsError_NotImplemented);
    return h->sd.SetSize(size);
}

Result FsGameProxy::WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) {
    FileHandle* h;
    std::memcpy(&h, &file->s, sizeof(h));
    R_UNLESS(h->is_sd, FsError_NotImplemented);
    return h->sd.Write(off, buf, write_size, option);
}

Result FsGameProxy::OpenFile(const char *path, u32 mode, FsFile *out_file) {
    log_write("[MTP-GAMES] OpenFile(%s)\n", path);
    const auto pp = Parse(path);
    auto handle = std::make_unique<FileHandle>();

    if (pp.kind == sphaira::mtp::PathKind::ModsPath) {
        fs::FsPath sd_path;
        R_TRY(ModsSdPath(pp, sd_path));
        R_UNLESS(!pp.filename.empty(), FsError_PathNotFound);
        R_TRY(m_sd.OpenFile(sd_path, mode, &handle->sd));
        handle->is_sd = true;
        auto raw = handle.release();
        std::memcpy(&out_file->s, &raw, sizeof(raw));
        R_SUCCEED();
    }

    R_UNLESS(!(mode & (FsOpenMode_Write | FsOpenMode_Append)), FsError_NotImplemented);
    if (pp.kind == sphaira::mtp::PathKind::InfoFile) {
        handle->info = InfoText(pp.game);
    } else if (pp.kind == sphaira::mtp::PathKind::MergedFile) {
        R_TRY(FindMergedFile(pp.filename, handle->nsp, handle->index));
    } else if (pp.kind == sphaira::mtp::PathKind::ForwardersFile) {
        R_TRY(FindForwarderFile(pp.filename, handle->nsp, handle->index));
    } else if (pp.kind == sphaira::mtp::PathKind::SeparateFile) {
        R_TRY(FindFile(pp.game, pp.filename, handle->nsp, handle->index));
    } else {
        R_THROW(FsError_PathNotFound);
    }

    auto raw = handle.release();
    std::memcpy(&out_file->s, &raw, sizeof(raw));
    R_SUCCEED();
}

Result FsGameProxy::GetFileSize(FsFile *file, s64 *out_size) {
    FileHandle* h;
    std::memcpy(&h, &file->s, sizeof(h));
    if (h->is_sd) {
        return h->sd.GetSize(out_size);
    }
    if (!h->info.empty()) {
        *out_size = static_cast<s64>(h->info.size());
        R_SUCCEED();
    }
    *out_size = h->nsp->entries[h->index].nsp_size;
    R_SUCCEED();
}

Result FsGameProxy::ReadFile(FsFile *file, s64 off, void *buf, u64 read_size, u32 option, u64 *out_bytes_read) {
    FileHandle* h;
    std::memcpy(&h, &file->s, sizeof(h));

    if (h->is_sd) {
        return h->sd.Read(off, buf, read_size, option, out_bytes_read);
    }
    if (!h->info.empty()) {
        *out_bytes_read = 0;
        if (off < 0 || off >= static_cast<s64>(h->info.size())) {
            R_SUCCEED();
        }
        const auto n = std::min<u64>(read_size, static_cast<u64>(h->info.size() - off));
        std::memcpy(buf, h->info.data() + off, n);
        *out_bytes_read = n;
        R_SUCCEED();
    }

    const auto rc = h->nsp->entries[h->index].Read(buf, off, read_size, out_bytes_read);
    if (m_is_file_based_emummc) {
        svcSleepThread(2e+6); // 2ms, same throttle the sd card dump uses.
    }
    return rc;
}

void FsGameProxy::CloseFile(FsFile *file) {
    FileHandle* h;
    std::memcpy(&h, &file->s, sizeof(h));
    delete h;
    std::memset(file, 0, sizeof(*file));
}

Result FsGameProxy::OpenDirectory(const char *path, u32 mode, FsDir *out_dir) {
    const auto pp = Parse(path);
    auto handle = std::make_unique<DirHandle>();

    switch (pp.kind) {
        case sphaira::mtp::PathKind::Root: {
            if (m_layout == sphaira::mtp::GamesLayout::Compatible) {
                if (mode & FsDirOpenMode_ReadFiles) {
                    R_TRY(BuildMergedCacheIfNeeded());
                    SCOPED_MUTEX(&m_cache_mutex);
                    for (const auto& [name, nsp] : m_merged_cache) {
                        if (!nsp->listing.empty()) {
                            handle->entries.emplace_back(nsp->listing[0]);
                        }
                    }
                    handle->entries.emplace_back(InfoDirEntry(""));
                }
                if (mode & FsDirOpenMode_ReadDirs) {
                    handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                }
            } else if (m_layout == sphaira::mtp::GamesLayout::Separate) {
                if (mode & FsDirOpenMode_ReadDirs) {
                    for (const auto& [name, game] : m_games) {
                        handle->entries.emplace_back(MakeVirtualDirEntry(name));
                    }
                    handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                }
                if (mode & FsDirOpenMode_ReadFiles) {
                    handle->entries.emplace_back(InfoDirEntry(""));
                }
            } else {
                if (mode & FsDirOpenMode_ReadDirs) {
                    handle->entries.emplace_back(MakeVirtualDirEntry(m_names.merged));
                    handle->entries.emplace_back(MakeVirtualDirEntry(m_names.separate));
                    handle->entries.emplace_back(MakeVirtualDirEntry(m_names.forwarders));
                }
                if (mode & FsDirOpenMode_ReadFiles) {
                    handle->entries.emplace_back(InfoDirEntry(""));
                }
            }
            break;
        }

        case sphaira::mtp::PathKind::MergedDir: {
            if (mode & FsDirOpenMode_ReadFiles) {
                R_TRY(BuildMergedCacheIfNeeded());
                SCOPED_MUTEX(&m_cache_mutex);
                for (const auto& [name, nsp] : m_merged_cache) {
                    if (!nsp->listing.empty()) {
                        handle->entries.emplace_back(nsp->listing[0]);
                    }
                }
                handle->entries.emplace_back(InfoDirEntry("merged"));
            }
            break;
        }

        case sphaira::mtp::PathKind::ForwardersDir: {
            if (mode & FsDirOpenMode_ReadFiles) {
                R_TRY(BuildForwarderCacheIfNeeded());
                SCOPED_MUTEX(&m_cache_mutex);
                for (const auto& [name, nsp] : m_forwarder_cache) {
                    if (!nsp->listing.empty()) {
                        handle->entries.emplace_back(nsp->listing[0]);
                    }
                }
                handle->entries.emplace_back(InfoDirEntry("forwarders"));
            }
            break;
        }

        case sphaira::mtp::PathKind::SeparateDir: {
            if (mode & FsDirOpenMode_ReadDirs) {
                for (const auto& [name, game] : m_games) {
                    handle->entries.emplace_back(MakeVirtualDirEntry(name));
                }
            }
            if (mode & FsDirOpenMode_ReadFiles) {
                handle->entries.emplace_back(InfoDirEntry("separate"));
            }
            break;
        }

        case sphaira::mtp::PathKind::SeparateGameDir: {
            std::shared_ptr<GameNsp> nsp;
            R_TRY(GetNsp(pp.game, nsp));
            if (mode & FsDirOpenMode_ReadFiles) {
                handle->entries = nsp->listing;
            }
            if (mode & FsDirOpenMode_ReadDirs) {
                handle->entries.emplace_back(MakeVirtualDirEntry(m_names.mods));
            }
            break;
        }

        case sphaira::mtp::PathKind::ModsPath: {
            fs::FsPath sd_path;
            R_TRY(ModsSdPath(pp, sd_path));
            fs::Dir dir;
            if (const auto rc = m_sd.OpenDirectory(sd_path, mode, &dir); R_SUCCEEDED(rc)) {
                R_TRY(dir.ReadAll(handle->entries));
            } else if (!pp.filename.empty()) {
                return rc;
            }
            break;
        }

        default:
            R_THROW(FsError_PathNotFound);
    }

    auto raw = handle.release();
    std::memcpy(&out_dir->s, &raw, sizeof(raw));
    log_write("[MTP-GAMES] OpenDirectory(%s) kind=%d\n", path, static_cast<int>(pp.kind));
    R_SUCCEED();
}

Result FsGameProxy::ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) {
    DirHandle* h;
    std::memcpy(&h, &d->s, sizeof(h));

    max_entries = std::min<s64>(h->entries.size() - h->index, max_entries);
    std::memcpy(buf, h->entries.data() + h->index, max_entries * sizeof(*buf));
    h->index += max_entries;
    *out_total_entries = max_entries;
    R_SUCCEED();
}

Result FsGameProxy::GetDirectoryEntryCount(FsDir *d, s64 *out_count) {
    DirHandle* h;
    std::memcpy(&h, &d->s, sizeof(h));
    *out_count = h->entries.size();
    R_SUCCEED();
}

void FsGameProxy::CloseDirectory(FsDir *d) {
    DirHandle* h;
    std::memcpy(&h, &d->s, sizeof(h));
    delete h;
    std::memset(d, 0, sizeof(*d));
}

bool FsGameProxy::MultiThreadTransfer(s64 size, bool read) {
    return false;
}

std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsGameProxy(const char* name, const char* display_name) {
    return std::make_shared<FsGameProxy>(name, display_name);
}

} // namespace sphaira::haze
