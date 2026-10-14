#include "zero_fill.hpp"
#include "defines.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace sphaira::zero_fill {
namespace {

// left free so the system can still write saves and logs while the fill runs.
// ponytail: fixed 64 MiB, measure what HOS needs if a fill ever fails a system write.
constexpr s64 RESERVE = 64LL * 1024 * 1024;

// one fixed path, so a file left by a crash or a power-off is found and removed at the next start.
constexpr char TEMP_FILE[] = "/kefir-hub-zero-fill.tmp";

// short writes keep each fs call short: the menu behind polls the card's free space and a slow
// card would otherwise hold it for seconds per write.
constexpr s64 CHUNK_SIZE = 1024 * 1024;

auto Seconds(u64 start_tick) -> double {
    return armTicksToNs(armGetSystemTick() - start_tick) / 1e9;
}

auto Size(s64 bytes) -> std::string {
    return ui::menu::grid::FormatBytes(static_cast<u64>(std::max<s64>(0, bytes)));
}

void RemoveTempFile(fs::FsNativeSd& fs) {
    if (!fs.FileExists(TEMP_FILE)) {
        return;
    }
    const auto t = armGetSystemTick();
    const auto rc = fs.DeleteFile(TEMP_FILE);
    log_write("[ZERO] temporary file removed rc 0x%X in %.1fs\n", rc, Seconds(t));
}

} // namespace

void RemoveLeftover() {
    fs::FsNativeSd fs;
    RemoveTempFile(fs);
}

Result FillSdFreeSpace(ui::ProgressBox* pbox) {
    // hours on a slow card: the console must not sleep and the screen must not dim.
    App::SetAutoSleepDisabled(true);
    ON_SCOPE_EXIT(App::SetAutoSleepDisabled(false));

    fs::FsNativeSd fs;
    RemoveTempFile(fs);

    s64 free_space{};
    R_TRY(fs.GetFreeSpace("/", std::addressof(free_space)));
    const s64 size = free_space - RESERVE;
    log_write("[ZERO] sd free %lld fill %lld\n", (long long)free_space, (long long)size);
    if (size <= 0) {
        R_SUCCEED();
    }

    // the write speed plot, as in the install view
    pbox->SetShowGraph(true);

    // the file grows with each write: no up-front reservation, so the progress and Stop work at once.
    R_TRY(fs.CreateFile(TEMP_FILE, 0, FsCreateOption_BigFile));

    // the file is the whole point only while it is being written; it never stays.
    // freeing a card-sized file takes as long as the card needs: say so.
    ON_SCOPE_EXIT(
        pbox->SetHideSpeed(true);
        pbox->NewTransfer("Step 2 of 2: removing the temporary file, do not turn the console off..."_i18n);
        RemoveTempFile(fs);
    );

    {
        fs::File f;
        R_TRY(fs.OpenFile(TEMP_FILE, FsOpenMode_Write | FsOpenMode_Append, std::addressof(f)));
        const std::vector<u8> zeros(CHUNK_SIZE);

        pbox->SetHideSpeed(false);
        pbox->NewTransfer("Step 1 of 2: writing zeros"_i18n).UpdateTransfer(0, size);
        const auto t0 = armGetSystemTick();
        u64 label_tick = t0;
        u64 active_tick = t0;
        for (s64 offset = 0; offset < size; ) {
            R_TRY(pbox->ShouldExitResult());
            const auto n = std::min(CHUNK_SIZE, size - offset);
            R_TRY(f.Write(offset, zeros.data(), n, FsWriteOption_None));
            offset += n;
            pbox->UpdateTransfer(offset, size);
            // back to back writes would keep every other user of the card (the menu, the log) waiting.
            svcSleepThread(1'000'000);

            // how much is done, once a second
            if (Seconds(label_tick) >= 1.0) {
                label_tick = armGetSystemTick();
                pbox->SetTransfer(std::string{"Step 1 of 2: writing zeros"_i18n} + ": " + Size(offset) + " / " + Size(size));
            }
            // started from the Homebrew Menu the media playback state does not stop the screen dimming:
            // count as user activity.
            if (Seconds(active_tick) >= 10.0) {
                active_tick = armGetSystemTick();
                appletReportUserIsActive();
            }
        }
        log_write("[ZERO] written %lld in %.1fs\n", (long long)size, Seconds(t0));
    } // the file is closed here, before the removal above

    R_SUCCEED();
}

} // namespace sphaira::zero_fill
