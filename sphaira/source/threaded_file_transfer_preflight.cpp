#include "threaded_file_transfer_preflight.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "minizip_helper.hpp"
#include "path_util.hpp"

#include <vector>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <minizip/unzip.h>

namespace sphaira::thread {
namespace {

// the HOS filesystem rejects certain characters in a path component with
// FsError_InvalidCharacter. some zip packs (e.g. cheat packs organised by
// human-readable game title) carry entries whose names contain them, which
// would otherwise abort the entire extraction on the first bad entry. replace
// the offending characters with '_' per component, leaving '/' separators
// intact. entries whose names are pure hex (atmosphere cheat paths) are
// unaffected.
bool IsInvalidPathChar(char c) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20) {
        return true; // control characters
    }
    switch (c) {
        case ':': case '*': case '?': case '"':
        case '<': case '>': case '|': case '\\':
            return true;
        default:
            return false;
    }
}

fs::FsPath SanitizeZipEntryName(const fs::FsPath& name) {
    fs::FsPath out = name;
    bool changed = false;
    for (u32 i = 0; out.s[i] != '\0'; i++) {
        if (IsInvalidPathChar(out.s[i])) {
            out.s[i] = '_';
            changed = true;
        }
    }
    if (changed) {
        log_write("[Unzip] sanitized invalid entry name '%s' -> '%s'\n", name.s, out.s);
    }
    return out;
}

} // namespace

Result ResolveArchiveEntryName(const unz_file_info64& info, const char* name_buf, bool save_dbi_compat, fs::FsPath& out_name, bool* out_is_dbi_root_marker) {
    if (out_is_dbi_root_marker) {
        *out_is_dbi_root_marker = false;
    }

    if (info.size_filename == 0) {
        log_write("archive entry has empty name\n");
        R_THROW(FsError_InvalidCharacter);
    }

    if (info.size_filename >= sizeof(out_name.s)) {
        log_write("archive entry name too long (%lu bytes)\n", static_cast<unsigned long>(info.size_filename));
        R_THROW(FsError_TooLongPath);
    }

    if (std::strlen(name_buf) != info.size_filename) {
        log_write("archive entry name length mismatch (%zu != %lu)\n", std::strlen(name_buf), static_cast<unsigned long>(info.size_filename));
        R_THROW(FsError_TooLongPath);
    }

    const std::string_view raw{name_buf, info.size_filename};
    if (save_dbi_compat) {
        if (raw == "//") {
            if (!path::IsDbiRootMarkerEntry(raw, info.uncompressed_size, info.external_fa)) {
                log_write("invalid DBI root marker: %s (size %llu, fa 0x%08x)\n", name_buf, static_cast<unsigned long long>(info.uncompressed_size), static_cast<unsigned int>(info.external_fa));
                R_THROW(FsError_InvalidCharacter);
            }
            if (out_is_dbi_root_marker) {
                *out_is_dbi_root_marker = true;
            }
            out_name = "";
            R_SUCCEED();
        }

        const auto norm = path::NormalizeSaveArchiveEntry(raw);
        if (!norm.has_value()) {
            log_write("unsafe save archive entry: %s\n", name_buf);
            R_THROW(FsError_InvalidCharacter);
        }
        if (norm->size() >= sizeof(out_name.s)) {
            log_write("normalized save archive entry too long (%zu bytes)\n", norm->size());
            R_THROW(FsError_TooLongPath);
        }
        std::memcpy(out_name.s, norm->data(), norm->size());
        out_name.s[norm->size()] = '\0';
    } else {
        if (!path::IsSafeArchiveEntry(raw)) {
            log_write("unsafe archive entry path: %s\n", name_buf);
            R_THROW(FsError_InvalidCharacter);
        }
        std::memcpy(out_name.s, name_buf, info.size_filename);
        out_name.s[info.size_filename] = '\0';
    }

    R_SUCCEED();
}

Result ResolveArchiveDestinationEntry(
    const unz_file_info64& info,
    const char* name_buf,
    const fs::FsPath& base_path,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    ResolvedDestinationEntry& out) {

    out = ResolvedDestinationEntry{};
    bool is_marker = false;
    fs::FsPath name;
    R_TRY(ResolveArchiveEntryName(info, name_buf, save_dbi_compat, name, &is_marker));
    if (is_marker) {
        out.is_dbi_root_marker = true;
        out.is_directory = true;
        out.keep = false;
        out.path = "";
        R_SUCCEED();
    }

    name = SanitizeZipEntryName(name);

    out.path = fs::AppendPath(base_path, name);
    out.keep = filter ? filter(name, out.path) : true;
    if (out.keep) {
        const auto path_len = out.path.length();
        if (path_len == 0) {
            log_write("empty destination path\n");
            R_THROW(FsError_InvalidCharacter);
        }

        if (!path::IsSafeExtractionDestination(out.path, base_path, save_dbi_compat)) {
            log_write("unsafe destination path: %s\n", out.path.s);
            R_THROW(FsError_InvalidCharacter);
        }

        out.is_directory = (out.path[path_len - 1] == '/');
    } else {
        out.is_directory = false;
    }

    R_SUCCEED();
}

std::vector<std::string> GetParentDirectories(const std::string& path) {
    std::vector<std::string> parents;
    size_t last_slash = path.find_last_of('/');
    while (last_slash != std::string::npos && last_slash > 0) {
        std::string parent = path.substr(0, last_slash);
        parents.push_back(parent);
        last_slash = parent.find_last_of('/');
    }
    return parents;
}

Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter, bool save_dbi_compat, UnzipPayloadSummary* output, UnzipPayloadInventory* inventory_out, bool allow_empty) {
    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }

    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        R_THROW(FsError_InvalidSize);
    }

    if (ginfo.number_entry == 0) {
        if (!allow_empty) {
            R_THROW(FsError_InvalidSize);
        }
        if (output) {
            *output = UnzipPayloadSummary{};
        }
        if (inventory_out) {
            *inventory_out = UnzipPayloadInventory{};
        }
        R_SUCCEED();
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    UnzipPayloadSummary local_summary{};
    UnzipPayloadInventory local_inventory{};
    std::set<std::string> explicit_dirs;
    std::vector<u8> drain_buf(64 * 1024);
    bool seen_dbi_root_marker = false;

    for (s64 i = 0; i < entry_count; i++) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile during preflight\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        unz_file_info64 info;
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            log_write("failed to get current info during preflight\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        if (info.uncompressed_size > static_cast<u64>(std::numeric_limits<s64>::max())) {
            log_write("archive uncompressed size exceeds s64 maximum\n");
            R_THROW(FsError_InvalidSize);
        }

        ResolvedDestinationEntry resolved{};
        R_TRY(ResolveArchiveDestinationEntry(info, name_buf, base_path, filter, save_dbi_compat, resolved));

        if (resolved.is_dbi_root_marker) {
            if (seen_dbi_root_marker) {
                log_write("duplicate DBI root marker during preflight\n");
                R_THROW(FsError_PathAlreadyExists);
            }
            seen_dbi_root_marker = true;
        }

        if (resolved.keep) {
            if (resolved.is_directory) {
                if (local_summary.directory_count == std::numeric_limits<s64>::max()) {
                    log_write("archive directory count exceeds s64 maximum\n");
                    R_THROW(FsError_InvalidSize);
                }
                local_summary.directory_count++;
            } else {
                if (local_summary.file_count == std::numeric_limits<s64>::max()) {
                    log_write("archive file count exceeds s64 maximum\n");
                    R_THROW(FsError_InvalidSize);
                }
                if (static_cast<u64>(std::numeric_limits<s64>::max()) - static_cast<u64>(local_summary.file_bytes) < info.uncompressed_size) {
                    log_write("archive file_bytes aggregate overflow exceeds s64 maximum\n");
                    R_THROW(FsError_InvalidSize);
                }
                local_summary.file_count++;
                local_summary.file_bytes += static_cast<s64>(info.uncompressed_size);
            }

            if (inventory_out) {
                if (resolved.is_directory) {
                    std::string canonical_dir = resolved.path.s;
                    while (canonical_dir.size() > 1 && canonical_dir.back() == '/') {
                        canonical_dir.pop_back();
                    }
                    if (canonical_dir.empty() || canonical_dir == "/") {
                        R_THROW(FsError_InvalidCharacter);
                    }
                    if (!explicit_dirs.insert(canonical_dir).second) {
                        log_write("duplicate explicit directory: %s\n", canonical_dir.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    if (local_inventory.files.contains(canonical_dir)) {
                        log_write("dir conflicts with existing file: %s\n", canonical_dir.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    for (const auto& parent : GetParentDirectories(canonical_dir)) {
                        if (local_inventory.files.contains(parent)) {
                            log_write("parent of dir conflicts with existing file: %s\n", parent.c_str());
                            R_THROW(FsError_PathAlreadyExists);
                        }
                        local_inventory.directories.insert(parent);
                    }
                    local_inventory.directories.insert(canonical_dir);
                } else {
                    std::string file_key = resolved.path.s;
                    if (local_inventory.files.contains(file_key)) {
                        log_write("duplicate kept file: %s\n", file_key.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    if (local_inventory.directories.contains(file_key)) {
                        log_write("file conflicts with directory or parent: %s\n", file_key.c_str());
                        R_THROW(FsError_PathAlreadyExists);
                    }
                    for (const auto& parent : GetParentDirectories(file_key)) {
                        if (local_inventory.files.contains(parent)) {
                            log_write("parent of file conflicts with existing file: %s\n", parent.c_str());
                            R_THROW(FsError_PathAlreadyExists);
                        }
                        local_inventory.directories.insert(parent);
                    }
                    local_inventory.files.emplace(std::move(file_key), static_cast<s64>(info.uncompressed_size));
                }
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file during preflight: %s\n", name_buf);
            R_THROW(Result_UnzOpenCurrentFile);
        }

        u32 crc32_out = 0;
        u64 bytes_drained = 0;
        int read_res = 0;
        do {
            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    unzCloseCurrentFile(zfile);
                    R_THROW(exit_rc);
                }
            }

            read_res = unzReadCurrentFile(zfile, drain_buf.data(), drain_buf.size());
            if (read_res < 0) {
                log_write("failed to read zip file during preflight: %s %d\n", name_buf, read_res);
                unzCloseCurrentFile(zfile);
                R_THROW(Result_UnzReadCurrentFile);
            }
            if (read_res > 0) {
                const auto read_u64 = static_cast<u64>(read_res);
                if (std::numeric_limits<u64>::max() - bytes_drained < read_u64 || bytes_drained + read_u64 > info.uncompressed_size) {
                    log_write("archive entry exceeded declared uncompressed size during read: %s\n", name_buf);
                    unzCloseCurrentFile(zfile);
                    R_THROW(FsError_InvalidSize);
                }
                if (info.crc) {
                    crc32_out = crc32CalculateWithSeed(crc32_out, drain_buf.data(), read_res);
                }
                bytes_drained += read_u64;
            }
        } while (read_res > 0);

        const int close_res = unzCloseCurrentFile(zfile);
        if (close_res == UNZ_CRCERROR) {
            log_write("crc error on unzCloseCurrentFile during preflight: %s\n", name_buf);
            R_THROW(0x8);
        } else if (close_res != UNZ_OK) {
            log_write("failed to close zip file during preflight: %s %d\n", name_buf, close_res);
            R_THROW(Result_UnzReadCurrentFile);
        }

        if (bytes_drained != info.uncompressed_size) {
            log_write("archive entry drained size mismatch (%llu != %llu)\n", static_cast<unsigned long long>(bytes_drained), static_cast<unsigned long long>(info.uncompressed_size));
            R_THROW(FsError_InvalidSize);
        }

        if (info.crc && crc32_out != info.crc) {
            log_write("archive entry crc mismatch (%08x != %08x)\n", crc32_out, static_cast<unsigned int>(info.crc));
            R_THROW(0x8);
        }
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        log_write("failed to unzGoToFirstFile after preflight\n");
        R_THROW(Result_UnzGoToFirstFile);
    }

    if (save_dbi_compat && local_summary.file_count == 0 && local_summary.directory_count == 0) {
        if (!allow_empty) {
            log_write("archive contains no kept payload items\n");
            R_THROW(FsError_InvalidSize);
        }
    }

    if (output) {
        *output = local_summary;
    }
    if (inventory_out) {
        *inventory_out = std::move(local_inventory);
    }

    R_SUCCEED();
}

Result TransferUnzipPreflight(ui::ProgressBox* pbox, void* zfile, const fs::FsPath& base_path, UnzipAllFilter filter, bool save_dbi_compat, UnzipPayloadSummary* output, UnzipPayloadInventory* inventory_out) {
    return TransferUnzipPreflight(pbox, zfile, base_path, filter, save_dbi_compat, output, inventory_out, false);
}

Result TransferUnzipPreflight(ui::ProgressBox* pbox, const fs::FsPath& zip_out, const fs::FsPath& base_path, UnzipAllFilter filter, bool save_dbi_compat, UnzipPayloadSummary* output, UnzipPayloadInventory* inventory_out) {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return TransferUnzipPreflight(pbox, zfile, base_path, filter, save_dbi_compat, output, inventory_out);
}

} // namespace sphaira::thread
