#include "zero_fill.hpp"
#include "defines.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::zero_fill {
namespace {

// left free so the system can still write saves and logs while the fill runs.
// ponytail: fixed 64 MiB, measure what HOS needs if a fill ever fails a system write.
constexpr s64 RESERVE = 64LL * 1024 * 1024;

auto Seconds(u64 start_tick) -> double {
    return armTicksToNs(armGetSystemTick() - start_tick) / 1e9;
}

auto Size(s64 bytes) -> std::string {
    return ui::menu::grid::FormatBytes(static_cast<u64>(std::max<s64>(0, bytes)));
}

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

    // the write speed plot, as in the install view
    pbox->SetShowGraph(true);

    NcmPlaceHolderId placeholder_id{};
    R_TRY(ncmContentStorageGeneratePlaceHolderId(std::addressof(cs), std::addressof(placeholder_id)));
    NcmContentId content_id{};
    static_assert(sizeof(content_id) == sizeof(placeholder_id));
    std::memcpy(std::addressof(content_id), std::addressof(placeholder_id), sizeof(content_id));

    // step 1: the system reserves one temporary file as big as the free space. No bytes move yet, so
    // there is no speed to show and Stop works only once this call returns.
    pbox->SetHideSpeed(true);
    pbox->NewTransfer(std::string{"Step 1 of 3: reserving the free space"_i18n} + " (" + Size(size) + ")");
    auto t0 = armGetSystemTick();
    R_TRY(ncmContentStorageCreatePlaceHolder(std::addressof(cs), std::addressof(content_id), std::addressof(placeholder_id), size));
    log_write("[ZERO] reserved in %.1fs\n", Seconds(t0));

    // the placeholder is the whole point only while it is being written; it never stays.
    // deleting it releases the whole file, which takes as long as the system needs: say so.
    ON_SCOPE_EXIT(
        pbox->SetHideSpeed(true);
        pbox->NewTransfer("Step 3 of 3: removing the temporary file, do not turn the console off..."_i18n);
        const auto t = armGetSystemTick();
        ncmContentStorageDeletePlaceHolder(std::addressof(cs), std::addressof(placeholder_id));
        log_write("[ZERO] placeholder removed in %.1fs\n", Seconds(t));
    );

    // same chunk sizing as the title move: each write is one transaction on bis.
    const s64 chunk_size = App::IsFileBaseEmummc() ? 512LL * 1024 : 4LL * 1024 * 1024;
    const std::vector<u8> zeros(chunk_size);

    // step 2: the writing, with speed, remaining time and the plot.
    pbox->SetHideSpeed(false);
    pbox->NewTransfer("Step 2 of 3: writing zeros"_i18n).UpdateTransfer(0, size);
    t0 = armGetSystemTick();
    u64 label_tick = t0;
    for (s64 offset = 0; offset < size; ) {
        R_TRY(pbox->ShouldExitResult());
        const auto n = std::min(chunk_size, size - offset);
        R_TRY(ncmContentStorageWritePlaceHolder(std::addressof(cs), std::addressof(placeholder_id), offset, zeros.data(), n));
        offset += n;
        pbox->UpdateTransfer(offset, size);

        // how much is done, once a second
        if (Seconds(label_tick) >= 1.0) {
            label_tick = armGetSystemTick();
            pbox->SetTransfer(std::string{"Step 2 of 3: writing zeros"_i18n} + ": " + Size(offset) + " / " + Size(size));
        }
    }
    log_write("[ZERO] written %lld in %.1fs\n", (long long)size, Seconds(t0));

    R_SUCCEED();
}

} // namespace sphaira::zero_fill
