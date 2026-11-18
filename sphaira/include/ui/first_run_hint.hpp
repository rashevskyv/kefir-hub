#pragma once

#include "ui/widget.hpp"

#include <functional>
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

// the frame around one button of a footer row (its glyph and label); w == 0 when the row has no such button
auto FooterButtonFrame(const Widget::uiButtons& row, Button target) -> Vec4;

// A on the old Kefir Updater tile: shows where the updater is now (updater_guide_timeline.hpp). R is framed and
// pressed, `open_tools` flips the page and returns the Updater tile, the tile pulses, then `done` runs.
// A or B skips to `done`. The page below keeps its header and footer.
struct UpdaterGuide final : Widget {
    UpdaterGuide(Widget* below, Vec4 r_frame, std::function<Vec4()> open_tools, std::function<void()> done);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    // drawn after the chrome, so the dim covers the footer and the spotlight shows R through it
    auto IsModal() const -> bool override { return true; }
    // the footer is the one of the page below (it changes when the page flips)
    auto GetFooterOwner() -> Widget* override { return m_below; }

private:
    void Finish();

    Widget* m_below;
    Vec4 m_r_frame;
    Vec4 m_tile{};
    std::function<Vec4()> m_open_tools;
    std::function<void()> m_done;
    int m_frame{};
};

} // namespace sphaira::ui
