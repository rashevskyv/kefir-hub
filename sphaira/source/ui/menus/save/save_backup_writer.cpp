#include "save_backup_writer.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <ctime>
#include <cstring>
#include <algorithm>
#include <unistd.h>

namespace sphaira::ui::menu::save {

voidpf RecoveryOpen(voidpf opaque, const void* filename, int mode) {
    auto* ctx = static_cast<RecoveryStreamContext*>(opaque);
    const char* mode_fopen = nullptr;
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ) {
        mode_fopen = "rb";
    } else if (mode & ZLIB_FILEFUNC_MODE_EXISTING) {
        mode_fopen = "r+b";
    } else if (mode & ZLIB_FILEFUNC_MODE_CREATE) {
        mode_fopen = "wb";
    } else {
        return nullptr;
    }
    auto f = std::fopen(static_cast<const char*>(filename), mode_fopen);
    if (f) {
        std::setvbuf(f, nullptr, _IOFBF, 1024 * 512);
        if (ctx) {
            ctx->fp = f;
        }
    }
    return f;
}

ZPOS64_T RecoveryTell(voidpf /*opaque*/, voidpf stream) {
    return std::ftell(static_cast<std::FILE*>(stream));
}

long RecoverySeek(voidpf /*opaque*/, voidpf stream, ZPOS64_T offset, int origin) {
    return std::fseek(static_cast<std::FILE*>(stream), offset, origin);
}

uLong RecoveryRead(voidpf /*opaque*/, voidpf stream, void* buf, uLong size) {
    return std::fread(buf, 1, size, static_cast<std::FILE*>(stream));
}

uLong RecoveryWrite(voidpf opaque, voidpf stream, const void* buf, uLong size) {
    auto* ctx = static_cast<RecoveryStreamContext*>(opaque);
    auto file = static_cast<std::FILE*>(stream);
    const auto written = std::fwrite(buf, 1, size, file);
    if (written != size && ctx) {
        ctx->write_failed = true;
    }
    return written;
}

int RecoveryClose(voidpf opaque, voidpf stream) {
    auto* ctx = static_cast<RecoveryStreamContext*>(opaque);
    auto file = static_cast<std::FILE*>(stream);
    if (!file) {
        return 0;
    }
    if (std::fflush(file) != 0) {
        if (ctx) ctx->flush_failed = true;
    }
    const int fd = fileno(file);
    if (fd == -1) {
        if (ctx) ctx->sync_failed = true;
    } else if (fsync(fd) != 0) {
        if (ctx) ctx->sync_failed = true;
    }
    const int rc = std::fclose(file);
    if (rc != 0) {
        if (ctx) ctx->close_failed = true;
    }
    if (ctx && ctx->fp == file) {
        ctx->fp = nullptr;
    }
    return rc;
}

int RecoveryError(voidpf /*opaque*/, voidpf stream) {
    auto file = static_cast<std::FILE*>(stream);
    if (file) {
        return std::ferror(file);
    }
    return 0;
}

bool IsInvalidSavePathChar(char c) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20) {
        return true;
    }
    switch (c) {
        case ':': case '*': case '?': case '"':
        case '<': case '>': case '|': case '\\':
            return true;
        default:
            return false;
    }
}

Result WriteSaveBackupZip(
    ProgressBox* pbox,
    fs::Fs* target_fs,
    const fs::FsPath& temp_path,
    fs::Fs* save_fs,
    const Entry& e,
    const FsSaveDataExtraData& extra,
    const filebrowser::FsDirCollections& collections,
    const std::string& account_name,
    bool dbi_format,
    bool compressed,
    bool recovery_mode,
    bool checked_stream) {

    const auto t = (extra.timestamp != 0) ? static_cast<time_t>(extra.timestamp) : std::time(nullptr);
    const auto tm = std::localtime(&t);

    zip_fileinfo zip_info_default{};
    zip_info_default.tmz_date.tm_sec = tm->tm_sec;
    zip_info_default.tmz_date.tm_min = tm->tm_min;
    zip_info_default.tmz_date.tm_hour = tm->tm_hour;
    zip_info_default.tmz_date.tm_mday = tm->tm_mday;
    zip_info_default.tmz_date.tm_mon = tm->tm_mon;
    zip_info_default.tmz_date.tm_year = tm->tm_year;

    const bool use_checked_stream = recovery_mode || checked_stream;
    const auto file_download = use_checked_stream || App::IsApplet() || e.size >= 1024ULL * 1024ULL * 1024ULL;

    RecoveryStreamContext rec_ctx{};
    mz::MzMem mz_mem{};
    zlib_filefunc64_def file_func{};
    if (use_checked_stream) {
        file_func.zopen64_file = RecoveryOpen;
        file_func.zread_file = RecoveryRead;
        file_func.zwrite_file = RecoveryWrite;
        file_func.ztell64_file = RecoveryTell;
        file_func.zseek64_file = RecoverySeek;
        file_func.zclose_file = RecoveryClose;
        file_func.zerror_file = RecoveryError;
        file_func.opaque = &rec_ctx;
    } else if (!file_download) {
        mz::FileFuncMem(&mz_mem, &file_func);
    } else {
        mz::FileFuncStdio(&file_func);
    }

    {
        auto zfile = zipOpen2_64(temp_path, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        bool zip_archive_open = true;
        ON_SCOPE_EXIT({
            if (zip_archive_open && zfile) {
                zipClose(zfile, nullptr);
            }
        });

        // add save meta (sphaira format only, dbi stores its own meta below).
        if (!dbi_format) {
            const NXSaveMeta meta{
                .magic = NX_SAVE_META_MAGIC,
                .version = NX_SAVE_META_VERSION,
                .attr = extra.attr,
                .owner_id = extra.owner_id,
                .timestamp = extra.timestamp,
                .flags = extra.flags,
                .unk_x54 = extra.unk_x54,
                .data_size = extra.data_size,
                .journal_size = extra.journal_size,
                .commit_id = extra.commit_id,
                .raw_size = e.size,
            };

            R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, NX_SAVE_META_NAME, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
            bool meta_open = true;
            ON_SCOPE_EXIT({
                if (meta_open && zfile) {
                    zipCloseFileInZip(zfile);
                }
            });
            R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, &meta, sizeof(meta)), Result_ZipWriteInFileInZip);
            meta_open = false;
            R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
        }

        // dbi stores explicit directory entries with absolute paths.
        if (dbi_format) {
            for (const auto& collection : collections) {
                if (collection.path == "/") {
                    continue;
                }

                fs::FsPath dir_name;
                std::snprintf(dir_name, sizeof(dir_name), "%s/", collection.path.s);
                R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, dir_name, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
                bool dir_entry_open = true;
                ON_SCOPE_EXIT({
                    if (dir_entry_open && zfile) {
                        zipCloseFileInZip(zfile);
                    }
                });
                dir_entry_open = false;
                R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
            }
        } else if (recovery_mode) {
            for (const auto& collection : collections) {
                if (collection.path == "/") {
                    continue;
                }
                const char* rel_dir = collection.path.s;
                while (*rel_dir == '/') {
                    rel_dir++;
                }
                if (*rel_dir == '\0') {
                    continue;
                }
                fs::FsPath dir_name;
                std::snprintf(dir_name, sizeof(dir_name), "%s/", rel_dir);
                R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, dir_name, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
                bool dir_entry_open = true;
                ON_SCOPE_EXIT({
                    if (dir_entry_open && zfile) {
                        zipCloseFileInZip(zfile);
                    }
                });
                dir_entry_open = false;
                R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
            }
        }

        const auto zip_add = [&](const fs::FsPath& file_path) -> Result {
            const char* file_name_in_zip = file_path.s;

            // strip root path (/ or ums0:)
            if (!std::strncmp(file_name_in_zip, save_fs->Root(), std::strlen(save_fs->Root()))) {
                file_name_in_zip += std::strlen(save_fs->Root());
            }

            // root paths are banned in zips, they will warn when extracting otherwise.
            while (file_name_in_zip[0] == '/') {
                file_name_in_zip++;
            }

            // dbi stores entries with absolute paths.
            fs::FsPath dbi_name;
            if (dbi_format) {
                std::snprintf(dbi_name, sizeof(dbi_name), "/%s", file_name_in_zip);
                file_name_in_zip = dbi_name.s;
            }

            pbox->NewTransfer(file_name_in_zip);

            const auto level = compressed ? Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION;
            if (ZIP_OK != zipOpenNewFileInZip(zfile, file_name_in_zip, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, level)) {
                log_write("failed to add zip for %s\n", file_path.s);
                R_THROW(Result_ZipOpenNewFileInZip);
            }
            bool file_in_zip_open = true;
            ON_SCOPE_EXIT({
                if (file_in_zip_open && zfile) {
                    zipCloseFileInZip(zfile);
                }
            });

            R_TRY(thread::TransferZip(pbox, zfile, save_fs, file_path));
            file_in_zip_open = false;
            R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
            R_SUCCEED();
        };

        // loop through every save file and store to zip.
        for (const auto& collection : collections) {
            for (const auto& file : collection.files) {
                const auto file_path = fs::AppendPath(collection.path, file.name);
                R_TRY(zip_add(file_path));
            }
        }

        // add the dbi meta entries last, matching real dbi backups.
        if (dbi_format) {
            const auto write_meta_file = [&](const char* name, const void* data, size_t size) -> Result {
                R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, name, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
                bool meta_open = true;
                ON_SCOPE_EXIT({
                    if (meta_open && zfile) {
                        zipCloseFileInZip(zfile);
                    }
                });
                R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, data, size), Result_ZipWriteInFileInZip);
                meta_open = false;
                R_UNLESS(ZIP_OK == zipCloseFileInZip(zfile), Result_ZipWriteInFileInZip);
                R_SUCCEED();
            };

            const auto account = e.save_data_type == FsSaveDataType_Account
                ? account_name
                : std::string{GetSaveTypeLabel(e.save_data_type)};

            const char* space = "User";
            switch (e.save_data_space_id) {
                case FsSaveDataSpaceId_System:
                case FsSaveDataSpaceId_SdSystem:
                case FsSaveDataSpaceId_ProperSystem:
                    space = "System";
                    break;
                case FsSaveDataSpaceId_Temporary:
                    space = "Temporary";
                    break;
            }

            const auto now_dbi = std::time(nullptr);
            const auto now_tm = *std::localtime(&now_dbi);

            char info[0x400];
            std::snprintf(info, sizeof(info),
                "TitleId=%016lX\n"
                "TitleName=%s\n"
                "BackupDate=%04d-%02d-%02d %02d:%02d:%02d\n"
                "Account=%s\n"
                "Space=%s",
                e.application_id,
                e.GetName(),
                now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec,
                account.c_str(),
                space);

            R_TRY(write_meta_file(DBI_SAVE_INFO_NAME, info, std::strlen(info)));
            R_TRY(write_meta_file(DBI_SAVE_EXTRA_NAME, &extra, sizeof(extra)));
        }

        zip_archive_open = false;
        R_UNLESS(ZIP_OK == zipClose(zfile, "sphaira v" APP_VERSION_HASH), Result_ZipWriteInFileInZip);
    }

    if (use_checked_stream) {
        R_UNLESS(!rec_ctx.write_failed, Result_ZipWriteInFileInZip);
        R_UNLESS(!rec_ctx.flush_failed, Result_FsUnknownStdioError);
        R_UNLESS(!rec_ctx.sync_failed, Result_FsUnknownStdioError);
        R_UNLESS(!rec_ctx.close_failed, Result_FsUnknownStdioError);
        R_UNLESS(rec_ctx.fp == nullptr, Result_FsUnknownStdioError);

        const auto sdmc_rc = fsdevCommitDevice("sdmc");
        R_TRY(sdmc_rc);
        fs::FsNativeSd sd_fs;
        R_TRY(sd_fs.GetFsOpenResult());
        R_TRY(sd_fs.Commit());
    } else if (!file_download && target_fs) {
        // if we dumped the save to ram, flush the data to file.
        const auto is_file_based_emummc = App::IsFileBaseEmummc();
        pbox->NewTransfer("Flushing zip to file");
        R_TRY(target_fs->CreateFile(temp_path, mz_mem.buf.size(), 0));

        fs::File file;
        R_TRY(target_fs->OpenFile(temp_path, FsOpenMode_Write, &file));

        R_TRY(thread::Transfer(pbox, mz_mem.buf.size(),
            [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                size = std::min<s64>(size, mz_mem.buf.size() - off);
                std::memcpy(data, mz_mem.buf.data() + off, size);
                *bytes_read = size;
                R_SUCCEED();
            },
            [&](const void* data, s64 off, s64 size) -> Result {
                const auto rc = file.Write(off, data, size, FsWriteOption_None);
                if (is_file_based_emummc) {
                    svcSleepThread(2e+6); // 2ms
                }
                return rc;
            }
        ));
    }

    R_SUCCEED();
}

} // namespace sphaira::ui::menu::save
