// Backup user -> console user for a restore without a question.
//   g++ -std=c++20 -I../sphaira/include test_save_user_match.cpp -o t && ./t
#include "ui/menus/save/save_user_match.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::ui::menu::save;

int main() {
    const std::vector<ConsoleUser> users{
        {{0x11, 0x12}, "Vlad"},
        {{0x21, 0x22}, "Kid"},
        {{0x31, 0x32}, "Guest"},
    };

    // the same uid wins, whatever the name says
    assert(MatchBackupUser({0x21, 0x22}, "Vlad", users) == 1u);

    // a new uid (other console, users made again): the name decides
    assert(MatchBackupUser({0x99, 0x98}, "Guest", users) == 2u);

    // no uid in the backup (folder backups): the name decides
    assert(MatchBackupUser({0, 0}, "Kid", users) == 1u);

    // unknown name, or no name: ask
    assert(!MatchBackupUser({0x99, 0x98}, "Mom", users));
    assert(!MatchBackupUser({0x99, 0x98}, "", users));
    assert(!MatchBackupUser({0, 0}, "", users));

    // two users with one name: ask
    const std::vector<ConsoleUser> twins{{{1, 1}, "Kid"}, {{2, 2}, "Kid"}};
    assert(!MatchBackupUser({0x99, 0x98}, "Kid", twins));
    // ...unless the uid says which one
    assert(MatchBackupUser({2, 2}, "Kid", twins) == 1u);

    // no users on the console
    assert(!MatchBackupUser({0x11, 0x12}, "Vlad", {}));

    std::puts("test_save_user_match: ok");
    return 0;
}
