#include "account/nand_transfer_internal.hpp"
#include "account/account_restore.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "path_util.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::nand_transfer {

auto ZipWriteBuffer(zipFile zf, const char* filename_in_zip, const void* data, size_t size, int compress_level = Z_DEFAULT_COMPRESSION) -> Result {
    zip_fileinfo zi{};
    if (ZIP_OK != zipOpenNewFileInZip(zf, filename_in_zip, &zi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, compress_level)) {
        return Result_FsInvalidType;
    }
    ON_SCOPE_EXIT(zipCloseFileInZip(zf));
    if (size > 0 && data) {
        if (ZIP_OK != zipWriteInFileInZip(zf, data, size)) {
            return Result_FsInvalidType;
        }
    }
    R_SUCCEED();
}

auto ReadZipFileEntry(unzFile zf, const char* filename, std::vector<u8>& out, u64 max_size) -> bool {
    if (UNZ_OK != unzLocateFile(zf, filename, 0)) {
        return false;
    }
    unz_file_info64 info{};
    if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, nullptr, 0, nullptr, 0, nullptr, 0)) {
        return false;
    }
    if (info.uncompressed_size > max_size) {
        return false;
    }
    if (UNZ_OK != unzOpenCurrentFile(zf)) {
        return false;
    }
    ON_SCOPE_EXIT(unzCloseCurrentFile(zf));
    out.resize(info.uncompressed_size);
    if (info.uncompressed_size > 0) {
        u64 total_read = 0;
        while (total_read < info.uncompressed_size) {
            int read_bytes = unzReadCurrentFile(zf, out.data() + total_read, static_cast<unsigned int>(info.uncompressed_size - total_read));
            if (read_bytes <= 0) {
                out.clear();
                return false;
            }
            total_read += read_bytes;
        }
    }
    return true;
}

auto ValidatePackArchiveFile(const std::string& path) -> bool {
    if (path.empty()) {
        return false;
    }
    fs::FsNativeSd sd;
    if (!sd.FileExists(path.c_str())) {
        return false;
    }
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    unzFile zf = unzOpen2_64(path.c_str(), &file_func);
    if (!zf) {
        return false;
    }
    ON_SCOPE_EXIT(unzClose(zf));

    std::vector<u8> manifest_bytes;
    if (!ReadZipFileEntry(zf, "manifest.json", manifest_bytes, 64 * 1024)) {
        return false;
    }
    const std::string manifest_str(manifest_bytes.begin(), manifest_bytes.end());
    if (manifest_str.find("kefir-nand-transfer") == std::string::npos) {
        return false;
    }

    unz_global_info64 ginfo{};
    if (UNZ_OK != unzGetGlobalInfo64(zf, &ginfo) || ginfo.number_entry == 0) {
        return false;
    }

    if (UNZ_OK != unzGoToFirstFile(zf)) {
        return false;
    }

    bool has_save_tree = false;
    for (u64 i = 0; i < ginfo.number_entry; ++i) {
        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zf)) {
                return false;
            }
        }
        unz_file_info64 info{};
        char name_buf[512]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zf, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            return false;
        }
        if (info.size_filename == 0 || info.size_filename >= sizeof(name_buf)) {
            return false;
        }
        std::string_view name{name_buf, info.size_filename};
        if (!path::IsSafeArchiveEntry(name)) {
            return false;
        }
        if (name.ends_with("manifest.json") && name != "manifest.json") {
            return false;
        }
        if (name.starts_with("8000000000000010") || name.starts_with("80000000000000F0")) {
            has_save_tree = true;
        }
    }

    return has_save_tree;
}

auto IsPackArchive(const std::string& path) -> bool {
    if (path.empty()) {
        return false;
    }
    std::string_view sv{path};
    if (!sv.ends_with(".zip") && !sv.ends_with(".ZIP")) {
        return false;
    }
    return ValidatePackArchiveFile(path);
}

auto CollectAllFilesRecursively(fs::Fs& sd, const std::string& base, const std::string& rel, std::vector<std::string>& files) -> Result {
    const auto full = rel.empty() ? base : (base + "/" + rel);
    fs::Dir d;
    R_TRY(sd.OpenDirectory(full.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));
    std::vector<FsDirectoryEntry> ents;
    R_TRY(d.ReadAll(ents));
    for (const auto& e : ents) {
        const auto child_rel = rel.empty() ? std::string(e.name) : (rel + "/" + e.name);
        if (e.type == FsDirEntryType_Dir) {
            R_TRY(CollectAllFilesRecursively(sd, base, child_rel, files));
        } else if (e.type == FsDirEntryType_File) {
            if (std::string_view{e.name}.ends_with(".part")) {
                continue;
            }
            files.push_back(child_rel);
        }
    }
    R_SUCCEED();
}

auto FinalizePackArchive(const std::string& staging_dir, std::string& out_archive_path, ui::ProgressBox* pbox) -> Result {
    out_archive_path.clear();
    fs::FsNativeSd sd;
    R_UNLESS(path::IsSafeBackupStagingDir(staging_dir), Result_FsInvalidType);
    R_UNLESS(sd.DirExists(staging_dir.c_str()), Result_FsInvalidType);

    const auto base = BaseName(staging_dir);
    R_UNLESS(!base.empty(), Result_FsInvalidType);

    const auto parent = ParentDir(staging_dir);
    std::string archive_stem = base;
    if (archive_stem.starts_with("_staging_")) {
        archive_stem = archive_stem.substr(std::string_view("_staging_").size());
    }
    std::string base_archive = parent + "/" + archive_stem + ".kefir-nand.zip";
    std::string final_path = base_archive;
    int suffix = 1;
    while (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
        final_path = parent + "/" + archive_stem + "_" + std::to_string(suffix++) + ".kefir-nand.zip";
    }

    const std::string part_path = final_path + ".part";
    sd.DeleteFile(part_path.c_str());

    std::vector<std::string> rel_files;
    R_TRY(CollectAllFilesRecursively(sd, staging_dir, "", rel_files));
    R_UNLESS(!rel_files.empty(), Result_FsEmpty);

    bool has_manifest = false;
    for (const auto& f : rel_files) {
        if (f == "manifest.json") {
            has_manifest = true;
            break;
        }
    }
    R_UNLESS(has_manifest, Result_FsInvalidType);

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    zipFile zf = zipOpen2_64(part_path.c_str(), APPEND_STATUS_CREATE, nullptr, &file_func);
    R_UNLESS(zf, Result_FsInvalidType);

    bool zip_closed = false;
    bool archive_success = false;
    ON_SCOPE_EXIT({
        if (zf && !zip_closed) {
            zipClose(zf, nullptr);
        }
        if (!archive_success) {
            sd.DeleteFile(part_path.c_str());
        }
    });

    for (const auto& rel : rel_files) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(rel.c_str());
        }
        std::vector<u8> data;
        const auto src_path = staging_dir + "/" + rel;
        R_TRY(sd.read_entire_file(src_path.c_str(), data));
        R_TRY(ZipWriteBuffer(zf, rel.c_str(), data.data(), data.size()));
    }

    if (zipClose(zf, "KefirHub NAND transfer backup") != ZIP_OK) {
        zip_closed = true;
        log_write("[NAND] zipClose failed for %s\n", part_path.c_str());
        return Result_FsInvalidType;
    }
    zip_closed = true;

    if (!ValidatePackArchiveFile(part_path)) {
        log_write("[NAND] validation failed for %s\n", part_path.c_str());
        return Result_FsInvalidType;
    }

    R_TRY(sd.RenameFile(part_path.c_str(), final_path.c_str()));
    archive_success = true;

    sd.DeleteDirectoryRecursively(staging_dir.c_str());
    out_archive_path = final_path;
    log_write("[NAND] finalized archive %s (from %s)\n", final_path.c_str(), staging_dir.c_str());
    R_SUCCEED();
}

auto StagePackArchiveForRestore(const std::string& archive_path, std::string& out_staging_dir, ui::ProgressBox* pbox) -> Result {
    out_staging_dir.clear();
    fs::FsNativeSd sd;
    R_UNLESS(sd.FileExists(archive_path.c_str()) && IsPackArchive(archive_path), Result_FsInvalidType);

    const auto stem = StemName(BaseName(archive_path));
    R_UNLESS(!stem.empty(), Result_FsInvalidType);

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    std::string staging_dir = paths::DATA_ROOT + "/nand_transfer/_restore_" + stem + "_" + stamp;
    if (sd.DirExists(staging_dir.c_str())) {
        int suffix = 1;
        while (sd.DirExists((staging_dir + "_" + std::to_string(suffix)).c_str())) {
            suffix++;
        }
        staging_dir += "_" + std::to_string(suffix);
    }
    account_restore::CleanRestoreStagingDir(staging_dir);
    R_TRY(sd.CreateDirectoryRecursively(staging_dir.c_str()));

    bool stage_success = false;
    ON_SCOPE_EXIT({
        if (!stage_success) {
            account_restore::CleanRestoreStagingDir(staging_dir);
        }
    });

    R_TRY(thread::TransferUnzipAll(pbox, archive_path.c_str(), &sd, staging_dir.c_str()));

    if (!PackLooksRight(sd, staging_dir) || !sd.FileExists((staging_dir + "/manifest.json").c_str())) {
        log_write("[NAND] extracted archive failed validation: %s\n", staging_dir.c_str());
        return Result_FsInvalidType;
    }

    stage_success = true;
    out_staging_dir = staging_dir;
    log_write("[NAND] staged archive %s to %s\n", archive_path.c_str(), staging_dir.c_str());
    R_SUCCEED();
}

} // namespace sphaira::nand_transfer
