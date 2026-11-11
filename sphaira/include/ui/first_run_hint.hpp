#pragma once

#include "ui/widget.hpp"

#include <memory>
#include <string>

namespace sphaira::ui {

// The three one-time hints. Each one has its own flag in the config file.
enum class FirstRunHint {
    Main,
    Tools,
    KefirSettings,
};

namespace hint {

// true the first time it is asked for `which`; the flag is stored, so later calls return false.
auto TakeFirst(FirstRunHint which) -> bool;
// clears all flags, so every hint shows again.
void ResetAll();

} // namespace hint

// Dims the screen and shows a text box. With the footer row of the page below, the row is drawn above the dim, its
// `target` button framed, and the box sits above that button. Closes with A or B.
struct HintBox final : Widget {
    HintBox(std::string text, uiButtons row = {}, Button target = Button::NONE);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    // no chrome: the header and footer of the page below are covered by the dim.
    auto WantsChrome() const -> bool override {
        return false;
    }

private:
    std::string m_text;
    uiButtons m_row; // the legend of the page below: this box has no chrome, so it draws the row itself
    Vec4 m_frame{};  // around the target button; w == 0 = no target
};

} // namespace sphaira::ui
