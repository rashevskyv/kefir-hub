#include "haze/haze_game_proxy_internal.hpp"

namespace sphaira::haze {

Result FsGameProxy::GetTotalSpace(const char *path, s64 *out) {
    *out = 1024ULL * 1024ULL * 1024ULL * 256ULL;
    R_SUCCEED();
}

Result FsGameProxy::GetFreeSpace(const char *path, s64 *out) {
    // read-only drive: nothing can be written into it.
    *out = 0;
    R_SUCCEED();
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
    log_write("[MTP-GAMES] rejecting CreateFile(%s)\n", path);
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::DeleteFile(const char* path) {
    log_write("[MTP-GAMES] rejecting DeleteFile(%s)\n", path);
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::RenameFile(const char *old_path, const char *new_path) {
    log_write("[MTP-GAMES] rejecting RenameFile(%s)\n", old_path);
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::CreateDirectory(const char* path) {
    log_write("[MTP-GAMES] rejecting CreateDirectory(%s)\n", path);
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::DeleteDirectoryRecursively(const char* path) {
    log_write("[MTP-GAMES] rejecting DeleteDirectoryRecursively(%s)\n", path);
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::RenameDirectory(const char *old_path, const char *new_path) {
    log_write("[MTP-GAMES] rejecting RenameDirectory(%s)\n", old_path);
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::SetFileSize(FsFile *file, s64 size) {
    log_write("[MTP-GAMES] rejecting SetFileSize()\n");
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) {
    log_write("[MTP-GAMES] rejecting WriteFile()\n");
    R_THROW(FsError_NotImplemented);
}

Result FsGameProxy::OpenFile(const char *path, u32 mode, FsFile *out_file) {
    log_write("[MTP-GAMES] OpenFile(%s)\n", path);
    R_UNLESS(!(mode & (FsOpenMode_Write | FsOpenMode_Append)), FsError_NotImplemented);

    const auto pp = Parse(path);
    auto handle = std::make_unique<FileHandle>();

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
