#pragma once

#include "ui/widget.hpp"

namespace sphaira::ui {

// The first start. A whole page in the console's language instead of the main
// menu, which is built underneath but not drawn: what the HOME Menu icon is
// for, then the language list (A). Pops itself once a language is chosen; a
// language other than the page's own restarts Kefir Hub.
struct FirstStart final : Widget {
    FirstStart();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    auto IsModal() const -> bool override { return true; }
    auto BlocksDrawUnder() const -> bool override { return true; }
};

} // namespace sphaira::ui
