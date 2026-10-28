#include <ui/menus/dbi/package_skip_gate.hpp>

#include <cassert>

using sphaira::ui::menu::dbi::PackageSkipGate;

int main() {
    PackageSkipGate gate;

    gate.Open(4);
    assert(!gate.Accept(3)); // A delayed click cannot cancel the next package.
    assert(gate.Accept(4));
    assert(!gate.Accept(4)); // One cancel signal per package.
    assert(gate.Close());
    assert(!gate.Accept(4)); // A late click cannot leave a USB cancel pending.

    gate.Open(5);
    assert(!gate.Close()); // A normal package completion is not a skip.
    assert(!gate.Accept(5));
}
