#pragma once

#include "option.hpp"
#include "ui/menus/install_plan.hpp"

#include <cstddef>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

// What an install question does. Defer: the first pass of a queue notes it for the Resolve
// page, no box. Wait: after the page, a box until answered. Stream: MTP/FTP/web, the data
// cannot wait, a box for 10 s.
enum class AskMode { Stream, Defer, Wait };
enum class AskEnd { Waiting, Answered, Expire };
// ponytail: fixed 10 s before a stream keeps the console as it is; a setting if someone wants longer.
inline constexpr uint64_t UpdateAskGraceNs = 10'000'000'000ULL;

// A shown box. choice: < 0 no answer yet, 0 no, 1 yes. interrupted: cancel, skip or exit.
constexpr AskEnd UpdateAskStep(AskMode mode, int choice, bool interrupted, uint64_t waited_ns) {
    if (choice >= 0) return AskEnd::Answered;
    const bool timed_out = mode == AskMode::Stream && waited_ns >= UpdateAskGraceNs;
    return interrupted || timed_out ? AskEnd::Expire : AskEnd::Waiting;
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

// The install order: the queue once, then the files that had a question, once more.
// starts_final: the first file after the queue; the questions are resolved before it.
class DeferQueue {
public:
    struct Item { size_t index; bool final_pass; bool starts_final{}; };
    std::optional<Item> Next(size_t size) {
        if (m_cursor < size) {
            m_in_final = false;
            return Item{m_cursor++, false};
        }
        if (m_later < m_deferred.size()) {
            const bool starts = !m_in_final;
            m_in_final = true;
            return Item{m_deferred[m_later++], true, starts};
        }
        return std::nullopt;
    }
    void Defer(size_t index) { m_deferred.push_back(index); }
private:
    std::vector<size_t> m_deferred;
    size_t m_cursor{}, m_later{};
    bool m_in_final{};
};

// A question the queue did not wait for. It is answered on the Resolve page after the queue.
enum class PendingKind {
    Reinstall, // this version is already on the console
    Older,     // an update older than the console's
    Several,   // Ask mode: one of several updates of the game in the queue
};

struct PendingInstall {
    size_t file{};                   // queue index
    PendingKind kind{};
    uint64_t id{};                   // title id from the file name
    uint32_t version{};              // raw, from the file name or the cnmt
    std::optional<uint32_t> console; // raw version on the console, nullopt when none
    std::string game;
};

// One row of the Resolve page: a game, or one file for Reinstall.
// choice 0 keeps the console as it is; choice k installs files[k - 1], the rest are skipped.
struct ResolveRow {
    std::string game;
    PendingKind kind{};
    std::optional<uint32_t> console;
    std::vector<size_t> files;
    std::vector<uint32_t> versions;
    int choice{};
};

// The updates of one game share a row; each Reinstall file has its own.
inline std::vector<ResolveRow> BuildResolveRows(std::span<const PendingInstall> pending) {
    std::vector<ResolveRow> rows;
    std::vector<uint64_t> row_ids; // title id per row, 0 for a Reinstall row
    for (const auto& p : pending) {
        const uint64_t id = p.kind == PendingKind::Reinstall ? 0 : p.id;
        ResolveRow* row = nullptr;
        for (size_t r = 0; id && r < rows.size(); r++) {
            if (row_ids[r] == id) row = &rows[r];
        }
        if (row && std::find(row->files.begin(), row->files.end(), p.file) != row->files.end()) continue;
        if (!row) {
            rows.push_back({p.game, p.kind, p.console, {}, {}, 0});
            row_ids.push_back(id);
            row = &rows.back();
        } else {
            row->kind = PendingKind::Several;
            if (!row->console) row->console = p.console;
        }
        row->files.push_back(p.file);
        row->versions.push_back(p.version);
    }
    return rows;
}

// Between the worker and the Resolve page. The page writes the choices, then sets done;
// the worker sets done too when the session ends, and the page closes.
struct ResolveData {
    std::vector<ResolveRow> rows;
    std::atomic<bool> shown{};
    std::atomic<bool> done{};
};

// file index -> install it (true) or skip it (false)
inline std::unordered_map<size_t, bool> ResolveAnswers(std::span<const ResolveRow> rows) {
    std::unordered_map<size_t, bool> out;
    for (const auto& r : rows) {
        for (size_t k = 0; k < r.files.size(); k++) {
            out[r.files[k]] = r.choice == static_cast<int>(k) + 1;
        }
    }
    return out;
}

// "Zelda [0100...][v196608].nsp" -> "Zelda"
inline std::string GameName(std::string_view file_name) {
    auto end = file_name.find('[');
    if (end == std::string_view::npos) end = file_name.rfind('.');
    auto name = file_name.substr(0, end);
    while (!name.empty() && (name.back() == ' ' || name.back() == '_')) name.remove_suffix(1);
    return std::string{name.empty() ? file_name : name};
}

// USB live queue freezes the entries up to this index. In the final pass all of them,
// so a deferred file does not cut the queue back to itself.
constexpr size_t LiveQueueActive(DeferQueue::Item item, size_t size) {
    return item.final_pass ? size - 1 : item.index;
}

} // namespace sphaira::ui::menu::dbi
