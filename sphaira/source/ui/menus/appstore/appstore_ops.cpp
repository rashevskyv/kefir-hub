#include "ui/menus/appstore.hpp"
#include "ui/menus/appstore/appstore_internal.hpp"
#include "ui/menus/appstore_util.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "hasher.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"
#include <switch.h>
#include <physfs.h>
#include <minizip/unzip.h>
#include "minizip_helper.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ranges>
#include <vector>

namespace sphaira::ui::menu::appstore {
auto ExtractPhysfsArchive(ProgressBox* pbox, const fs::FsPath& archive_path, const fs::FsPath& dest_root) -> Result {
    if (!PHYSFS_isInit()) {
        PHYSFS_init(nullptr);
    }

    if (!PHYSFS_mount(archive_path.s, "temp_mount", 1)) {
        log_write("PHYSFS_mount failed: %s\n", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return Result_UnzOpen2_64;
    }
    ON_SCOPE_EXIT(PHYSFS_unmount(archive_path.s));

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    std::function<Result(const std::string&)> extract_dir = [&](const std::string& vdir) -> Result {
        char** files = PHYSFS_enumerateFiles(vdir.c_str());
        if (!files) return 0;
        ON_SCOPE_EXIT(PHYSFS_freeList(files));

        for (char** i = files; *i != nullptr; i++) {
            if (pbox && pbox->ShouldExit()) return Result_TransferCancelled;

            std::string sub_vpath = vdir.empty() ? *i : (vdir + "/" + *i);
            PHYSFS_Stat stat{};
            if (!PHYSFS_stat(sub_vpath.c_str(), &stat)) continue;

            std::string rel_path = sub_vpath;
            if (rel_path.starts_with("temp_mount/")) {
                rel_path = rel_path.substr(11);
            } else if (rel_path == "temp_mount") {
                continue;
            }

            std::string full_dest = dest_root.s;
            if (full_dest.empty() || full_dest.back() != '/') {
                full_dest += '/';
            }
            full_dest += rel_path;

            if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
                fs.CreateDirectoryRecursively(full_dest);
                R_TRY(extract_dir(sub_vpath));
            } else if (stat.filetype == PHYSFS_FILETYPE_REGULAR) {
                fs.CreateDirectoryRecursivelyWithPath(full_dest);
                if (pbox) {
                    pbox->NewTransfer(rel_path);
                }

                PHYSFS_File* f = PHYSFS_openRead(sub_vpath.c_str());
                if (!f) continue;
                ON_SCOPE_EXIT(PHYSFS_close(f));

                fs::File dest_file;
                fs.DeleteFile(full_dest);
                R_TRY(fs.CreateFile(full_dest, stat.filesize, 0));
                R_TRY(fs.OpenFile(full_dest, FsOpenMode_Write, &dest_file));

                std::vector<u8> buffer(128 * 1024);
                s64 write_offset = 0;
                while (true) {
                    if (pbox && pbox->ShouldExit()) return Result_TransferCancelled;
                    PHYSFS_sint64 read_bytes = PHYSFS_readBytes(f, buffer.data(), buffer.size());
                    if (read_bytes <= 0) break;
                    R_TRY(dest_file.Write(write_offset, buffer.data(), read_bytes, FsWriteOption_None));
                    write_offset += read_bytes;
                }
                dest_file.Close();
            }
        }
        return 0;
    };

    return extract_dir("temp_mount");
}

auto UninstallApp(ProgressBox* pbox, const Entry& entry) -> Result {
    const auto manifest = LoadAndParseManifest(entry);
    fs::FsNativeSd fs;

    if (manifest.empty()) {
        if (!entry.binary.empty()) {
            R_TRY(fs.DeleteFile(entry.binary));
        }
    } else {
        for (auto& e : manifest) {
            pbox->NewTransfer(e.path);

            const auto safe_buf = fs::AppendPath("/", e.path);
            // this will handle read only files, ie, hbmenu.nro
            if (R_FAILED(fs.DeleteFile(safe_buf))) {
                log_write("failed to delete file: %s\n", safe_buf.s);
            } else {
                log_write("deleted file: %s\n", safe_buf.s);
                svcSleepThread(1e+5);
                // todo: delete empty directories!
                // fs::delete_directory(safe_buf);
            }
        }
    }

    // remove directory, this will also delete manifest and info
    const auto dir = BuildPackageCachePath(entry);
    pbox->NewTransfer("Removing "_i18n + dir.toString());
    if (R_FAILED(fs.DeleteDirectoryRecursively(dir))) {
        log_write("failed to delete folder: %s\n", dir.s);
    } else {
        log_write("deleted: %s\n", dir.s);
    }

    R_SUCCEED();
}

// this is called by ProgressBox on a seperate thread
// it has 4 main steps
// 1. download the zip
// 2. md5 check the zip
// 3. parse manifest and unzip everything to placeholder
// 4. move everything from placeholder to normal location
auto InstallApp(ProgressBox* pbox, const Entry& entry) -> Result {
    const bool is_retroarch = IsRetroArchPackage(entry);
    const fs::FsPath zip_out = is_retroarch ? "/switch/sphaira/cache/appstore/temp.7z" : "/switch/sphaira/cache/appstore/temp.zip";
    std::vector<u8> buf(1024 * 512); // 512KiB

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    // check if we can download the entire zip to mem for faster download / extract times.
    // current limit is 300MiB, or disabled for applet mode or 7z archives.
    const auto file_download = is_retroarch || App::IsApplet() || entry.filesize >= 1024 * 1024 * 300;
    curl::ApiResult api_result{};

    // 1. download the archive
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Downloading "_i18n + entry.title);
        log_write("starting download\n");

        const auto url = BuildZipUrl(entry);
        curl::Api api{
            curl::Url{url},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        };

        if (file_download) {
            api.SetOption(curl::Path{zip_out});
            api_result = curl::ToFile(api);
        } else {
            api_result = curl::ToMemory(api);
        }

        if (pbox->ShouldExit()) {
            return Result_TransferCancelled;
        }
        R_UNLESS(api_result.success, Result_AppstoreFailedZipDownload);
    }

    ON_SCOPE_EXIT(fs.DeleteFile(zip_out));

    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }

    // 2. md5 check (skip for RetroArch Nightly as its buildbot hash differs from repo.json)
    if (!is_retroarch && !pbox->ShouldExit()) {
        pbox->NewTransfer("Checking MD5"_i18n);
        log_write("starting md5 check\n");

        std::string hash_out;
        if (file_download) {
            R_TRY(hash::Hash(pbox, hash::Type::Md5, &fs, zip_out, hash_out));
        } else {
            R_TRY(hash::Hash(pbox, hash::Type::Md5, api_result.data, hash_out));
        }

        if (strncasecmp(hash_out.data(), entry.md5.data(), entry.md5.length())) {
            log_write("bad md5: %.*s vs %.*s\n", 32, hash_out.data(), 32, entry.md5.c_str());
            R_THROW(Result_AppstoreFailedMd5);
        }
    }

    // Special handler for RetroArch Nightly 7z extraction
    if (is_retroarch) {
        if (!pbox->ShouldExit()) {
            pbox->NewTransfer("Extracting RetroArch Nightly"_i18n);
            R_TRY(ExtractPhysfsArchive(pbox, zip_out, "/"));

            // Write info.json so it's registered as installed Nightly
            const auto info_path = BuildInfoCachePath(entry);
            fs.CreateDirectoryRecursivelyWithPath(info_path);
            const std::string info_content = "{\"version\":\"Nightly\",\"name\":\"RetroNX\",\"title\":\"Retroarch\",\"binary\":\"/switch/retroarch_switch.nro\"}\n";
            fs::File info_file;
            fs.DeleteFile(info_path);
            if (R_SUCCEEDED(fs.CreateFile(info_path, info_content.size(), 0)) &&
                R_SUCCEEDED(fs.OpenFile(info_path, FsOpenMode_Write, &info_file))) {
                info_file.Write(0, info_content.data(), info_content.size(), FsWriteOption_Flush);
                info_file.Close();
            }
        }
        R_SUCCEED();
    }

    mz::MzSpan mz_span{api_result.data};
    zlib_filefunc64_def file_func;
    if (!file_download) {
        mz::FileFuncSpan(&mz_span, &file_func);
    } else {
        mz::FileFuncStdio(&file_func);
    }

    // 3. extract the zip
    if (!pbox->ShouldExit()) {
        auto zfile = unzOpen2_64(zip_out, &file_func);
        R_UNLESS(zfile, Result_UnzOpen2_64);
        ON_SCOPE_EXIT(unzClose(zfile));

        // get manifest
        if (UNZ_END_OF_LIST_OF_FILE == unzLocateFile(zfile, "manifest.install", 0)) {
            log_write("failed to find manifest.install\n");
            R_THROW(Result_UnzLocateFile);
        }

        ManifestEntries new_manifest;
        const auto old_manifest = LoadAndParseManifest(entry);
        {
            if (UNZ_OK != unzOpenCurrentFile(zfile)) {
                log_write("failed to open current file\n");
                R_THROW(Result_UnzOpenCurrentFile);
            }
            ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

            unz_file_info64 info;
            if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, 0, 0, 0, 0, 0, 0)) {
                log_write("failed to get current info\n");
                R_THROW(Result_UnzGetGlobalInfo64);
            }

            std::vector<char> manifest_data(info.uncompressed_size);
            if ((int)info.uncompressed_size != unzReadCurrentFile(zfile, manifest_data.data(), manifest_data.size())) {
                log_write("failed to read manifest file\n");
                R_THROW(Result_UnzReadCurrentFile);
            }

            new_manifest = ParseManifest(manifest_data);
            if (new_manifest.empty()) {
                log_write("manifest is empty!\n");
                R_THROW(Result_AppstoreFailedParseManifest);
            }
        }

        const auto unzip_to = [&](const fs::FsPath& inzip, const fs::FsPath& output) -> Result {
            pbox->ResetTransferProgress();

            if (UNZ_END_OF_LIST_OF_FILE == unzLocateFile(zfile, inzip, 0)) {
                log_write("failed to find %s\n", inzip.s);
                R_THROW(Result_UnzLocateFile);
            }

            if (UNZ_OK != unzOpenCurrentFile(zfile)) {
                log_write("failed to open current file\n");
                R_THROW(Result_UnzOpenCurrentFile);
            }
            ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

            unz_file_info64 info;
            if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, 0, 0, 0, 0, 0, 0)) {
                log_write("failed to get current info\n");
                R_THROW(Result_UnzGetCurrentFileInfo64);
            }

            auto path = output;
            if (path[0] != '/') {
                path = fs::AppendPath("/", path);
            }

            return thread::TransferUnzip(pbox, zfile, &fs, path, info.uncompressed_size, info.crc, thread::Mode::SingleThreadedIfSmaller, false);
        };

        // unzip manifest, info and all entries.
        TimeStamp ts;
        #if 1
        pbox->NewTransfer("Extracting app metadata"_i18n);
        R_TRY(unzip_to("info.json", BuildInfoCachePath(entry)));
        R_TRY(unzip_to("manifest.install", BuildManifestCachePath(entry)));
        #endif

        pbox->NewTransfer("Extracting app files"_i18n);
        R_TRY(thread::TransferUnzipAll(pbox, zfile, &fs, "/", [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
            const auto it = std::ranges::find_if(new_manifest, [&name](auto& e){
                return !strcasecmp(name, e.path);
            });

            if (it == new_manifest.end()) [[unlikely]] {
                return false;
            }

            switch (it->command) {
                case 'E': // both are the same?
                case 'U':
                    return true;

                case 'G': // checks if file exists, if not, extract
                    return !fs.FileExists(fs::AppendPath("/", it->path));

                default:
                    log_write("bad command: %c\n", it->command);
                    return false;
            }
        }));

        log_write("\n\t[APPSTORE] finished extract new, time taken: %.2fs %zums\n\n", ts.GetSecondsD(), ts.GetMs());

        // finally finally, remove files no longer in the manifest
        for (auto& old_entry : old_manifest) {
            bool found = false;
            for (auto& new_entry : new_manifest) {
                if (!strcasecmp(old_entry.path, new_entry.path)) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                const auto safe_buf = fs::AppendPath("/", old_entry.path);
                if (R_FAILED(fs.DeleteFile(safe_buf))) {
                    log_write("failed to delete: %s\n", safe_buf.s);
                } else {
                    log_write("deleted file: %s\n", safe_buf.s);
                    svcSleepThread(1e+5);
                }
            }
        }
    }

    log_write("finished install :)\n");
    R_SUCCEED();
}

// case-insensitive version of str.find()

} // namespace sphaira::ui::menu::appstore
