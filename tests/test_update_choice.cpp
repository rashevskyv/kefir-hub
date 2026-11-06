// Which update of a game the install queue keeps.
//   g++ -std=c++20 -I../sphaira/include test_update_choice.cpp -o t && ./t
#include "ui/menus/update_choice.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace sphaira::ui::menu::dbi;

int main() {
    // file names as the queue holds them: base, v3, v6 of one game, and another game's update
    std::vector<NameTitle> q{
        ParseNameTitle("Game [0100ABF008968000][v0].nsp"),
        ParseNameTitle("Game [0100ABF008968800][v196608].nsp"),
        ParseNameTitle("Game [0100ABF008968800][v393216].nsp"),
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

    // Newest (default): v3 is skipped whatever the order, v6 installs
    assert(QueueUpdateStep(UpdateChoice_Newest, v3, false, 0, 196608) == UpdateStep::Skip);
    assert(QueueUpdateStep(UpdateChoice_Newest, v6, false, 0, 393216) == UpdateStep::Install);
    // one update in the queue goes on to yati's console check
    assert(QueueUpdateStep(UpdateChoice_Newest, FindQueueUpdates(q, 3), false, 0, 65536) == UpdateStep::Install);

    // Ask: both are asked until one is picked, then the other is skipped
    assert(QueueUpdateStep(UpdateChoice_Ask, v3, false, 0, 196608) == UpdateStep::Ask);
    assert(QueueUpdateStep(UpdateChoice_Ask, v6, true, 196608, 393216) == UpdateStep::Skip);
    assert(QueueUpdateStep(UpdateChoice_Ask, v3, true, 196608, 196608) == UpdateStep::Install);

    // older than the console: the only update is asked about, in both modes
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 1, false) == UpdateStep::Ask);
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 0, false) == UpdateStep::Ask); // stream
    assert(OlderThanConsoleStep(UpdateChoice_Ask, 1, false) == UpdateStep::Ask);
    // the newest of several, still older than the console: Newest keeps the console's
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 2, false) == UpdateStep::Skip);
    assert(OlderThanConsoleStep(UpdateChoice_Ask, 2, false) == UpdateStep::Ask);
    // the user picked it in the queue: no second question
    assert(OlderThanConsoleStep(UpdateChoice_Newest, 2, true) == UpdateStep::Install);

    std::puts("test_update_choice: ok");
    return 0;
}
