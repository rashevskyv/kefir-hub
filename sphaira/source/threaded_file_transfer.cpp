#include "threaded_file_transfer.hpp"
#include "threaded_file_transfer_preflight.hpp"
#include "threaded_file_transfer_zip_io.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "minizip_helper.hpp"
#include "ui/progress_box.hpp"

#include <vector>
#include <algorithm>
#include <cstring>
#include <limits>
#include <minizip/unzip.h>

namespace sphaira::thread {

Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode, bool save_dbi_compat, bool checked_native_save, s64 checked_save_journal_size) {
    if (checked_native_save) {
        if (!fs || !fs->IsNative()) {
            R_THROW(FsError_NotImplemented);
        }
        if (checked_save_journal_size < 0) {
            R_THROW(FsError_InvalidSize);
        }
    }

    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }

    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        R_THROW(FsError_InvalidSize);
    }
    if (ginfo.number_entry == 0) {
        R_THROW(FsError_InvalidSize);
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    const auto base_len = base_path.length();
    const bool base_needs_slash = (base_len > 0 && base_path[base_len - 1] != '/');

    bool seen_dbi_marker_sizing = false;
    s64 total_size = 0;
    for (s64 i = 0; i < entry_count; i++) {
        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile while sizing archive\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("failed to get current info while sizing archive\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        fs::FsPath name;
        bool is_marker = false;
        R_TRY(ResolveArchiveEntryName(info, name_buf, save_dbi_compat, name, &is_marker));
        if (is_marker) {
            if (seen_dbi_marker_sizing) {
                log_write("duplicate DBI root marker in sizing pass\n");
                R_THROW(FsError_PathAlreadyExists);
            }
            seen_dbi_marker_sizing = true;
            continue;
        }

        const auto full_path_len = base_len + (base_needs_slash ? 1 : 0) + std::strlen(name.s);
        if (full_path_len + 1 > sizeof(fs::FsPath)) {
            log_write("archive entry output path exceeds buffer (%zu bytes)\n", full_path_len + 1);
            R_THROW(FsError_TooLongPath);
        }

        if (info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())) {
            log_write("archive uncompressed size exceeds s64 maximum\n");
            R_THROW(FsError_InvalidSize);
        }

        if (static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(total_size) < info.uncompressed_size) {
            log_write("archive total uncompressed size exceeds s64 maximum\n");
            R_THROW(FsError_InvalidSize);
        }

        total_size += static_cast<s64>(info.uncompressed_size);
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    const auto use_entry_progress = total_size == 0;
    const s64 progress_total = use_entry_progress ? entry_count : total_size;
    s64 progress_offset = 0;

    pbox->ResetTransferProgress();
    pbox->UpdateTransfer(0, progress_total);

    bool seen_dbi_marker = false;
    for (s64 i = 0; i < entry_count; i++) {
        R_TRY(pbox->ShouldExitResult());

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file\n");
            R_THROW(Result_UnzOpenCurrentFile);
        }
        bool curr_file_open = true;
        ON_SCOPE_EXIT({
            if (curr_file_open && zfile) {
                unzCloseCurrentFile(zfile);
            }
        });

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), 0, 0, 0, 0)) {
            log_write("failed to get current info\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        ResolvedDestinationEntry resolved{};
        R_TRY(ResolveArchiveDestinationEntry(info, name_buf, base_path, filter, save_dbi_compat, resolved));

        const auto entry_progress_start = progress_offset;
        const s64 entry_progress_size = use_entry_progress ? 1 : static_cast<s64>(info.uncompressed_size);
        const auto update_progress = [&](s64 bytes) {
            progress_offset = std::min(progress_offset + bytes, progress_total);
            pbox->UpdateTransfer(progress_offset, progress_total);
        };
        const auto finish_entry = [&]() {
            progress_offset = std::min(entry_progress_start + entry_progress_size, progress_total);
            pbox->UpdateTransfer(progress_offset, progress_total);
        };

        if (resolved.is_dbi_root_marker) {
            if (seen_dbi_marker) {
                log_write("duplicate DBI root marker in extraction loop\n");
                curr_file_open = false;
                unzCloseCurrentFile(zfile);
                R_THROW(FsError_PathAlreadyExists);
            }
            seen_dbi_marker = true;

            char drain_buf[128];
            int zr = 0;
            do {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        curr_file_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                zr = unzReadCurrentFile(zfile, drain_buf, sizeof(drain_buf));
                if (zr < 0) {
                    curr_file_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(Result_UnzReadCurrentFile);
                }
                if (zr > 0) {
                    curr_file_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(FsError_InvalidSize);
                }
            } while (zr > 0);

            curr_file_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            continue;
        }

        if (!resolved.keep) {
            curr_file_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            finish_entry();
            continue;
        }

        if (resolved.is_directory) {
            if (checked_native_save) {
                const auto dir_rc = CreateDirectoryChecked(pbox, fs, resolved.path);
                curr_file_open = false;
                const int close_res = unzCloseCurrentFile(zfile);
                R_TRY(dir_rc);
                if (close_res == UNZ_CRCERROR) {
                    R_THROW(0x8);
                }
                R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
                finish_entry();
            } else {
                Result rc;
                if (R_FAILED(rc = fs->CreateDirectoryRecursively(resolved.path)) && rc != FsError_PathAlreadyExists) {
                    log_write("failed to create folder: %s 0x%04X\n", resolved.path.s, rc);
                    curr_file_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(rc);
                }
                curr_file_open = false;
                const int close_res = unzCloseCurrentFile(zfile);
                if (close_res == UNZ_CRCERROR) {
                    R_THROW(0x8);
                }
                R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
                finish_entry();
            }
        } else {
            const auto unzip_rc = TransferUnzipInternal(pbox, zfile, fs, resolved.path, info.uncompressed_size, info.crc, mode,
                [&](s64 bytes_written) {
                    update_progress(bytes_written);
                },
                false,
                checked_native_save,
                checked_save_journal_size
            );
            curr_file_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            R_TRY(unzip_rc);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            finish_entry();
        }
    }

    R_SUCCEED();
}

Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode, bool save_dbi_compat, bool checked_native_save, s64 checked_save_journal_size) {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return TransferUnzipAll(pbox, zfile, fs, base_path, filter, mode, save_dbi_compat, checked_native_save, checked_save_journal_size);
}

} // namespace sphaira::thread
