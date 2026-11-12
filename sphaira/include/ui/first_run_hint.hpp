#pragma once

#include "ui/widget.hpp"

#include <memory>
#include <string>
#include <vector>

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

// One stop of a tour: the text and the screen area it frames (w == 0 = nothing framed).
struct HintStep {
    std::string text;
    Vec4 frame{};
};

// Dims the screen and shows a text box. With the footer row of the page below, the row is drawn above the dim, its
// `target` button framed, and the box sits above that button. Closes with A or B.
// With steps it is a tour: A goes to the next step (the last one closes), B closes at once.
struct HintBox final : Widget {
    HintBox(std::string text, uiButtons row = {}, Button target = Button::NONE);
    explicit HintBox(std::vector<HintStep> steps);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    // no chrome: the header and footer of the page below are covered by the dim.
    auto WantsChrome() const -> bool override {
        return false;
    }

private:
    std::vector<HintStep> m_steps; // m_steps[m_step].frame: around the target; w == 0 = no target
    size_t m_step{};
    uiButtons m_row; // the legend of the page below: this box has no chrome, so it draws the row itself
};

} // namespace sphaira::ui
