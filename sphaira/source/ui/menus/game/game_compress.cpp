// Game Actions > Compress game: estimate what an installed game would save, ask, then replace it with its
// LZ4-compressed copy through the hacBrewPackLZ engine (hbp_bridge.hpp).

#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "hbp_bridge.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <memory>

namespace sphaira::ui::menu::game {
namespace {

struct CompressState {
    hbp::Estimate estimate;
    s64 before{};
    s64 after{};
    std::string error;
};

auto ProgressInto(ProgressBox* pbox) -> hbp::ProgressFn {
    return [pbox, last = std::string{}](const char* label, s64 done, s64 total) mutable {
        if (last != label) {
            last = label;
            pbox->NewTransfer(label);
        }
        pbox->UpdateTransfer(done, total);
        return !pbox->ShouldExit();
    };
}

auto ResultOf(const std::string& error) -> Result {
    if (error.empty()) {
        R_SUCCEED();
    }
    return error == hbp::Stopped ? Result_TransferCancelled : Result_CompressFailed;
}

void ShowError(Result rc, const std::string& error) {
    if (rc != Result_TransferCancelled) {
        App::PushErrorBox(rc, "Compress failed!"_i18n + "\n" + error);
    }
}

auto BuildSummary(const hbp::Estimate& e) -> std::string {
    const auto saved = e.now > e.after ? e.now - e.after : 0;
    const auto percent = e.now ? saved * 100 / e.now : 0;
    std::string msg;
    msg += "Size now"_i18n + ": " + grid::FormatBytes(e.now) + "\n";
    msg += "After compressing"_i18n + ": ~" + grid::FormatBytes(e.after) + " (± " + grid::FormatBytes(e.margin) + ")\n";
    msg += "Space saved"_i18n + ": ~" + grid::FormatBytes(saved) + " (" + std::to_string(percent) + "%)\n\n";
    if (e.has_update) {
        msg += "The update becomes part of the game."_i18n + "\n";
    }
    msg += "Official updates do not install over a compressed game. Install the original game again first."_i18n + "\n";
    msg += "Needs firmware 12.0.0+ and sigpatches."_i18n;
    return msg;
}

void StartCompress(u64 app_id, const std::string& name, std::function<void()> on_done) {
    auto state = std::make_shared<CompressState>();
    App::Push<ProgressBox>(0, "Compressing"_i18n, name, [app_id, state](auto pbox) -> Result {
        state->error = hbp::CompressGame(app_id, state->before, state->after, ProgressInto(pbox));
        return ResultOf(state->error);
    }, [state, on_done](Result rc) {
        if (R_SUCCEEDED(rc)) {
            const auto freed = state->before > state->after ? state->before - state->after : 0;
            App::Notify("Compressed, freed"_i18n + " " + grid::FormatBytes(freed));
            on_done();
        } else {
            ShowError(rc, state->error);
        }
    // the engine runs deep call chains: 1 MiB stack, as its own app gives it.
    // cpu boost stays off: FastLoad drops the gpu to 76.8 MHz and the UI looks hung for minutes.
    }, 1, PRIO_PREEMPTIVE, 1024 * 1024, false);
}

} // namespace

void ShowCompressGame(u64 app_id, const std::string& name, std::function<void()> on_done) {
    auto state = std::make_shared<CompressState>();
    App::Push<ProgressBox>(0, "Estimating"_i18n, name, [app_id, state](auto pbox) -> Result {
        state->error = hbp::EstimateGame(app_id, state->estimate, ProgressInto(pbox));
        return ResultOf(state->error);
    }, [app_id, name, state, on_done](Result rc) {
        if (R_FAILED(rc)) {
            ShowError(rc, state->error);
            return;
        }
        if (state->estimate.already_compressed) {
            App::Notify("This game is already compressed"_i18n);
            return;
        }
        App::Push<OptionBox>(BuildSummary(state->estimate), "Back"_i18n, "Compress"_i18n, 1, [app_id, name, on_done](auto op_index) {
            if (op_index && *op_index) {
                StartCompress(app_id, name, on_done);
            }
        });
    }, 1, PRIO_PREEMPTIVE, 1024 * 1024, false);
}

} // namespace sphaira::ui::menu::game
