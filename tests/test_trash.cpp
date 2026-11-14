// Trash and action journal (trash.hpp) on a temp folder that stands for the SD card.
// LINK: sphaira/source/trash.cpp
#include "trash.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace sphaira::trash;

namespace {

auto Exists(const std::string& p) -> bool {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

void Put(const std::string& p, const std::string& text) {
    std::string dir = p.substr(0, p.rfind('/'));
    std::system(("mkdir -p '" + dir + "'").c_str());
    std::ofstream(p) << text;
}

auto Read(const std::string& p) -> std::string {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

} // namespace

int main() {
    char tmpl[] = "/tmp/trash_test_XXXXXX";
    const std::string sd = mkdtemp(tmpl);
    const std::time_t now = 1'800'000'000;

    // a backup file and a profile pack folder go into one item
    Put(sd + "/dumps/Zelda/save.zip", "zip");
    Put(sd + "/config/kefir/account_backups/Bob/user.bin", "bob");
    const auto item = NewItem(sd, "user", "Bob\nline", now);
    assert(item.starts_with(std::string{DIR} + "/"));
    assert(Move(sd, item, "/dumps/Zelda/save.zip"));
    assert(Move(sd, item, "/config/kefir/account_backups/Bob"));
    assert(!Exists(sd + "/dumps/Zelda/save.zip"));
    assert(Exists(sd + InItem(item, "/dumps/Zelda/save.zip")));
    assert(!Move(sd, item, "/dumps/missing.zip"));

    // a second item in the same second gets its own folder
    const auto item2 = NewItem(sd, "backup", "Mario", now + 100);
    assert(!item2.empty() && item2 != item);

    auto items = List(sd);
    assert(items.size() == 2);
    assert(items[0].label == "Mario"); // newest first
    assert(items[1].kind == "user" && items[1].label == "Bob line" && items[1].when == now);
    assert(items[1].bytes == 6);

    // restore: the file whose place is taken stays in the trash
    Put(sd + "/dumps/Zelda/save.zip", "new");
    std::size_t kept = 0;
    assert(Restore(sd, items[1].id, &kept) == 1);
    assert(kept == 1);
    assert(Read(sd + "/config/kefir/account_backups/Bob/user.bin") == "bob");
    assert(Read(sd + "/dumps/Zelda/save.zip") == "new");
    assert(List(sd).size() == 2);
    // the place is free again: all back, the item is gone
    std::remove((sd + "/dumps/Zelda/save.zip").c_str());
    assert(Restore(sd, items[1].id, &kept) == 1 && kept == 0);
    assert(Read(sd + "/dumps/Zelda/save.zip") == "zip");
    assert(List(sd).size() == 1);

    // ids that leave the trash folder do nothing
    assert(!Remove(sd, "../dumps"));
    assert(!Remove(sd, ".."));
    assert(Restore(sd, "../x") == 0);

    // purge: age first, then low space removes the oldest
    const auto old = NewItem(sd, "save", "Old", now - 31 * 24 * 60 * 60);
    assert(!old.empty());
    assert(List(sd).size() == 2);
    assert(Purge(sd, now + 200, nullptr) == 1);
    assert(List(sd).size() == 1);
    int calls = 0;
    assert(Purge(sd, now + 200, [&] { return calls++ < 1; }) == 1);
    assert(List(sd).empty());

    // journal: one line per action, kept between runs
    assert(FormatLine("delete", "a\nb", "", now).find(" | delete | a b | no copy\n") != std::string::npos);
    const auto journal = Read(sd + JOURNAL);
    assert(journal.find("trash: removed (30 days) | Old") != std::string::npos);
    assert(journal.find("trash: removed (low space) | Mario") != std::string::npos);

    std::system(("rm -rf '" + sd + "'").c_str());
    std::puts("test_trash: ok");
    return 0;
}
