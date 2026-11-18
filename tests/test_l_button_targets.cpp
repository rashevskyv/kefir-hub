// Host test for sphaira/include/l_button_targets.hpp: ids round-trip, and every label key is in en.json
// (a key that is not there never matches a live row, so the target would silently vanish from the picker).

#include "l_button_targets.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main() {
    using namespace sphaira::l_button;

    std::ifstream f("assets/romfs/i18n/en.json");
    assert(f && "run from the repo root");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string en = ss.str();

    for (const auto& t : TARGETS) {
        assert(Find(MakeId(t)) == &t); // also: no two targets share an id
        const std::string quoted = "\"" + std::string{t.key} + "\":";
        if (en.find(quoted) == std::string::npos) {
            std::printf("FAIL l_button: \"%.*s\" is not an en.json key\n", int(t.key.size()), t.key.data());
            return 1;
        }
        if (!t.sub.empty()) {
            bool has_rows = false;
            for (const auto& c : TARGETS) {
                has_rows |= c.group == t.sub;
            }
            assert(has_rows);
        }
    }

    assert(Find("") == nullptr);
    assert(Find("game/Nope") == nullptr);
    assert(Find("game") == nullptr);
    assert(Find("gameXSaves") == nullptr);
    assert(Find("tile/Saves") == nullptr); // Saves lives in Game Tools, not on the tiles

    std::printf("ok  l_button_targets: all checks passed\n");
    return 0;
}
