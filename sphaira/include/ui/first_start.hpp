#pragma once

#include "ui/widget.hpp"
#include <string>

namespace sphaira::ui {

// The first start. A whole page instead of the main menu, which is built
// underneath but not drawn. The language list opens at once; the page that
// says what the HOME Menu icon is for comes after it, in the chosen language
// (A = OK). A language other than the one the start began in rebuilds the main
// menu in it; there is no restart.
// True from the first-start page opening until it closes. Nothing may reach for the
// USB port (PC Install, MTP offers) before the user has finished the first set-up.
auto FirstStartActive() -> bool;

struct FirstStart final : Widget {
    FirstStart();
    ~FirstStart() override;

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    auto IsModal() const -> bool override { return true; }
    auto BlocksDrawUnder() const -> bool override { return true; }

private:
    bool m_list_opened{false};
    bool m_page_shown{false};
    std::string m_start_language{};
};

} // namespace sphaira::ui
