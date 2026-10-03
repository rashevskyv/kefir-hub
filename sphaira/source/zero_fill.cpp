#include "zero_fill.hpp"
#include "defines.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace sphaira::zero_fill {
namespace {

// left free so the system can still write saves and logs while the fill runs.
// ponytail: fixed 64 MiB, measure what HOS needs if a fill ever fails a system write.
constexpr s64 RESERVE = 64LL * 1024 * 1024;

} // namespace

Result FillFreeSpace(NcmStorageId storage_id, ui::ProgressBox* pbox) {
    NcmContentStorage cs{};
    R_TRY(ncmOpenContentStorage(std::addressof(cs), storage_id));
    ON_SCOPE_EXIT(ncmContentStorageClose(std::addressof(cs)));

    s64 free_space{};
    R_TRY(ncmContentStorageGetFreeSpaceSize(std::addressof(cs), std::addressof(free_space)));
    const s64 size = free_space - RESERVE;
    log_write("[ZERO] storage %u free %lld fill %lld\n", (unsigned)storage_id, (long long)free_space, (long long)size);
    if (size <= 0) {
        R_SUCCEED();
    }

    NcmPlaceHolderId placeholder_id{};
    R_TRY(ncmContentStorageGeneratePlaceHolderId(std::addressof(cs), std::addressof(placeholder_id)));
    NcmContentId content_id{};
    static_assert(sizeof(content_id) == sizeof(placeholder_id));
    std::memcpy(std::addressof(content_id), std::addressof(placeholder_id), sizeof(content_id));

    pbox->NewTransfer("Allocating"_i18n).UpdateTransfer(0, size);
    R_TRY(ncmContentStorageCreatePlaceHolder(std::addressof(cs), std::addressof(content_id), std::addressof(placeholder_id), size));
    // the placeholder is the whole point only while it is being written; it never stays.
    ON_SCOPE_EXIT(ncmContentStorageDeletePlaceHolder(std::addressof(cs), std::addressof(placeholder_id)));

    // same chunk sizing as the title move: each write is one transaction on bis.
    const s64 chunk_size = App::IsFileBaseEmummc() ? 512LL * 1024 : 4LL * 1024 * 1024;
    const std::vector<u8> zeros(chunk_size);

    pbox->NewTransfer("Filling with zeros"_i18n);
    for (s64 offset = 0; offset < size; ) {
        R_TRY(pbox->ShouldExitResult());
        const auto n = std::min(chunk_size, size - offset);
        R_TRY(ncmContentStorageWritePlaceHolder(std::addressof(cs), std::addressof(placeholder_id), offset, zeros.data(), n));
        offset += n;
        pbox->UpdateTransfer(offset, size);
    }

    R_SUCCEED();
}

} // namespace sphaira::zero_fill
