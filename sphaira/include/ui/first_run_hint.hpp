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

// Dims the screen and shows a text box. With a target footer button, the button
// is drawn above the dim, framed, and the box sits above it. Closes with A or B.
struct HintBox final : Widget {
    HintBox(std::string text, const uiButton* target);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    // no chrome: the header and footer of the page below are covered by the dim.
    auto WantsChrome() const -> bool override {
        return false;
    }

private:
    std::string m_text;
    std::unique_ptr<uiButton> m_target;
    Vec4 m_frame{};
};

} // namespace sphaira::ui
