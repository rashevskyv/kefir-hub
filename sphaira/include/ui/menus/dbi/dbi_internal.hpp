#pragma once

#include "ui/menus/dbi_menu.hpp"
#include "ui/nvg_util.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::dbi {

constexpr u64 CONNECTION_TIMEOUT = UINT64_MAX;
constexpr u64 TRANSFER_TIMEOUT = UINT64_MAX;
constexpr u64 FINISHED_TIMEOUT = 1e+9 * 3;
constexpr size_t MAX_LOG_LINES = 128;
// how long to wait for the host to re-enumerate us and answer the replayed
// handshake after the link dropped. Bounded, unlike the session timeouts
// above, so an unplugged cable ends the queue instead of hanging on it.
constexpr u64 RECONNECT_TIMEOUT = 1e+9 * 15;
// a blip usually recovers on the first attempt; the cap is there so a link
// that keeps flapping ends the session rather than looping forever.
constexpr u32 MAX_LINK_RETRIES = 3;

void thread_func(void* user);

auto ResultText(Result rc) -> std::string;
auto IsDbiSessionError(Result rc) -> bool;
void AddSizeSaturated(s64& total, s64 value);
s64 PlanSize(const QueueEntry& entry);
s64 QueuePackageSize(const QueueEntry& entry);
bool IsEntryAlreadyInstalled(const QueueEntry& entry);

// one "label: value" cell of a stats row.
struct StatItem {
    std::string label{};
    std::string value{};
    // value colour; the label is always drawn in the info colour.
    std::optional<NVGcolor> colour{};
};

// draws stat cells left to right, with the label faked bold (over-drawn -- no
// bold font face is loaded) so the labels stand out from the numbers.
void DrawStatRow(NVGcontext* vg, NVGcolor info_col, float x0, float y, float size, const std::vector<StatItem>& items);

// "how long until those bytes are written at that rate". Empty when either
// number cannot answer the question.
auto FormatEta(s64 bytes_left, s64 bps) -> std::string;

void DrawCheckbox(NVGcontext* vg, Theme* theme, const Vec4& row, bool selected);

} // namespace sphaira::ui::menu::dbi