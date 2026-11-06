#pragma once

#include "option.hpp"
#include "ui/menus/install_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sphaira::ui::menu::dbi {

// "Several updates of one game" setting.
enum UpdateChoiceMode : long {
    UpdateChoice_Newest = 0, // default: only the newest update of a game is installed
    UpdateChoice_Ask = 1,    // ask for each update until one is picked
};

// Settings > Install; defined in dbi_session.cpp (ENABLE_NETWORK_INSTALL builds).
option::OptionLong& UpdateChoiceOption();

enum class UpdateStep { Install, Skip, Ask };

// The ticked updates of one game in the queue, versions from the file names.
struct QueueUpdates {
    int count{};    // this file included; 0 when this file is not an update with a version tag
    bool newer{};   // another one has a higher version
};

constexpr bool IsUpdateId(uint64_t id) {
    return id && (id & 0xFFF) == 0x800;
}

// titles[i] is entry i of the queue; an unticked entry has id 0.
constexpr QueueUpdates FindQueueUpdates(std::span<const NameTitle> titles, size_t index) {
    QueueUpdates out{};
    if (index >= titles.size()) return out;
    const auto& self = titles[index];
    if (!IsUpdateId(self.id) || !self.has_version) return out;
    for (const auto& t : titles) {
        if (t.id != self.id || !t.has_version) continue;
        out.count++;
        if (t.version > self.version) out.newer = true;
    }
    return out;
}

// Before a queued update starts. pick: the version the user already chose for this game.
constexpr UpdateStep QueueUpdateStep(long mode, QueueUpdates q, bool has_pick, uint32_t pick, uint32_t version) {
    if (has_pick) return pick == version ? UpdateStep::Install : UpdateStep::Skip;
    if (q.count < 2) return UpdateStep::Install;
    if (mode == UpdateChoice_Ask) return UpdateStep::Ask;
    return q.newer ? UpdateStep::Skip : UpdateStep::Install;
}

// yati found a newer update on the console and "Allow downgrade" is off.
// count as in QueueUpdates; 0 when no queue knows the file (MTP, FTP, web).
// The only update of a game in the queue is asked about: a mod may need that old update.
constexpr UpdateStep OlderThanConsoleStep(long mode, int count, bool picked) {
    if (picked) return UpdateStep::Install;
    if (count < 2 || mode == UpdateChoice_Ask) return UpdateStep::Ask;
    return UpdateStep::Skip;
}

} // namespace sphaira::ui::menu::dbi
