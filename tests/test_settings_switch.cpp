// Host test for sphaira/include/ui/menus/settings/settings_switch.hpp

#include "ui/menus/settings/settings_switch.hpp"

#include <cassert>

using namespace sphaira::ui::menu::settings;

int main() {
    assert(TriFromBool(true, false) == Tri::On);
    assert(TriFromBool(false, false) == Tri::Off);
    assert(TriFromBool(true, true) == Tri::Waiting && TriFromBool(false, true) == Tri::Waiting);
    assert(KnobTarget(Tri::Off) == 0.f && KnobTarget(Tri::Waiting) == 0.5f && KnobTarget(Tri::On) == 1.f);
    float k = 0.f;
    for (int i = 0; i < 20; ++i) { k = KnobStep(k, 1.f); assert(k <= 1.f); }
    assert(k == 1.f);
    for (int i = 0; i < 20; ++i) { k = KnobStep(k, 0.5f); assert(k >= 0.5f); }
    assert(k == 0.5f);
    return 0;
}
