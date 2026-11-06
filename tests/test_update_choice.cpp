// Which update of a game the install queue keeps.
//   g++ -std=c++20 -I../sphaira/include test_update_choice.cpp -o t && ./t
#include "ui/menus/update_choice.hpp"

#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace sphaira::ui::menu::dbi;

namespace {

constexpr uint64_t GAME = 0x0100ABF008968800;
constexpr uint32_t V3 = 3 << 16, V6 = 6 << 16, V9 = 9 << 16;

// the user's answer to a question: 1 yes, 0 no, -1 none within 10 s
using Answers = std::function<int(const std::string& file, bool final_pass)>;

std::string Label(const std::string& file) {
    return file.substr(0, file.find(' '));
}

// AskUpdate with a user who answers at once or not at all.
std::optional<bool> Ask(AskMode mode, int answer, bool& deferred) {
    auto end = UpdateAskStep(mode, answer, false, answer >= 0 ? 0 : UpdateAskGraceNs);
    if (end == AskEnd::Waiting) end = UpdateAskStep(mode, answer, true, 0); // the end of the queue: the user skips
    if (end == AskEnd::Answered) return answer == 1;
    deferred = end == AskEnd::Defer;
    return std::nullopt;
}

// One queue as dbi_local.cpp / dbi_usb.cpp drive it, with a fake console (update version
// per game) and "Allow downgrade" off. Returns "install|skip|defer <label>" in order.
std::vector<std::string> RunQueue(long mode, const std::vector<std::string>& files,
                                  std::unordered_map<uint64_t, uint32_t>& console, const Answers& answers,
                                  int* asked = nullptr) {
    std::vector<NameTitle> titles;
    for (const auto& f : files) titles.push_back(ParseNameTitle(f));
    std::unordered_map<uint64_t, uint32_t> picks;
    std::vector<std::string> log;
    DeferQueue order;
    while (const auto item = order.Next(files.size())) {
        const auto& file = files[item->index];
        const auto mode_now = item->final_pass ? AskMode::Wait : AskMode::Defer;
        bool deferred = false;
        auto ask = [&] {
            if (asked) ++*asked;
            return Ask(mode_now, answers(file, item->final_pass), deferred);
        };
        auto no_answer = [&] {
            if (deferred) order.Defer(item->index);
            log.push_back((deferred ? "defer " : "skip ") + Label(file));
        };

        // CheckQueuedUpdate
        QueueUpdates q{};
        const auto r = ResolveQueuedUpdate(mode, titles, item->index, picks, q, [&](int, uint32_t) { return ask(); });
        if (r == QueuedUpdate::NoAnswer) { no_answer(); continue; }
        if (r != QueuedUpdate::Install && r != QueuedUpdate::Picked) { log.push_back("skip " + Label(file)); continue; }

        // yati GetLatestVersion -> PromptDowngrade
        const auto t = titles[item->index];
        const auto on_console = console.find(t.id);
        if (IsUpdateId(t.id) && on_console != console.end() && on_console->second > t.version) {
            const auto step = OlderThanConsoleStep(mode, q.count, r == QueuedUpdate::Picked);
            if (step == UpdateStep::Skip) { log.push_back("skip " + Label(file)); continue; }
            if (step == UpdateStep::Ask) {
                const auto a = ask();
                if (!a) { no_answer(); continue; }
                if (!*a) { log.push_back("skip " + Label(file)); continue; }
            }
        }
        console[t.id] = t.version;
        log.push_back("install " + Label(file));
    }
    return log;
}

using Log = std::vector<std::string>;

const std::string F_V3 = "v3 [0100ABF008968800][v196608].nsp";
const std::string F_V6 = "v6 [0100ABF008968800][v393216].nsp";
const std::string F_OTHER = "other [0100000000010000][v0].nsp";

void TestDecisions() {
    // file names as the queue holds them: base, v3, v6 of one game, and another game's update
    std::vector<NameTitle> q{
        ParseNameTitle("Game [0100ABF008968000][v0].nsp"),
        ParseNameTitle(F_V3),
        ParseNameTitle(F_V6),
        ParseNameTitle("Other [0100000000010800][v65536].nsp"),
    };

    assert(FindQueueUpdates(q, 0).count == 0); // base game: not an update
    const auto v3 = FindQueueUpdates(q, 1);
    const auto v6 = FindQueueUpdates(q, 2);
    assert(v3.count == 2 && v3.newer);
    assert(v6.count == 2 && !v6.newer);
    assert(FindQueueUpdates(q, 3).count == 1);
    assert(FindQueueUpdates(q, 9).count == 0);

    // an unticked entry does not count
    auto unticked = q;
    unticked[2].id = 0;
    assert(FindQueueUpdates(unticked, 1).count == 1);

    assert(QueueUpdateStep(UpdateChoice_Newest, v3, false, 0, V3) == UpdateStep::Skip);
    assert(QueueUpdateStep(UpdateChoice_Newest, v6, false, 0, V6) == UpdateStep::Install);
    assert(QueueUpdateStep(UpdateChoice_Newest, FindQueueUpdates(q, 3), false, 0, 65536) == UpdateStep::Install);
    assert(QueueUpdateStep(UpdateChoice_Ask, v3, false, 0, V3) == UpdateStep::Ask);
    assert(QueueUpdateStep(UpdateChoice_Ask, v6, true, V3, V6) == UpdateStep::Skip);
    assert(QueueUpdateStep(UpdateChoice_Ask, v3, true, V3, V3) == UpdateStep::Install);

    assert(OlderThanConsoleStep(UpdateChoice_Newest, 1, false) == UpdateStep::Ask);
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 0, false) == UpdateStep::Ask); // stream
    assert(OlderThanConsoleStep(UpdateChoice_Ask, 1, false) == UpdateStep::Ask);
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 2, false) == UpdateStep::Skip);
    assert(OlderThanConsoleStep(UpdateChoice_Ask, 2, false) == UpdateStep::Ask);
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 2, true) == UpdateStep::Install);
}

void TestAskTimeout() {
    // under 10 s every mode waits
    assert(UpdateAskStep(AskMode::Defer, -1, false, UpdateAskGraceNs - 1) == AskEnd::Waiting);
    assert(UpdateAskStep(AskMode::Stream, -2, false, UpdateAskGraceNs - 1) == AskEnd::Waiting);
    // after 10 s: a queue moves the file to its end, MTP/FTP/web closes the box
    assert(UpdateAskStep(AskMode::Defer, -1, false, UpdateAskGraceNs) == AskEnd::Defer);
    assert(UpdateAskStep(AskMode::Stream, -1, false, UpdateAskGraceNs) == AskEnd::Expire);
    // the final pass never times out, so a file is deferred at most once
    assert(UpdateAskStep(AskMode::Wait, -2, false, UpdateAskGraceNs * 1000) == AskEnd::Waiting);
    // cancel or skip while asking: no deferral
    assert(UpdateAskStep(AskMode::Defer, -2, true, 0) == AskEnd::Expire);
    assert(UpdateAskStep(AskMode::Wait, -2, true, 0) == AskEnd::Expire);
    // an answer counts in every mode, also one given to a box that stayed open
    assert(UpdateAskStep(AskMode::Wait, 0, false, 0) == AskEnd::Answered);
    assert(UpdateAskStep(AskMode::Stream, 1, true, UpdateAskGraceNs) == AskEnd::Answered);
}

void TestDeferQueue() {
    DeferQueue order;
    std::vector<std::pair<size_t, bool>> seen;
    while (const auto item = order.Next(3)) {
        seen.emplace_back(item->index, item->final_pass);
        if (item->index == 1 && !item->final_pass) order.Defer(1);
    }
    assert((seen == std::vector<std::pair<size_t, bool>>{{0, false}, {1, false}, {2, false}, {1, true}}));

    // the USB live queue grows during the final pass: new files go first, in the normal pass
    DeferQueue grow;
    assert(grow.Next(1)->index == 0);
    grow.Defer(0);
    assert(grow.Next(1)->final_pass);
    const auto added = grow.Next(2);
    assert(added->index == 1 && !added->final_pass);
    assert(!grow.Next(2));

    // the live queue keeps the entries up to the file that installs
    assert(LiveQueueActive({2, false}, 5) == 2);
    assert(LiveQueueActive({1, true}, 5) == 4);
}

void TestScenarios() {
    const Answers silent = [](const std::string&, bool) { return -1; };

    // Newest: v3 and v6 in a queue, in both orders; v6 installs, no question
    {
        std::unordered_map<uint64_t, uint32_t> console;
        int asked = 0;
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_V6}, console, silent, &asked) == Log{"skip v3", "install v6"}));
        assert(console[GAME] == V6 && asked == 0);
        console.clear();
        assert((RunQueue(UpdateChoice_Newest, {F_V6, F_V3}, console, silent) == Log{"install v6", "skip v3"}));
        assert(console[GAME] == V6);
    }
    // Ask: the user picks v3 at once; v6 is skipped without a question
    {
        std::unordered_map<uint64_t, uint32_t> console;
        int asked = 0;
        const Answers yes_v3 = [](const std::string& f, bool) { return f == F_V3 ? 1 : -1; };
        assert((RunQueue(UpdateChoice_Ask, {F_V3, F_V6}, console, yes_v3, &asked) == Log{"install v3", "skip v6"}));
        assert(console[GAME] == V3 && asked == 1);
    }
    // Ask, no answer in the first pass: both go to the end; there the user says no to v3, yes to v6
    {
        std::unordered_map<uint64_t, uint32_t> console;
        const Answers late = [](const std::string& f, bool final_pass) { return final_pass ? (f == F_V6 ? 1 : 0) : -1; };
        assert((RunQueue(UpdateChoice_Ask, {F_V3, F_V6, F_OTHER}, console, late) ==
                Log{"defer v3", "defer v6", "install other", "skip v3", "install v6"}));
        assert(console[GAME] == V6);
    }
    // the only update, older than the console, no answer: the next file installs first,
    // the old update is asked again at the end (a translation needs it)
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V6}};
        const Answers late_yes = [](const std::string&, bool final_pass) { return final_pass ? 1 : -1; };
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_OTHER}, console, late_yes) ==
                Log{"defer v3", "install other", "install v3"}));
        assert(console[GAME] == V3);
        // the user says no at the end: the console keeps v6
        console[GAME] = V6;
        const Answers late_no = [](const std::string&, bool final_pass) { return final_pass ? 0 : -1; };
        assert((RunQueue(UpdateChoice_Newest, {F_V3}, console, late_no) == Log{"defer v3", "skip v3"}));
        assert(console[GAME] == V6);
    }
    // Newest, both in the queue are older than the console: nothing installs, no question
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V9}};
        int asked = 0;
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_V6}, console, silent, &asked) == Log{"skip v3", "skip v6"}));
        assert(console[GAME] == V9 && asked == 0);
    }
    // Ask: the user picks v3 in the queue, the console has v6: no second question
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V6}};
        int asked = 0;
        const Answers yes_v3 = [](const std::string& f, bool) { return f == F_V3 ? 1 : 0; };
        assert((RunQueue(UpdateChoice_Ask, {F_V3, F_V6}, console, yes_v3, &asked) == Log{"install v3", "skip v6"}));
        assert(console[GAME] == V3 && asked == 1);
    }
    // MTP/FTP/web: no queue (count 0), an older update, no answer in 10 s: the console keeps v6
    {
        bool deferred = false;
        assert(OlderThanConsoleStep(UpdateChoice_Newest, 0, false) == UpdateStep::Ask);
        assert(!Ask(AskMode::Stream, -1, deferred) && !deferred);
        assert(Ask(AskMode::Stream, 1, deferred) == true);
    }
    // the DBI Backend sends one update of the game: it installs as sent
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V3}};
        assert((RunQueue(UpdateChoice_Newest, {F_V6}, console, silent) == Log{"install v6"}));
    }
}

} // namespace

int main() {
    TestDecisions();
    TestAskTimeout();
    TestDeferQueue();
    TestScenarios();
    std::puts("test_update_choice: ok");
    return 0;
}
