#include "ui/menus/filebrowser_path_notes.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <utility>

int main() {
    using namespace sphaira::ui::menu::filebrowser;

    // lookups: root spelled either way, trailing slash, case
    assert(std::strcmp(FindPathNote("/", "atmosphere"), "Atmosphere: the custom firmware") == 0);
    assert(std::strcmp(FindPathNote("", "Atmosphere"), "Atmosphere: the custom firmware") == 0);
    assert(std::strcmp(FindPathNote("/config/kefir/", "themes"), "Custom user interface themes") == 0);
    assert(std::strcmp(FindPathNote("/CONFIG/KEFIR", "LOG.TXT"), "Kefir Hub log of this start") == 0);
    assert(std::strcmp(FindPathNote("/bootloader/ini", "!kefir_updater.ini"), "Boot entry used by the Kefir auto-update") == 0);

    // the same name means different things in different folders
    assert(std::strcmp(FindPathNote("/", "themes"), FindPathNote("/config/kefir", "themes")) != 0);
    assert(std::strcmp(FindPathNote("/", "config"), FindPathNote("/atmosphere", "config")) != 0);

    // unknown
    assert(FindPathNote("/", "Unknown") == nullptr);
    assert(FindPathNote("/config/kefir/themes", "abyss.ini") == nullptr);
    assert(FindPathNote("/switch/DBI", "DBI.nro") == nullptr);

    // table hygiene: unique (dir, name), dirs start with '/', no trailing slash, notes short enough for one line
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& n : PATH_NOTES) {
        assert(n.dir[0] == '/');
        assert(std::strlen(n.dir) == 1 || n.dir[std::strlen(n.dir) - 1] != '/');
        assert(n.name[0] != '\0' && n.note[0] != '\0');
        assert(std::strlen(n.note) <= 72);
        std::string lo_dir = n.dir, lo_name = n.name;
        for (auto& c : lo_dir) c = (char)std::tolower((unsigned char)c);
        for (auto& c : lo_name) c = (char)std::tolower((unsigned char)c);
        assert(seen.insert({lo_dir, lo_name}).second);
    }
    assert(seen.size() > 100);

    std::cout << "test_filebrowser_path_notes passed (" << seen.size() << " notes)\n";
    return 0;
}
