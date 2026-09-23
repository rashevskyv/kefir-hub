#include "threaded_file_transfer.hpp"
#include "threaded_file_transfer_core.hpp"
#include "threaded_file_transfer_zip_io.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "ui/progress_box.hpp"

#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string_view>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::thread {

Result CreateDirectoryChecked(ui::ProgressBox* pbox, fs::Fs* fs, const fs::FsPath& dir_path) {
    if (!fs || !fs->IsNative()) {
        return FsError_NotImplemented;
    }
    auto* native_fs = static_cast<fs::FsNative*>(fs);

    std::string_view path_view{dir_path.s};
    while (path_view.length() > 1 && path_view.back() == '/') {
        path_view.remove_suffix(1);
    }
    if (path_view.empty() || path_view == "/") {
        return 0;
    }

    if (path_view.front() == '/') {
        path_view.remove_prefix(1);
    }

    fs::FsPath current_path{"/"};
    std::string_view sv{path_view};
    while (!sv.empty()) {
        const auto slash_pos = sv.find('/');
        const auto part = (slash_pos == std::string_view::npos) ? sv : sv.substr(0, slash_pos);
        if (slash_pos == std::string_view::npos) {
            sv = {};
        } else {
            sv.remove_prefix(slash_pos + 1);
        }

        if (part.empty()) {
            continue;
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        const auto cur_len = current_path.length();
        const bool need_slash = (cur_len > 0 && current_path[cur_len - 1] != '/');
        const auto part_len = part.size();
        if (cur_len + (need_slash ? 1 : 0) + part_len + 1 > sizeof(fs::FsPath)) {
            return FsError_TooLongPath;
        }
        if (need_slash) {
            current_path += '/';
        }
        current_path += part;

        const auto create_rc = fsFsCreateDirectory(&native_fs->m_fs, current_path.s);
        if (R_SUCCEEDED(create_rc)) {
            R_TRY(fs->Commit());
        } else if (create_rc == FsError_PathAlreadyExists) {
            FsDirEntryType type{};
            R_TRY(fs->GetEntryType(current_path, &type));
            R_UNLESS(type == FsDirEntryType_Dir, FsError_PathAlreadyExists);
        } else {
            return create_rc;
        }
    }

    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    return 0;
}

Result TransferUnzipInternal(
    ui::ProgressBox* pbox,
    void* zfile,
    fs::Fs* fs,
    const fs::FsPath& path,
    s64 size,
    u32 crc32,
    Mode mode,
    UnzipProgressCallback progress,
    bool update_progress,
    bool checked_native_save,
    s64 checked_save_journal_size) {
    if (checked_native_save) {
        if (!fs || !fs->IsNative()) {
            return FsError_NotImplemented;
        }
        if (size < 0) {
            return FsError_InvalidSize;
        }
        if (checked_save_journal_size < 0) {
            return FsError_InvalidSize;
        }

        // ponytail: declared-size payload cap/per-read commits do not measure metadata/allocation/block overhead or actual free journal; even one operation may exhaust journal; proven budgeting remains queued.
        s64 request_cap = static_cast<s64>(SMALL_BUFFER_SIZE);
        if (checked_save_journal_size > 0 && checked_save_journal_size < request_cap) {
            request_cap = checked_save_journal_size;
        }

        // Implicit parent directories component by component
        const char* last_slash = std::strrchr(path.s, '/');
        if (last_slash && last_slash > path.s) {
            fs::FsPath parent_dir{};
            std::snprintf(parent_dir, sizeof(parent_dir), "%.*s", static_cast<int>(last_slash - path.s), path.s);
            R_TRY(CreateDirectoryChecked(pbox, fs, parent_dir));
        }

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        auto* native_fs = static_cast<fs::FsNative*>(fs);

        // New checked file: native fsFsCreateFile at validated full s64 size, option 0,
        // then separately CHECK native filesystem Commit before opening payload handle.
        // Unexpected existing destination FILE is an error (do not accept PathAlreadyExists).
        const auto create_rc = fsFsCreateFile(&native_fs->m_fs, path.s, size, 0);
        R_TRY(create_rc);

        const auto commit_create_rc = fs->Commit();
        R_TRY(commit_create_rc);

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        fs::File f;
        auto close_and_invalidate = [&]() {
            if (serviceIsActive(&f.m_native.s)) {
                fsFileClose(&f.m_native);
            }
            f.m_native = {};
            f.m_fs = nullptr;
        };
        ON_SCOPE_EXIT({
            close_and_invalidate();
        });

        if (size == 0) {
            const auto open_rc = fs->OpenFile(path, FsOpenMode_Write, &f);
            if (R_FAILED(open_rc)) {
                return open_rc;
            }

            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    close_and_invalidate();
                    return exit_rc;
                }
            }

            const auto flush_rc = fsFileFlush(&f.m_native);
            close_and_invalidate();
            R_TRY(flush_rc);

            R_TRY(fs->Commit());

            if (pbox) {
                R_TRY(pbox->ShouldExitResult());
            }

            R_UNLESS(!crc32 || crc32 == 0, 0x8);
            return 0;
        }

        const auto open_rc = fs->OpenFile(path, FsOpenMode_Write, &f);
        if (R_FAILED(open_rc)) {
            return open_rc;
        }

        std::vector<u8> buffer(request_cap);
        s64 remaining = size;
        s64 current_offset = 0;
        u32 crc32_out = 0;

        while (remaining > 0) {
            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    close_and_invalidate();
                    return exit_rc;
                }
            }

            const s64 to_read_s64 = std::min(request_cap, remaining);
            const int to_read = static_cast<int>(to_read_s64);

            const int read_res = unzReadCurrentFile(zfile, buffer.data(), to_read);
            if (read_res <= 0) {
                close_and_invalidate();
                log_write("failed to read zip file: %s %d\n", path.s, read_res);
                return Result_UnzReadCurrentFile;
            }
            if (read_res > to_read || static_cast<s64>(read_res) > remaining) {
                close_and_invalidate();
                return FsError_InvalidSize;
            }

            if (crc32) {
                crc32_out = crc32CalculateWithSeed(crc32_out, buffer.data(), read_res);
            }

            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    close_and_invalidate();
                    return exit_rc;
                }
            }

            const auto write_rc = fsFileWrite(&f.m_native, current_offset, buffer.data(), read_res, FsWriteOption_None);
            if (R_FAILED(write_rc)) {
                close_and_invalidate();
                return write_rc;
            }

            const auto flush_rc = fsFileFlush(&f.m_native);
            close_and_invalidate();
            R_TRY(flush_rc);

            const auto commit_rc = fs->Commit();
            R_TRY(commit_rc);

            current_offset += read_res;
            remaining -= read_res;
            if (progress) {
                progress(read_res);
            }

            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    return exit_rc;
                }
            }

            if (remaining > 0) {
                const auto reopen_rc = fs->OpenFile(path, FsOpenMode_Write, &f);
                if (R_FAILED(reopen_rc)) {
                    return reopen_rc;
                }
            }
        }

        if (current_offset != size) {
            return FsError_InvalidSize;
        }

        R_UNLESS(!crc32 || crc32 == crc32_out, 0x8);

        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        return 0;
    }

    Result rc;
    if (R_FAILED(rc = fs->CreateDirectoryRecursivelyWithPath(path)) && rc != FsError_PathAlreadyExists) {
        log_write("failed to create folder: %s 0x%04X\n", path.s, rc);
        R_THROW(rc);
    }

    if (R_FAILED(rc = fs->CreateFile(path, size, 0)) && rc != FsError_PathAlreadyExists) {
        log_write("failed to create file: %s 0x%04X\n", path.s, rc);
        R_THROW(rc);
    }

    fs::File f;
    R_TRY(fs->OpenFile(path, FsOpenMode_Write, &f));

    // only update the size if this is an existing file.
    if (rc == FsError_PathAlreadyExists) {
        R_TRY(f.SetSize(size));
    }

    // NOTES: do not use temp file with rename / delete after as it massively slows
    // down small file transfers (RA 21s -> 50s).
    u32 crc32_out{};
    const auto transfer_rc = thread::TransferInternal(pbox, size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto result = unzReadCurrentFile(zfile, data, size);
            if (result <= 0) {
                log_write("failed to read zip file: %s %d\n", path.s, result);
                R_THROW(Result_UnzReadCurrentFile);
            }

            if (crc32) {
                crc32_out = crc32CalculateWithSeed(crc32_out, data, result);
            }

            *bytes_read = result;
            R_SUCCEED();
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            R_TRY(f.Write(off, data, size, FsWriteOption_None));
            if (progress) {
                progress(size);
            }
            R_SUCCEED();
        },
        nullptr, mode, SMALL_BUFFER_SIZE, update_progress ? TransferProgressCallback{} : TransferProgressCallback{[](s64, s64){}}
    );

    R_TRY(transfer_rc);
    // validate crc32 (if set in the info).
    R_UNLESS(!crc32 || crc32 == crc32_out, 0x8);
    R_SUCCEED();
}

Result TransferUnzip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, s64 size, u32 crc32, Mode mode, bool update_progress) {
    return TransferUnzipInternal(pbox, zfile, fs, path, size, crc32, mode, nullptr, update_progress);
}

Result TransferZip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, u32* crc32, Mode mode) {
    fs::File f;
    R_TRY(fs->OpenFile(path, FsOpenMode_Read, &f));

    s64 file_size;
    R_TRY(f.GetSize(&file_size));

    if (crc32) {
        *crc32 = 0;
    }

    return thread::TransferInternal(pbox, file_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = f.Read(off, data, size, FsReadOption_None, bytes_read);
            if (R_SUCCEEDED(rc) && crc32) {
                *crc32 = crc32CalculateWithSeed(*crc32, data, *bytes_read);
            }
            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            if (ZIP_OK != zipWriteInFileInZip(zfile, data, size)) {
                log_write("failed to write zip file: %s\n", path.s);
                R_THROW(Result_ZipWriteInFileInZip);
            }
            R_SUCCEED();
        },
        nullptr, mode, SMALL_BUFFER_SIZE
    );
}

} // namespace sphaira::thread
