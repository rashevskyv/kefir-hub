#include "ui/menus/save/save_slot_backend.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"
#include <minizip/unzip.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace sphaira::ui::menu::save {

namespace {

struct SaveReaderContext {
    zlib_filefunc64_def base_funcs{};
    bool io_error{false};
    bool close_error{false};

    [[nodiscard]] bool HasError() const { return io_error || close_error; }

    void InitFileFunc(zlib_filefunc64_def* funcs) {
        mz::FileFuncStdio(&base_funcs);
        *funcs = base_funcs;
        funcs->opaque = this;
        funcs->zopen64_file = [](voidpf opaque, const void* filename, int mode) -> voidpf {
            auto self = static_cast<SaveReaderContext*>(opaque);
            return self->base_funcs.zopen64_file(self->base_funcs.opaque, filename, mode);
        };
        funcs->zread_file = [](voidpf opaque, voidpf stream, void* buf, uLong size) -> uLong {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zread_file(self->base_funcs.opaque, stream, buf, size);
            if (res < size && self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                self->io_error = true;
            }
            return res;
        };
        funcs->zseek64_file = [](voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) -> long {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zseek64_file(self->base_funcs.opaque, stream, offset, origin);
            if (res != 0) self->io_error = true;
            return res;
        };
        funcs->ztell64_file = [](voidpf opaque, voidpf stream) -> ZPOS64_T {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.ztell64_file(self->base_funcs.opaque, stream);
            if (res == static_cast<ZPOS64_T>(-1) || (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream))) {
                self->io_error = true;
            }
            return res;
        };
        funcs->zclose_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = self->base_funcs.zclose_file ? self->base_funcs.zclose_file(self->base_funcs.opaque, stream) : 0;
            if (res != 0) self->close_error = true;
            return res;
        };
        funcs->zerror_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = self->base_funcs.zerror_file ? self->base_funcs.zerror_file(self->base_funcs.opaque, stream) : 0;
            if (res != 0) self->io_error = true;
            return res;
        };
    }
};

} // namespace

auto InspectSaveArchiveAdmission(
    const fs::FsPath& archive_path,
    ProgressBox* pbox,
    bool allow_empty
) -> SaveArchiveAdmissionResult {
    SaveArchiveAdmissionResult result{};

    SaveReaderContext reader_ctx;
    zlib_filefunc64_def file_func;
    reader_ctx.InitFileFunc(&file_func);

    auto zfile = unzOpen2_64(archive_path.s, &file_func);
    if (!zfile) {
        result.rc = Result_UnzOpen2_64;
        return result;
    }

    bool zfile_closed = false;
    const auto close_zfile = [&]() {
        if (!zfile_closed && zfile) {
            const int close_res = unzClose(zfile);
            zfile_closed = true;
            if (close_res != UNZ_OK || reader_ctx.close_error) {
                return false;
            }
        }
        return true;
    };

    const auto save_filter = [](const fs::FsPath& name, fs::FsPath& /*path*/) -> bool {
        return !IsSaveReservedMetadataRoot(name.s);
    };

    thread::UnzipPayloadSummary summary{};
    thread::UnzipPayloadInventory inventory{};
    const auto preflight_rc = thread::TransferUnzipPreflight(
        pbox, zfile, "/", save_filter, true, &summary, &inventory, allow_empty);

    if (R_FAILED(preflight_rc)) {
        close_zfile();
        result.rc = preflight_rc;
        return result;
    }

    if (reader_ctx.HasError()) {
        close_zfile();
        result.rc = Result_UnzOpen2_64;
        return result;
    }

    if (!allow_empty && summary.file_count == 0) {
        close_zfile();
        result.rc = FsError_PathNotFound;
        return result;
    }

    DecodedSaveMetadata meta{};
    Result meta_rc = 0;
    const auto meta_status = ReadArchiveSaveMetadata(zfile, pbox, meta, &meta_rc);
    if (meta_status == ArchiveMetaStatus::Invalid) {
        close_zfile();
        result.rc = R_FAILED(meta_rc) ? meta_rc : Result_UnzOpen2_64;
        return result;
    }

    if (!close_zfile() || reader_ctx.HasError()) {
        result.rc = Result_UnzOpen2_64;
        return result;
    }

    result.admitted = true;
    result.rc = 0;
    if (meta_status == ArchiveMetaStatus::Valid && (meta.has_nx_meta || meta.has_dbi_extra)) {
        result.sizing.has_sizing = true;
        result.sizing.data_size = meta.meta.data_size;
        result.sizing.journal_size = meta.meta.journal_size;
        result.sizing.has_metadata = true;
        result.sizing.owner_id = meta.meta.owner_id;
        result.sizing.attr = meta.meta.attr;
    }
    result.payload_file_count = summary.file_count;
    result.payload_directory_count = summary.directory_count;
    result.payload_file_bytes = summary.file_bytes;
    return result;
}

} // namespace sphaira::ui::menu::save
