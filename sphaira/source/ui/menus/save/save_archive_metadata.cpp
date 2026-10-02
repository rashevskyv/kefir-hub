#include "ui/menus/save/save_paths.hpp"
#include "save_internal.hpp"
#include "path_util.hpp"
#include "defines.hpp"
#include "ui/progress_box.hpp"
#include <minizip/unzip.h>
#include <cstring>
#include <limits>

namespace sphaira::ui::menu::save {

auto ReadArchiveSaveMetadata(void* zfile, ui::ProgressBox* pbox, DecodedSaveMetadata& out, Result* out_rc) -> ArchiveMetaStatus {
    out = DecodedSaveMetadata{};
    if (out_rc) {
        *out_rc = 0;
    }

    if (!zfile) {
        if (out_rc) *out_rc = Result_UnzOpen2_64;
        return ArchiveMetaStatus::Invalid;
    }

    unz_global_info64 ginfo{};
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        if (out_rc) *out_rc = Result_UnzGetGlobalInfo64;
        return ArchiveMetaStatus::Invalid;
    }

    if (ginfo.size_comment >= 9) {
        char comment[64]{};
        const int comment_len = unzGetGlobalComment(zfile, comment, sizeof(comment));
        if (comment_len >= 9 && std::strncmp(comment, "sphaira v", 9) == 0) {
            out.has_kefir_comment = true;
        }
    }

    if (ginfo.number_entry == 0) {
        return ArchiveMetaStatus::NoMetadata;
    }
    if (ginfo.number_entry > static_cast<u64>(std::numeric_limits<s64>::max())) {
        if (out_rc) *out_rc = FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }
    const auto entry_count = static_cast<s64>(ginfo.number_entry);

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        if (out_rc) *out_rc = Result_UnzGoToFirstFile;
        return ArchiveMetaStatus::Invalid;
    }

    bool success = false;
    ON_SCOPE_EXIT({
        if (!success && zfile) {
            unzGoToFirstFile(zfile);
        }
    });

    bool seen_nx_meta = false;
    bool seen_dbi_extra = false;
    bool seen_dbi_info = false;
    bool seen_dbi_root_marker = false;
    DecodedSaveMetaInternal nx_meta{};
    DecodedSaveMetaInternal dbi_extra_meta{};
    bool has_valid_nx = false;
    bool has_valid_dbi_extra = false;

    for (s64 i = 0; i < entry_count; i++) {
        if (pbox) {
            const auto exit_rc = pbox->ShouldExitResult();
            if (R_FAILED(exit_rc)) {
                if (out_rc) *out_rc = exit_rc;
                return ArchiveMetaStatus::Invalid;
            }
        }

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                if (out_rc) *out_rc = Result_UnzGoToNextFile;
                return ArchiveMetaStatus::Invalid;
            }
        }

        unz_file_info64 info{};
        char name_buf[sizeof(fs::FsPath)]{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            if (out_rc) *out_rc = Result_UnzGetCurrentFileInfo64;
            return ArchiveMetaStatus::Invalid;
        }

        if (info.size_filename == 0) {
            if (out_rc) *out_rc = FsError_InvalidCharacter;
            return ArchiveMetaStatus::Invalid;
        }
        if (info.size_filename >= sizeof(name_buf)) {
            if (out_rc) *out_rc = FsError_TooLongPath;
            return ArchiveMetaStatus::Invalid;
        }
        if (std::strlen(name_buf) != info.size_filename) {
            if (out_rc) *out_rc = FsError_TooLongPath;
            return ArchiveMetaStatus::Invalid;
        }

        const std::string_view raw{name_buf, info.size_filename};
        if (raw == "//") {
            if (!path::IsDbiRootMarkerEntry(raw, info.uncompressed_size, info.external_fa)) {
                if (out_rc) *out_rc = FsError_InvalidCharacter;
                return ArchiveMetaStatus::Invalid;
            }
            if (seen_dbi_root_marker) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_dbi_root_marker = true;

            if (UNZ_OK != unzOpenCurrentFile(zfile)) {
                if (out_rc) *out_rc = Result_UnzOpenCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            char drain_chunk[64];
            int drain_res = unzReadCurrentFile(zfile, drain_chunk, sizeof(drain_chunk));
            if (drain_res < 0) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = Result_UnzReadCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            const int close_res = unzCloseCurrentFile(zfile);
            if (close_res != UNZ_OK) {
                if (out_rc) *out_rc = (close_res == UNZ_CRCERROR) ? Result{0x8} : static_cast<Result>(Result_UnzOpenCurrentFile);
                return ArchiveMetaStatus::Invalid;
            }
            continue;
        }

        const auto norm = path::NormalizeSaveArchiveEntry(raw);
        if (!norm.has_value()) {
            if (out_rc) *out_rc = FsError_InvalidCharacter;
            return ArchiveMetaStatus::Invalid;
        }

        std::string_view norm_view = *norm;
        const bool has_dir_attr = (info.external_fa & 0x10) != 0 ||
                                  ((info.external_fa >> 16) & 0xF000) == 0x4000;
        const bool is_dir = (!norm_view.empty() && norm_view.back() == '/') || has_dir_attr;
        std::string_view clean_view = norm_view;
        if (!clean_view.empty() && clean_view.back() == '/') {
            while (clean_view.size() > 1 && clean_view.back() == '/') {
                clean_view.remove_suffix(1);
            }
        }

        // Check if reserved metadata root is used as a parent directory for payload
        const auto slash_pos = clean_view.find('/');
        if (slash_pos != std::string_view::npos) {
            const auto first_segment = clean_view.substr(0, slash_pos);
            if (ClassifySaveReservedMetadataRoot(first_segment) != SaveReservedMetaKind::None) {
                if (out_rc) *out_rc = FsError_InvalidCharacter;
                return ArchiveMetaStatus::Invalid;
            }
            out.payload_count++;
            continue;
        }

        const auto reserved_kind = ClassifySaveReservedMetadataRoot(clean_view);
        if (reserved_kind == SaveReservedMetaKind::None) {
            out.payload_count++;
            continue;
        }

        // Reject directory kind aliases (trailing slash or directory attributes)
        if (is_dir) {
            if (out_rc) *out_rc = FsError_InvalidCharacter;
            return ArchiveMetaStatus::Invalid;
        }

        // Duplicate rejection
        if (reserved_kind == SaveReservedMetaKind::NxMeta) {
            if (seen_nx_meta) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_nx_meta = true;
            // Known NX declared unsupported sizes fail upfront
            if (info.uncompressed_size != 85 && info.uncompressed_size != 86 && info.uncompressed_size != 128) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
        } else if (reserved_kind == SaveReservedMetaKind::DbiExtra) {
            if (seen_dbi_extra) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_dbi_extra = true;
            // Known DBI extra declared unsupported size fails upfront
            if (info.uncompressed_size != 512) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
        } else if (reserved_kind == SaveReservedMetaKind::DbiInfo) {
            if (seen_dbi_info) {
                if (out_rc) *out_rc = FsError_PathAlreadyExists;
                return ArchiveMetaStatus::Invalid;
            }
            seen_dbi_info = true;
            // Opaque INI: no speculative content-size ceiling
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            if (out_rc) *out_rc = Result_UnzOpenCurrentFile;
            return ArchiveMetaStatus::Invalid;
        }

        u8 meta_read_buf[512]{};
        u64 bytes_drained = 0;
        int read_res = 0;
        do {
            if (pbox) {
                const auto exit_rc = pbox->ShouldExitResult();
                if (R_FAILED(exit_rc)) {
                    unzCloseCurrentFile(zfile);
                    if (out_rc) *out_rc = exit_rc;
                    return ArchiveMetaStatus::Invalid;
                }
            }
            u8 drain_chunk[512];
            void* target_dest = (bytes_drained < sizeof(meta_read_buf))
                ? static_cast<void*>(meta_read_buf + bytes_drained)
                : static_cast<void*>(drain_chunk);
            const uLong target_cap = (bytes_drained < sizeof(meta_read_buf))
                ? static_cast<uLong>(sizeof(meta_read_buf) - bytes_drained)
                : static_cast<uLong>(sizeof(drain_chunk));

            read_res = unzReadCurrentFile(zfile, target_dest, target_cap);
            if (read_res < 0) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = Result_UnzReadCurrentFile;
                return ArchiveMetaStatus::Invalid;
            }
            if (static_cast<uLong>(read_res) > target_cap) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            if (std::numeric_limits<u64>::max() - bytes_drained < static_cast<u64>(read_res)) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            bytes_drained += static_cast<u64>(read_res);
            if (bytes_drained > info.uncompressed_size) {
                unzCloseCurrentFile(zfile);
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
        } while (read_res > 0);

        const int close_res = unzCloseCurrentFile(zfile);
        if (close_res == UNZ_CRCERROR) {
            if (out_rc) *out_rc = 0x8;
            return ArchiveMetaStatus::Invalid;
        }
        if (close_res != UNZ_OK) {
            if (out_rc) *out_rc = Result_UnzReadCurrentFile;
            return ArchiveMetaStatus::Invalid;
        }
        if (bytes_drained != info.uncompressed_size) {
            if (out_rc) *out_rc = FsError_InvalidSize;
            return ArchiveMetaStatus::Invalid;
        }

        if (reserved_kind == SaveReservedMetaKind::NxMeta) {
            if (!DecodeNxSaveMeta(meta_read_buf, bytes_drained, nx_meta)) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            has_valid_nx = true;
        } else if (reserved_kind == SaveReservedMetaKind::DbiExtra) {
            if (!DecodeDbiRaw512(meta_read_buf, dbi_extra_meta)) {
                if (out_rc) *out_rc = FsError_InvalidSize;
                return ArchiveMetaStatus::Invalid;
            }
            has_valid_dbi_extra = true;
        } else if (reserved_kind == SaveReservedMetaKind::DbiInfo) {
            const auto shown = static_cast<size_t>(std::min<u64>(bytes_drained, sizeof(meta_read_buf)));
            out.account_name = path::IniAccountName({reinterpret_cast<const char*>(meta_read_buf), shown});
        }
    }

    // After declared entry count require expected end-of-list; unexpected extra entry/error -> Invalid
    const int end_rc = unzGoToNextFile(zfile);
    if (end_rc != UNZ_END_OF_LIST_OF_FILE) {
        if (out_rc) *out_rc = (end_rc < 0) ? static_cast<Result>(Result_UnzGoToNextFile) : static_cast<Result>(FsError_InvalidSize);
        return ArchiveMetaStatus::Invalid;
    }

    // Explicit checked success rewind
    const int rewind_rc = unzGoToFirstFile(zfile);
    if (rewind_rc != UNZ_OK) {
        if (out_rc) *out_rc = Result_UnzGoToFirstFile;
        return ArchiveMetaStatus::Invalid;
    }

    if (!seen_nx_meta && !seen_dbi_extra && !seen_dbi_info) {
        success = true;
        return ArchiveMetaStatus::NoMetadata;
    }

    if (seen_nx_meta && !has_valid_nx) {
        if (out_rc) *out_rc = FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }
    if (seen_dbi_extra && !has_valid_dbi_extra) {
        if (out_rc) *out_rc = FsError_InvalidSize;
        return ArchiveMetaStatus::Invalid;
    }

    DecodedSaveMetadata local_out{};
    local_out.payload_count = out.payload_count;
    local_out.has_kefir_comment = out.has_kefir_comment;
    local_out.account_name = out.account_name;

    if (has_valid_nx && has_valid_dbi_extra) {
        if (!CompareCommonSourceFields(nx_meta, dbi_extra_meta)) {
            if (out_rc) *out_rc = FsError_InvalidSize;
            return ArchiveMetaStatus::Invalid;
        }
        local_out.meta = ToNXSaveMeta(nx_meta);
        local_out.source_space = nx_meta.source_space;
        local_out.has_nx_meta = true;
        local_out.has_dbi_extra = true;
        local_out.has_dbi_info = seen_dbi_info;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::Valid;
    }

    if (has_valid_nx) {
        local_out.meta = ToNXSaveMeta(nx_meta);
        local_out.source_space = nx_meta.source_space;
        local_out.has_nx_meta = true;
        local_out.has_dbi_info = seen_dbi_info;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::Valid;
    }

    if (has_valid_dbi_extra) {
        local_out.meta = ToNXSaveMeta(dbi_extra_meta);
        local_out.source_space = std::nullopt;
        local_out.has_dbi_extra = true;
        local_out.has_dbi_info = seen_dbi_info;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::Valid;
    }

    if (seen_dbi_info && !seen_nx_meta && !seen_dbi_extra) {
        local_out.has_dbi_info = true;
        out = local_out;
        success = true;
        return ArchiveMetaStatus::NoMetadata;
    }

    if (out_rc) *out_rc = FsError_InvalidSize;
    return ArchiveMetaStatus::Invalid;
}

} // namespace sphaira::ui::menu::save
