#include "threaded_file_transfer.hpp"
#include "threaded_file_transfer_preflight.hpp"
#include "ui/menus/filebrowser.hpp"
#include "minizip_helper.hpp"
#include "log.hpp"
#include "defines.hpp"

#include <vector>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <cstring>
#include <limits>
#include <minizip/unzip.h>

namespace sphaira::thread {

Result VerifyArchiveAgainstNative(
    ui::ProgressBox* pbox,
    void* zfile,
    fs::Fs* fs,
    const fs::FsPath& base_path,
    const UnzipPayloadInventory& expected_inventory,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    bool allow_empty) {

    if (!fs || !fs->IsNative()) {
        R_THROW(FsError_NotImplemented);
    }
    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }

    // 1. Enumerate native destination with actual sizes
    ui::menu::filebrowser::FsDirCollections collections;
    R_TRY(ui::menu::filebrowser::FsView::get_collections(fs, base_path, "", collections, true));

    std::map<std::string, s64> native_files;
    std::set<std::string> native_dirs;

    for (const auto& col : collections) {
        if (col.path != base_path) {
            std::string dir_key = col.path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                native_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    native_dirs.insert(parent);
                }
            }
        }
        for (const auto& d : col.dirs) {
            auto dir_path = fs::AppendPath(col.path, d.name);
            std::string dir_key = dir_path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                native_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    native_dirs.insert(parent);
                }
            }
        }
        for (const auto& f : col.files) {
            R_UNLESS(f.file_size >= 0, FsError_InvalidSize);
            auto file_path = fs::AppendPath(col.path, f.name);
            std::string file_key = file_path.s;
            auto [fit, finserted] = native_files.try_emplace(file_key, f.file_size);
            R_UNLESS(finserted, FsError_PathAlreadyExists);
            for (const auto& parent : GetParentDirectories(file_key)) {
                native_dirs.insert(parent);
            }
        }
    }

    // 2. Exact inventory bijection check
    R_UNLESS(native_files.size() == expected_inventory.files.size(), FsError_PathNotFound);
    for (const auto& [path, size] : expected_inventory.files) {
        auto it = native_files.find(path);
        R_UNLESS(it != native_files.end(), FsError_PathNotFound);
        R_UNLESS(it->second == size, FsError_InvalidSize);
    }
    R_UNLESS(native_dirs.size() == expected_inventory.directories.size(), FsError_PathNotFound);
    for (const auto& dir : expected_inventory.directories) {
        R_UNLESS(native_dirs.contains(dir), FsError_PathNotFound);
    }

    // 3. Traversal and streaming byte-for-byte comparison
    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }
    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        R_THROW(FsError_InvalidSize);
    }
    if (ginfo.number_entry == 0) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        R_UNLESS(expected_inventory.files.empty() && expected_inventory.directories.empty(), FsError_InvalidSize);
        R_UNLESS(native_files.empty() && native_dirs.empty(), FsError_PathNotFound);
        if (!allow_empty) {
            R_THROW(FsError_InvalidSize);
        }
        R_SUCCEED();
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    UnzipPayloadInventory observed_inventory{};
    std::set<std::string> observed_explicit_dirs;
    std::set<std::string> verified_files;

    for (s64 i = 0; i < entry_count; i++) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile in VerifyArchiveAgainstNative\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("failed to get current info in VerifyArchiveAgainstNative\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        if (info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())) {
            log_write("archive uncompressed size exceeds s64 maximum in verifier\n");
            R_THROW(FsError_InvalidSize);
        }

        ResolvedDestinationEntry resolved{};
        R_TRY(ResolveArchiveDestinationEntry(info, name_buf, base_path, filter, save_dbi_compat, resolved));

        if (resolved.keep) {
            if (resolved.is_directory) {
                std::string canonical_dir = resolved.path.s;
                while (canonical_dir.size() > 1 && canonical_dir.back() == '/') {
                    canonical_dir.pop_back();
                }
                if (canonical_dir.empty() || canonical_dir == "/") {
                    R_THROW(FsError_InvalidCharacter);
                }
                if (!observed_explicit_dirs.insert(canonical_dir).second) {
                    log_write("verifier observed duplicate explicit directory: %s\n", canonical_dir.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                if (observed_inventory.files.contains(canonical_dir)) {
                    log_write("verifier dir conflicts with file: %s\n", canonical_dir.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                for (const auto& parent : GetParentDirectories(canonical_dir)) {
                    if (observed_inventory.files.contains(parent)) {
                        log_write("verifier parent of dir conflicts with file: %s\n", parent.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    observed_inventory.directories.insert(parent);
                }
                observed_inventory.directories.insert(canonical_dir);
            } else {
                std::string file_key = resolved.path.s;
                if (observed_inventory.files.contains(file_key)) {
                    log_write("verifier observed duplicate kept file: %s\n", file_key.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                if (observed_inventory.directories.contains(file_key)) {
                    log_write("verifier file conflicts with directory: %s\n", file_key.c_str());
                    R_THROW(FsError_PathAlreadyExists);
                }
                for (const auto& parent : GetParentDirectories(file_key)) {
                    if (observed_inventory.files.contains(parent)) {
                        log_write("verifier parent of file conflicts with file: %s\n", parent.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    observed_inventory.directories.insert(parent);
                }
                observed_inventory.files.emplace(file_key, static_cast<s64>(info.uncompressed_size));
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file in VerifyArchiveAgainstNative: %s\n", name_buf);
            R_THROW(Result_UnzOpenCurrentFile);
        }
        bool curr_open = true;
        ON_SCOPE_EXIT({
            if (curr_open && zfile) {
                unzCloseCurrentFile(zfile);
            }
        });

        if (!resolved.keep) {
            // Excluded source metadata: drain completely and check CRC
            std::vector<u8> drain_buf(32768);
            u32 crc_calc = 0;
            u64 bytes_drained = 0;
            int zr = 0;
            do {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                zr = unzReadCurrentFile(zfile, drain_buf.data(), drain_buf.size());
                if (zr < 0) {
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(Result_UnzReadCurrentFile);
                }
                if (zr > 0) {
                    const auto r_u64 = static_cast<u64>(zr);
                    if (std::numeric_limits<u64>::max() - bytes_drained < r_u64 || bytes_drained + r_u64 > info.uncompressed_size) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        R_THROW(FsError_InvalidSize);
                    }
                    if (info.crc) {
                        crc_calc = crc32CalculateWithSeed(crc_calc, drain_buf.data(), zr);
                    }
                    bytes_drained += r_u64;
                }
            } while (zr > 0);

            curr_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            R_UNLESS(bytes_drained == info.uncompressed_size, FsError_InvalidSize);
            if (info.crc && crc_calc != info.crc) {
                R_THROW(0x8);
            }
        } else if (resolved.is_directory) {
            std::string canonical_dir = resolved.path.s;
            while (canonical_dir.size() > 1 && canonical_dir.back() == '/') {
                canonical_dir.pop_back();
            }
            R_UNLESS(expected_inventory.directories.contains(canonical_dir), FsError_PathNotFound);
            R_UNLESS(native_dirs.contains(canonical_dir), FsError_PathNotFound);

            // Drain kept directory entries through EOF with checked byte count/CRC/close
            std::vector<u8> drain_buf(32768);
            u32 crc_calc = 0;
            u64 bytes_drained = 0;
            int zr = 0;
            do {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                zr = unzReadCurrentFile(zfile, drain_buf.data(), drain_buf.size());
                if (zr < 0) {
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    R_THROW(Result_UnzReadCurrentFile);
                }
                if (zr > 0) {
                    const auto r_u64 = static_cast<u64>(zr);
                    if (std::numeric_limits<u64>::max() - bytes_drained < r_u64 || bytes_drained + r_u64 > info.uncompressed_size) {
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        R_THROW(FsError_InvalidSize);
                    }
                    if (info.crc) {
                        crc_calc = crc32CalculateWithSeed(crc_calc, drain_buf.data(), zr);
                    }
                    bytes_drained += r_u64;
                }
            } while (zr > 0);

            curr_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            R_UNLESS(bytes_drained == info.uncompressed_size, FsError_InvalidSize);
            if (info.crc && crc_calc != info.crc) {
                R_THROW(0x8);
            }
        } else {
            auto eit = expected_inventory.files.find(resolved.path.s);
            R_UNLESS(eit != expected_inventory.files.end(), FsError_PathNotFound);
            R_UNLESS(static_cast<u64>(eit->second) == info.uncompressed_size, FsError_InvalidSize);
            R_UNLESS(!verified_files.contains(resolved.path.s), FsError_PathAlreadyExists);

            fs::File nf;
            const auto open_rc = fs->OpenFile(resolved.path, FsOpenMode_Read, &nf);
            if (R_FAILED(open_rc)) {
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return open_rc;
            }

            s64 nf_size = 0;
            const auto size_rc = nf.GetSize(&nf_size);
            if (R_FAILED(size_rc)) {
                nf.Close();
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return size_rc;
            }
            if (nf_size != eit->second) {
                nf.Close();
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return FsError_InvalidSize;
            }

            constexpr size_t CMP_BUF_SIZE = 32768;
            std::vector<u8> zbuf(CMP_BUF_SIZE);
            std::vector<u8> fbuf(CMP_BUF_SIZE);
            s64 offset = 0;
            u32 file_crc = 0;

            while (offset < nf_size) {
                if (pbox) {
                    const auto exit_rc = pbox->ShouldExitResult();
                    if (R_FAILED(exit_rc)) {
                        nf.Close();
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return exit_rc;
                    }
                }
                const auto chunk = static_cast<s64>(std::min<size_t>(CMP_BUF_SIZE, nf_size - offset));
                s64 z_accum = 0;
                while (z_accum < chunk) {
                    int zr = unzReadCurrentFile(zfile, zbuf.data() + z_accum, chunk - z_accum);
                    if (zr <= 0) {
                        nf.Close();
                        curr_open = false;
                        unzCloseCurrentFile(zfile);
                        return FsError_InvalidSize;
                    }
                    z_accum += zr;
                }
                if (info.crc) {
                    file_crc = crc32CalculateWithSeed(file_crc, zbuf.data(), chunk);
                }

                u64 fread = 0;
                const auto read_rc = nf.Read(offset, fbuf.data(), chunk, FsReadOption_None, &fread);
                if (R_FAILED(read_rc)) {
                    nf.Close();
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    return read_rc;
                }
                if (static_cast<s64>(fread) != chunk) {
                    nf.Close();
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    return FsError_InvalidSize;
                }
                if (std::memcmp(zbuf.data(), fbuf.data(), chunk) != 0) {
                    nf.Close();
                    curr_open = false;
                    unzCloseCurrentFile(zfile);
                    return FsError_InvalidSize;
                }
                offset += chunk;
            }

            u8 dummy;
            int extra_zr = unzReadCurrentFile(zfile, &dummy, 1);
            if (extra_zr != 0) {
                nf.Close();
                curr_open = false;
                unzCloseCurrentFile(zfile);
                return FsError_InvalidSize;
            }

            nf.Close();

            curr_open = false;
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res == UNZ_CRCERROR) {
                R_THROW(0x8);
            }
            R_UNLESS(close_res == UNZ_OK, Result_UnzOpenCurrentFile);
            if (info.crc && file_crc != info.crc) {
                R_THROW(0x8);
            }

            verified_files.insert(resolved.path.s);
        }
    }

    R_UNLESS(observed_inventory.files == expected_inventory.files, FsError_PathNotFound);
    R_UNLESS(observed_inventory.directories == expected_inventory.directories, FsError_PathNotFound);
    R_UNLESS(verified_files.size() == expected_inventory.files.size(), FsError_PathNotFound);
    R_UNLESS(UNZ_END_OF_LIST_OF_FILE == unzGoToNextFile(zfile), Result_UnzGoToNextFile);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        log_write("failed to rewind zip after verification\n");
        R_THROW(Result_UnzGoToFirstFile);
    }

    // 4. Re-enumerate native inventory after byte comparison to catch observable changes
    ui::menu::filebrowser::FsDirCollections post_collections;
    R_TRY(ui::menu::filebrowser::FsView::get_collections(fs, base_path, "", post_collections, true));

    std::map<std::string, s64> post_files;
    std::set<std::string> post_dirs;

    for (const auto& col : post_collections) {
        if (col.path != base_path) {
            std::string dir_key = col.path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                post_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    post_dirs.insert(parent);
                }
            }
        }
        for (const auto& d : col.dirs) {
            auto dir_path = fs::AppendPath(col.path, d.name);
            std::string dir_key = dir_path.s;
            while (dir_key.size() > 1 && dir_key.back() == '/') {
                dir_key.pop_back();
            }
            if (!dir_key.empty() && dir_key != "/") {
                post_dirs.insert(dir_key);
                for (const auto& parent : GetParentDirectories(dir_key)) {
                    post_dirs.insert(parent);
                }
            }
        }
        for (const auto& f : col.files) {
            R_UNLESS(f.file_size >= 0, FsError_TargetLocked);
            auto file_path = fs::AppendPath(col.path, f.name);
            std::string file_key = file_path.s;
            auto [fit, finserted] = post_files.try_emplace(file_key, f.file_size);
            R_UNLESS(finserted, FsError_TargetLocked);
            for (const auto& parent : GetParentDirectories(file_key)) {
                post_dirs.insert(parent);
            }
        }
    }

    R_UNLESS(post_files.size() == native_files.size(), FsError_TargetLocked);
    for (const auto& [path, size] : native_files) {
        auto it = post_files.find(path);
        R_UNLESS(it != post_files.end(), FsError_TargetLocked);
        R_UNLESS(it->second == size, FsError_TargetLocked);
    }
    R_UNLESS(post_dirs.size() == native_dirs.size(), FsError_TargetLocked);
    for (const auto& dir : native_dirs) {
        R_UNLESS(post_dirs.contains(dir), FsError_TargetLocked);
    }

    R_SUCCEED();
}

Result VerifyArchiveAgainstNative(
    ui::ProgressBox* pbox,
    const fs::FsPath& zip_out,
    fs::Fs* fs,
    const fs::FsPath& base_path,
    const UnzipPayloadInventory& expected_inventory,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    bool allow_empty) {

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return VerifyArchiveAgainstNative(pbox, zfile, fs, base_path, expected_inventory, filter, save_dbi_compat, allow_empty);
}

} // namespace sphaira::thread
