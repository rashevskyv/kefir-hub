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

// The user on the Resolve page: the choice for each row (0 keeps the console).
// Also records the rows the page showed.
struct Page {
    std::function<int(const ResolveRow&)> choose = [](const ResolveRow&) { return 0; };
    std::vector<ResolveRow> shown;
};

std::string Label(const std::string& file) {
    return file.substr(0, file.find(' '));
}

// One queue as dbi_local.cpp / dbi_usb.cpp drive it, with a fake console (version per
// title id), "Allow downgrade" off and "Already installed" on Prompt.
// Returns "install|skip|defer <label>" in order.
std::vector<std::string> RunQueue(long mode, const std::vector<std::string>& files,
                                  std::unordered_map<uint64_t, uint32_t>& console, Page& page) {
    std::vector<NameTitle> titles;
    for (const auto& f : files) titles.push_back(ParseNameTitle(f));
    std::unordered_map<uint64_t, uint32_t> picks;
    std::vector<PendingInstall> pending;
    std::unordered_map<size_t, bool> resolved;
    std::vector<std::string> log;
    DeferQueue order;
    while (const auto item = order.Next(files.size())) {
        if (item->starts_final && !pending.empty()) { // ResolvePending
            auto rows = BuildResolveRows(pending);
            pending.clear();
            for (auto& r : rows) r.choice = page.choose(r);
            page.shown.insert(page.shown.end(), rows.begin(), rows.end());
            resolved = ResolveAnswers(rows);
        }
        const auto& file = files[item->index];
        const auto ask_mode = item->final_pass ? AskMode::Wait : AskMode::Defer;
        bool deferred = false;
        // AskOrDefer; in the Wait mode a question the page did not cover is skipped by the user
        auto ask = [&](PendingInstall p) -> std::optional<bool> {
            p.file = item->index;
            if (const auto it = resolved.find(p.file); it != resolved.end()) return it->second;
            if (ask_mode == AskMode::Defer) {
                p.game = GameName(file);
                pending.push_back(p);
                deferred = true;
            }
            return std::nullopt;
        };
        auto skip_or_defer = [&] {
            if (deferred) order.Defer(item->index);
            log.push_back((deferred ? "defer " : "skip ") + Label(file));
        };

        const auto t = titles[item->index];
        const auto on_console = console.find(t.id);
        const std::optional<uint32_t> console_v = on_console != console.end() ? std::optional{on_console->second} : std::nullopt;

        // CheckQueuedUpdate
        QueueUpdates q{};
        const auto r = ResolveQueuedUpdate(mode, titles, item->index, picks, q, [&](int, uint32_t version) {
            return ask({0, PendingKind::Several, t.id, version, console_v, {}});
        });
        if (r == QueuedUpdate::NoAnswer) { skip_or_defer(); continue; }
        if (r != QueuedUpdate::Install && r != QueuedUpdate::Picked) { log.push_back("skip " + Label(file)); continue; }

        // yati: PromptReinstall, then GetLatestVersion -> PromptDowngrade
        if (console_v && *console_v == t.version) {
            const auto a = ask({0, PendingKind::Reinstall, t.id, t.version, console_v, {}});
            if (!a || !*a) { skip_or_defer(); continue; }
        } else if (IsUpdateId(t.id) && console_v && *console_v > t.version) {
            const auto step = OlderThanConsoleStep(mode, q.count, r == QueuedUpdate::Picked);
            if (step == UpdateStep::Skip) { log.push_back("skip " + Label(file)); continue; }
            if (step == UpdateStep::Ask) {
                const auto a = ask({0, PendingKind::Older, t.id, t.version, console_v, {}});
                if (!a || !*a) { skip_or_defer(); continue; }
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
    // MTP/FTP/web: the box waits 10 s, then the console keeps what it has
    assert(UpdateAskStep(AskMode::Stream, -2, false, UpdateAskGraceNs - 1) == AskEnd::Waiting);
    assert(UpdateAskStep(AskMode::Stream, -1, false, UpdateAskGraceNs) == AskEnd::Expire);
    // after the Resolve page a box waits for the answer
    assert(UpdateAskStep(AskMode::Wait, -2, false, UpdateAskGraceNs * 1000) == AskEnd::Waiting);
    // cancel or skip while asking closes the box
    assert(UpdateAskStep(AskMode::Wait, -2, true, 0) == AskEnd::Expire);
    // an answer counts in every mode, also one given to a box that stayed open
    assert(UpdateAskStep(AskMode::Wait, 0, false, 0) == AskEnd::Answered);
    assert(UpdateAskStep(AskMode::Stream, 1, true, UpdateAskGraceNs) == AskEnd::Answered);
}

void TestDeferQueue() {
    DeferQueue order;
    std::vector<std::pair<size_t, bool>> seen;
    std::vector<bool> starts;
    while (const auto item = order.Next(3)) {
        seen.emplace_back(item->index, item->final_pass);
        starts.push_back(item->starts_final);
        if (!item->final_pass && item->index != 2) order.Defer(item->index);
    }
    assert((seen == std::vector<std::pair<size_t, bool>>{{0, false}, {1, false}, {2, false}, {0, true}, {1, true}}));
    // the Resolve page opens once, before the first file of the final pass
    assert((starts == std::vector<bool>{false, false, false, true, false}));

    // the USB live queue grows during the final pass: new files go first, in the normal pass,
    // and their questions get a Resolve page of their own
    DeferQueue grow;
    assert(grow.Next(1)->index == 0);
    grow.Defer(0);
    assert(grow.Next(1)->starts_final);
    const auto added = grow.Next(2);
    assert(added->index == 1 && !added->final_pass);
    grow.Defer(1);
    const auto again = grow.Next(2);
    assert(again->index == 1 && again->final_pass && again->starts_final);
    assert(!grow.Next(2));

    // the live queue keeps the entries up to the file that installs
    assert(LiveQueueActive({2, false}, 5) == 2);
    assert(LiveQueueActive({1, true}, 5) == 4);
}

void TestScenarios() {
    const std::string F_OTHER_V2 = "Mario Kart [0100000000020800][v131072].nsp";
    constexpr uint64_t OTHER_UPD = 0x0100000000020800;

    // Newest: v3 and v6 in a queue, in both orders; v6 installs, no page
    {
        std::unordered_map<uint64_t, uint32_t> console;
        Page page;
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_V6}, console, page) == Log{"skip v3", "install v6"}));
        assert(console[GAME] == V6 && page.shown.empty());
        console.clear();
        assert((RunQueue(UpdateChoice_Newest, {F_V6, F_V3}, console, page) == Log{"install v6", "skip v3"}));
        assert(console[GAME] == V6 && page.shown.empty());
    }
    // Ask: nothing waits; both updates go to the end, the next game installs first,
    // the page shows one row for the game with both versions; the user picks v3
    {
        std::unordered_map<uint64_t, uint32_t> console;
        Page page;
        page.choose = [](const ResolveRow&) { return 1; }; // files[0] = v3
        assert((RunQueue(UpdateChoice_Ask, {F_V3, F_V6, F_OTHER}, console, page) ==
                Log{"defer v3", "defer v6", "install other", "install v3", "skip v6"}));
        assert(page.shown.size() == 1);
        const auto& row = page.shown[0];
        assert(row.kind == PendingKind::Several && row.game == "v3" && !row.console);
        assert((row.versions == std::vector<uint32_t>{V3, V6}));
        assert(console[GAME] == V3);
    }
    // the only update, older than the console: the queue does not wait, the page asks
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V6}};
        Page keep;
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_OTHER}, console, keep) ==
                Log{"defer v3", "install other", "skip v3"}));
        assert(console[GAME] == V6);
        assert(keep.shown.size() == 1 && keep.shown[0].kind == PendingKind::Older && keep.shown[0].console == V6);
        Page install;
        install.choose = [](const ResolveRow&) { return 1; };
        assert((RunQueue(UpdateChoice_Newest, {F_V3}, console, install) == Log{"defer v3", "install v3"}));
        assert(console[GAME] == V3);
    }
    // two games with questions: two rows, each chosen on its own
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V6}, {OTHER_UPD, 3 << 16}};
        Page page;
        page.choose = [](const ResolveRow& r) { return r.game == "v3" ? 0 : 1; };
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_OTHER_V2}, console, page) ==
                Log{"defer v3", "defer Mario", "skip v3", "install Mario"}));
        assert(page.shown.size() == 2 && page.shown[1].game == "Mario Kart");
        assert(console[GAME] == V6 && console[OTHER_UPD] == 2u << 16);
    }
    // the same version is on the console: "Reinstall?" goes to the page too
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V6}};
        Page page;
        page.choose = [](const ResolveRow&) { return 1; };
        assert((RunQueue(UpdateChoice_Newest, {F_V6, F_OTHER}, console, page) ==
                Log{"defer v6", "install other", "install v6"}));
        assert(page.shown.size() == 1 && page.shown[0].kind == PendingKind::Reinstall);
    }
    // Ask, the console has v9: the user picks v6 on the page; no second question about the older update
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V9}};
        Page page;
        page.choose = [](const ResolveRow&) { return 2; }; // files[1] = v6
        assert((RunQueue(UpdateChoice_Ask, {F_V3, F_V6}, console, page) ==
                Log{"defer v3", "defer v6", "skip v3", "install v6"}));
        assert(page.shown.size() == 1 && page.shown[0].console == V9);
        assert(console[GAME] == V6);
    }
    // "Skip all" (or a cancelled session): every choice 0, the console stays as it was
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V6}};
        Page page;
        assert((RunQueue(UpdateChoice_Ask, {F_V3, F_OTHER_V2, F_V6}, console, page) ==
                Log{"defer v3", "install Mario", "defer v6", "skip v3", "skip v6"}));
        assert(console[GAME] == V6);
    }
    // Newest, both in the queue are older than the console: nothing installs, no page
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V9}};
        Page page;
        assert((RunQueue(UpdateChoice_Newest, {F_V3, F_V6}, console, page) == Log{"skip v3", "skip v6"}));
        assert(console[GAME] == V9 && page.shown.empty());
    }
    // the DBI Backend sends one update of the game: it installs as sent
    {
        std::unordered_map<uint64_t, uint32_t> console{{GAME, V3}};
        Page page;
        assert((RunQueue(UpdateChoice_Newest, {F_V6}, console, page) == Log{"install v6"}));
    }
}

void TestResolveRows() {
    // the same file noted twice (two cnmt in one NSP) is one choice
    std::vector<PendingInstall> p{
        {4, PendingKind::Older, GAME, V3, V6, "Zelda"},
        {4, PendingKind::Older, GAME, V3, V6, "Zelda"},
        {7, PendingKind::Reinstall, 0x0100000000010000, 0, 0, "Other"},
    };
    auto rows = BuildResolveRows(p);
    assert(rows.size() == 2 && rows[0].files.size() == 1 && rows[0].kind == PendingKind::Older);
    rows[0].choice = 1;
    const auto a = ResolveAnswers(rows);
    assert(a.at(4) && !a.at(7));

    assert(GameName("Zelda TotK [0100ABF008968800][v196608].nsp") == "Zelda TotK");
    assert(GameName("Zelda_[0100ABF008968800].nsz") == "Zelda");
    assert(GameName("plain.nsp") == "plain");
    assert(GameName("[0100ABF008968800].nsp") == "[0100ABF008968800].nsp");
}

} // namespace

int main() {
    TestDecisions();
    TestAskTimeout();
    TestDeferQueue();
    TestScenarios();
    TestResolveRows();
    std::puts("test_update_choice: ok");
    return 0;
}
