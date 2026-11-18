#include "ui/first_run_hint.hpp"

#include "app.hpp"
#include "option.hpp"
#include "ui/nvg_util.hpp"
#include "ui/updater_guide_timeline.hpp"

#include <algorithm>

namespace sphaira::ui {
namespace {

constexpr float BOX_W = 560.f;
constexpr float BOX_PAD = 30.f;
constexpr float BOX_TEXT_SIZE = 22.f;
constexpr float BOX_LINE_HEIGHT = 1.4f;
constexpr float BOX_EDGE = 40.f;

// ponytail: one stored flag per hint, read and written lazily through option::OptionBool.
option::OptionBool g_flags[] = {
    {"hints", "main_shown", false},
    {"hints", "tools_shown", false},
    {"hints", "kefir_settings_shown", false},
};

} // namespace

namespace hint {

auto TakeFirst(FirstRunHint which) -> bool {
    auto& flag = g_flags[static_cast<size_t>(which)];
    if (flag.Get()) {
        return false;
    }
    flag.Set(true);
    return true;
}

void ResetAll() {
    for (auto& flag : g_flags) {
        flag.Set(false);
    }
}

} // namespace hint

HintBox::HintBox(std::vector<HintStep> steps) : m_steps{std::move(steps)} {
    if (m_steps.empty()) {
        m_steps.emplace_back();
    }
}

auto FooterButtonFrame(const Widget::uiButtons& row, Button target) -> Vec4 {
    for (const auto& b : row) {
        if (b.m_button == target) {
            // The frame goes around what is drawn: the glyph and its label are right- and top-aligned at their positions.
            // (The button's own box is its touch area: wider, and it ran past the screen's edge, owner 2026-11-11.)
            const float left = b.m_button_pos.x - b.m_button_pos.w;
            const float top = std::min(b.m_button_pos.y, b.m_hint_pos.y);
            const float bottom = std::max(b.m_button_pos.y + b.m_button_size, b.m_hint_pos.y + b.m_hint_size);
            return Vec4{left - 12.f, top - 8.f, b.m_hint_pos.x - left + 24.f, bottom - top + 20.f};
        }
    }
    return {};
}

HintBox::HintBox(std::string text, uiButtons row, Button target) : m_steps{{std::move(text)}}, m_row{std::move(row)} {
    m_steps[0].frame = FooterButtonFrame(m_row, target);
}

UpdaterGuide::UpdaterGuide(Widget* below, Vec4 r_frame, std::function<Vec4()> open_tools, std::function<void()> done)
    : m_below{below}, m_r_frame{r_frame}, m_open_tools{std::move(open_tools)}, m_done{std::move(done)} {
}

void UpdaterGuide::Finish() {
    if (m_frame < updater_guide::SWITCH_AT) {
        m_open_tools(); // skipped before the flip: still end on Tools, where the updater lives
    }
    m_frame = updater_guide::END_AT;
    SetPop();
    m_done();
}

void UpdaterGuide::Update(Controller* controller, TouchInfo*) {
    using namespace updater_guide;
    if (ShouldPop()) {
        return;
    }
    if (controller && (controller->GotDown(Button::A) || controller->GotDown(Button::B))) {
        Finish();
        return;
    }
    m_frame++;
    if (m_frame == PRESS_AT || m_frame == TAP_AT) {
        App::PlaySoundEffect(SoundEffect_Focus);
    }
    if (m_frame == SWITCH_AT) {
        m_tile = m_open_tools();
    }
    if (PhaseAt(m_frame) == Phase::Done) {
        Finish();
    }
}

void UpdaterGuide::Draw(NVGcontext* vg, Theme* theme) {
    using namespace updater_guide;
    const auto phase = PhaseAt(m_frame);
    // the spotlight of HintBox: R first, the Updater tile last, nothing while the page slides
    const Vec4 spot = phase == Phase::PressR ? m_r_frame : phase == Phase::Pulse ? m_tile : Vec4{};
    // the dim fades in over the first frames, so the press does not start with a flash of black
    const float fade = std::min(1.f, m_frame / 10.f);
    nvgBeginPath(vg);
    nvgRect(vg, 0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (spot.w > 0.f) {
        nvgRoundedRect(vg, spot.x, spot.y, spot.w, spot.h, 8.f);
        nvgPathWinding(vg, NVG_HOLE);
    }
    nvgFillColor(vg, nvgRGBA(0, 0, 0, u8(150 * fade)));
    nvgFill(vg);
    if (spot.w <= 0.f) {
        return;
    }

    auto hl = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
    // pressed: the frame shrinks a little and the inside lights up, like a key going down
    const float depth = phase == Phase::PressR ? PressDepth(m_frame, PRESS_AT) : PressDepth(m_frame, TAP_AT);
    const float in = 4.f * depth;
    const Vec4 r{spot.x + in, spot.y + in, spot.w - in * 2.f, spot.h - in * 2.f};
    if (depth > 0.f) {
        auto fill = hl;
        fill.a = 0.35f * depth;
        gfx::drawRect(vg, r, fill, 8.f);
    }
    nvgBeginPath(vg);
    nvgRoundedRect(vg, r.x, r.y, r.w, r.h, 8.f);
    nvgStrokeWidth(vg, 4.f);
    hl.a = fade;
    nvgStrokeColor(vg, hl);
    nvgStroke(vg);

    // the pulse: a ring grows out of the tile and fades
    if (const float t = PulseT(m_frame); phase == Phase::Pulse && t > 0.f) {
        const float grow = 26.f * t;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, spot.x - grow, spot.y - grow, spot.w + grow * 2.f, spot.h + grow * 2.f, 8.f + grow);
        nvgStrokeWidth(vg, 4.f * (1.f - t) + 1.f);
        hl.a = 1.f - t;
        nvgStrokeColor(vg, hl);
        nvgStroke(vg);
    }
}

void HintBox::Update(Controller* controller, TouchInfo* touch) {
    if (!controller) {
        return;
    }
    if (controller->GotDown(Button::A) && m_step + 1 < m_steps.size()) {
        m_step++;
    } else if (controller->GotDown(Button::A) || controller->GotDown(Button::B)) {
        SetPop();
    }
}

void HintBox::Draw(NVGcontext* vg, Theme* theme) {
    const auto& m_text = m_steps[m_step].text;
    const auto& m_frame = m_steps[m_step].frame;
    // A spotlight: the dim has a hole over the target, so the target stays bright, and the frame is a stroke only
    // (drawRectOutline fills its inside and hid the tile it was to point at, owner 2026-11-12).
    nvgBeginPath(vg);
    nvgRect(vg, 0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (m_frame.w > 0.f) {
        nvgRoundedRect(vg, m_frame.x, m_frame.y, m_frame.w, m_frame.h, 8.f);
        nvgPathWinding(vg, NVG_HOLE);
    }
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
    nvgFill(vg);

    if (m_frame.w > 0.f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, m_frame.x, m_frame.y, m_frame.w, m_frame.h, 8.f);
        nvgStrokeWidth(vg, 4.f);
        nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_HIGHLIGHT_1));
        nvgStroke(vg);
    }
    for (auto& b : m_row) { // the whole legend, so the screen below does not lose its footer
        b.Draw(vg, theme);
    }

    // measure the text, then place the box above the target or in the centre.
    nvgSave(vg);
    nvgFontSize(vg, BOX_TEXT_SIZE);
    nvgTextLineHeight(vg, BOX_LINE_HEIGHT);
    float bounds[4];
    nvgTextBoxBounds(vg, 0.f, 0.f, BOX_W - BOX_PAD * 2.f, m_text.c_str(), nullptr, bounds);
    nvgRestore(vg);
    const float text_h = bounds[3] - bounds[1];
    const float box_h = BOX_PAD + text_h + BOX_PAD * 2.f;

    Vec4 box{(SCREEN_WIDTH - BOX_W) / 2.f, (SCREEN_HEIGHT - box_h) / 2.f, BOX_W, box_h};
    if (m_frame.w > 0.f) {
        const float right = m_frame.x + m_frame.w;
        box.x = std::clamp(right - BOX_W, BOX_EDGE, SCREEN_WIDTH - BOX_EDGE - BOX_W);
        box.y = m_frame.y - 24.f - box_h;
        if (box.y < BOX_EDGE) { // no room above the target (a tile in the top row): below it
            box.y = m_frame.y + m_frame.h + 24.f;
        }
    }

    gfx::drawRect(vg, box, theme->GetColour(ThemeEntryID_POPUP), 5);
    gfx::drawTextBox(
        vg, box.x + BOX_PAD, box.y + BOX_PAD, BOX_TEXT_SIZE, BOX_W - BOX_PAD * 2.f,
        theme->GetColour(ThemeEntryID_TEXT), m_text.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, BOX_LINE_HEIGHT
    );
    gfx::drawTextBox(
        vg, box.x + BOX_PAD, box.y + box_h - BOX_PAD - 20.f, 18.f, BOX_W - BOX_PAD * 2.f,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        (m_step + 1 < m_steps.size() ? "A: next · B: close"_i18n : "Press A or B to close."_i18n).c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP
    );
}

} // namespace sphaira::ui
