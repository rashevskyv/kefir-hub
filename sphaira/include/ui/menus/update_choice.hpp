#pragma once

#include "option.hpp"
#include "ui/menus/install_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

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

// How long an update question waits. Stream: MTP/FTP/web, the file cannot wait.
// Defer: first pass of a queue. Wait: the files that come round again at the end.
enum class AskMode { Stream, Defer, Wait };
enum class AskEnd { Waiting, Answered, Defer, Expire };
// ponytail: fixed 10 s before the file moves on; a setting if someone wants longer.
inline constexpr uint64_t UpdateAskGraceNs = 10'000'000'000ULL;

// choice: < 0 no answer yet, 0 no, 1 yes. interrupted: cancel, skip or exit.
constexpr AskEnd UpdateAskStep(AskMode mode, int choice, bool interrupted, uint64_t waited_ns) {
    if (choice >= 0) return AskEnd::Answered;
    const bool timed_out = mode != AskMode::Wait && waited_ns >= UpdateAskGraceNs;
    if (!interrupted && !timed_out) return AskEnd::Waiting;
    return mode == AskMode::Defer && !interrupted ? AskEnd::Defer : AskEnd::Expire;
}

enum class QueuedUpdate {
    Install,          // not a queued duplicate, or the newest one
    Picked,           // the user chose this version
    SkipNewerInQueue, // Newest: the queue has a higher version
    SkipOtherPicked,  // another version of this game was chosen
    SkipNo,           // the user said no
    NoAnswer,         // the question ended without an answer
};

// Before a queued file starts. picks: per game, the version chosen this session.
// ask(count, version) shows the question and returns the answer, nullopt for none.
template <typename Ask>
QueuedUpdate ResolveQueuedUpdate(long mode, std::span<const NameTitle> titles, size_t index,
                                 std::unordered_map<uint64_t, uint32_t>& picks, QueueUpdates& q, Ask&& ask) {
    q = FindQueueUpdates(titles, index);
    if (!q.count) return QueuedUpdate::Install;
    const auto self = titles[index];
    const auto pick = picks.find(self.id);
    const bool has_pick = pick != picks.end();
    switch (QueueUpdateStep(mode, q, has_pick, has_pick ? pick->second : 0, self.version)) {
        case UpdateStep::Install: return has_pick ? QueuedUpdate::Picked : QueuedUpdate::Install;
        case UpdateStep::Skip: return has_pick ? QueuedUpdate::SkipOtherPicked : QueuedUpdate::SkipNewerInQueue;
        case UpdateStep::Ask: break;
    }
    const std::optional<bool> answer = ask(q.count, self.version);
    if (!answer) return QueuedUpdate::NoAnswer;
    if (!*answer) return QueuedUpdate::SkipNo;
    picks[self.id] = self.version;
    return QueuedUpdate::Picked;
}

// The install order: the queue once, then the files that got no answer, once more.
class DeferQueue {
public:
    struct Item { size_t index; bool final_pass; };
    std::optional<Item> Next(size_t size) {
        if (m_cursor < size) return Item{m_cursor++, false};
        if (m_later < m_deferred.size()) return Item{m_deferred[m_later++], true};
        return std::nullopt;
    }
    void Defer(size_t index) { m_deferred.push_back(index); }
private:
    std::vector<size_t> m_deferred;
    size_t m_cursor{}, m_later{};
};

// USB live queue freezes the entries up to this index. In the final pass all of them,
// so a deferred file does not cut the queue back to itself.
constexpr size_t LiveQueueActive(DeferQueue::Item item, size_t size) {
    return item.final_pass ? size - 1 : item.index;
}

} // namespace sphaira::ui::menu::dbi
