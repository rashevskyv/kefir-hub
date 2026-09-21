#pragma once

#include "ui/menus/save/save_paths.hpp"
#include "minizip_helper.hpp"
#include <minizip/unzip.h>
#include <optional>

namespace sphaira::ui::menu::save {

struct DecodedSaveMetaInternal {
    u64 application_id{};
    AccountUid uid{};
    u64 system_save_data_id{};
    u8 save_data_type{0xFF};
    u8 save_data_rank{};
    u16 save_data_index{};
    u64 owner_id{};
    u64 timestamp{};
    u32 flags{};
    u32 unk_x54{};
    s64 data_size{};
    s64 journal_size{};
    u64 commit_id{};
    u64 raw_size{};
    std::optional<u8> source_space{};
};

auto ValidateDecodedSaveMeta(const DecodedSaveMetaInternal& m, bool is_86_layout) -> bool;
auto DecodeJksv85(const u8* p, DecodedSaveMetaInternal& out) -> bool;
auto DecodeJksvTail86(const u8* p, DecodedSaveMetaInternal& out) -> bool;
auto DecodeJksvMiddle86(const u8* p, DecodedSaveMetaInternal& out) -> bool;
auto CompareCommonSourceFields(const DecodedSaveMetaInternal& a, const DecodedSaveMetaInternal& b) -> bool;
auto DecodeJksv86WithAmbiguityCheck(const u8* p, DecodedSaveMetaInternal& out) -> bool;
auto DecodeSphaira128(const u8* p, DecodedSaveMetaInternal& out) -> bool;
auto DecodeDbiRaw512(const u8* p, DecodedSaveMetaInternal& out) -> bool;
auto DecodeNxSaveMeta(const u8* p, size_t size, DecodedSaveMetaInternal& out) -> bool;
auto ToNXSaveMeta(const DecodedSaveMetaInternal& d) -> NXSaveMeta;

struct SaveReaderContext {
    zlib_filefunc64_def base_funcs{};
    bool io_error{false};
    bool close_error{false};

    [[nodiscard]] bool HasError() const {
        return io_error || close_error;
    }

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
            if (res < size) {
                if (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                    self->io_error = true;
                }
            }
            return res;
        };
        funcs->zseek64_file = [](voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) -> long {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.zseek64_file(self->base_funcs.opaque, stream, offset, origin);
            if (res != 0) {
                self->io_error = true;
            }
            return res;
        };
        funcs->ztell64_file = [](voidpf opaque, voidpf stream) -> ZPOS64_T {
            auto self = static_cast<SaveReaderContext*>(opaque);
            const auto res = self->base_funcs.ztell64_file(self->base_funcs.opaque, stream);
            if (res == static_cast<ZPOS64_T>(-1)) {
                self->io_error = true;
            } else if (self->base_funcs.zerror_file && self->base_funcs.zerror_file(self->base_funcs.opaque, stream)) {
                self->io_error = true;
            }
            return res;
        };
        funcs->zclose_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = 0;
            if (self->base_funcs.zclose_file) {
                res = self->base_funcs.zclose_file(self->base_funcs.opaque, stream);
                if (res != 0) {
                    self->close_error = true;
                }
            }
            return res;
        };
        funcs->zerror_file = [](voidpf opaque, voidpf stream) -> int {
            auto self = static_cast<SaveReaderContext*>(opaque);
            int res = 0;
            if (self->base_funcs.zerror_file) {
                res = self->base_funcs.zerror_file(self->base_funcs.opaque, stream);
                if (res != 0) {
                    self->io_error = true;
                }
            }
            return res;
        };
    }
};

} // namespace sphaira::ui::menu::save
