#pragma once

// The pages of the battery calibration, drawn as in the guide (docs/site/en/battery-wizard): a list of lines (headings,
// paragraphs, bullets, a check that follows the charger, label / value rows, boxes) and one action on A.
// Used by battery_calib.cpp.

#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace sphaira::battery::calib_ui {

// A check mark drawn as a path: the Switch font has no glyph for U+2713 (it shows an empty box).
inline void DrawTick(NVGcontext* vg, float x, float y, float size, NVGcolor colour) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + size * 0.08f, y + size * 0.55f);
    nvgLineTo(vg, x + size * 0.38f, y + size * 0.85f);
    nvgLineTo(vg, x + size * 0.92f, y + size * 0.18f);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, size * 0.13f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

// Line breaks for a text box: a word of one or two letters ("у", "в", "до") stays with the word after it, so no line ends
// with a lone preposition. The result holds the breaks as '\n'; draw it with a box a little wider than `width`.
inline auto WrapText(NVGcontext* vg, float size, float width, const std::string& text) -> std::string {
    nvgFontSize(vg, size);
    const auto measure = [vg](const std::string& s) {
        float b[4]{};
        nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, b);
        return b[2] - b[0];
    };
    const auto letters = [](const std::string& w) {
        size_t n = 0;
        for (const unsigned char c : w) {
            n += (c & 0xC0) != 0x80;
        }
        return n;
    };
    std::string out;
    size_t pos = 0;
    while (pos <= text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::vector<std::string> words;
        for (size_t i = pos; i < end;) {
            auto sp = text.find(' ', i);
            if (sp == std::string::npos || sp > end) {
                sp = end;
            }
            if (sp > i) {
                words.push_back(text.substr(i, sp - i));
            }
            i = sp + 1;
        }
        std::string line;
        for (size_t i = 0; i < words.size(); i++) {
            auto unit = words[i];
            while (letters(words[i]) <= 2 && i + 1 < words.size()) {
                unit += " " + words[++i];
            }
            const auto cand = line.empty() ? unit : line + " " + unit;
            if (!line.empty() && measure(cand) > width) {
                out += line + "\n";
                line = unit;
            } else {
                line = cand;
            }
        }
        out += line;
        if (end < text.size()) {
            out += "\n";
        }
        pos = end + 1;
    }
    return out;
}

// text box with the breaks of WrapText
inline void DrawWrapped(NVGcontext* vg, float x, float y, float size, float width, NVGcolor colour, const std::string& text) {
    ui::gfx::drawTextBox(vg, x, y, size, width + 40.f, colour, WrapText(vg, size, width, text).c_str());
}

struct Line {
    enum class Kind { Head, Para, Item, Warn, Ok, Dim, Check, Row, Boxes };
    Kind kind{Kind::Para};
    std::string text;                    // the text, or the label of a Row
    std::string value;                   // Row: the value on the right
    std::string no;                      // Check: the text while not done
    std::function<bool()> done;          // Check: the state (read about once a second by the page)
    std::vector<std::pair<std::string, std::string>> boxes; // Boxes: label and value of each
};

inline auto Head(std::string t) -> Line { Line l; l.kind = Line::Kind::Head; l.text = std::move(t); return l; }
inline auto Para(std::string t) -> Line { Line l; l.kind = Line::Kind::Para; l.text = std::move(t); return l; }
inline auto Item(std::string t) -> Line { Line l; l.kind = Line::Kind::Item; l.text = std::move(t); return l; }
inline auto Warn(std::string t) -> Line { Line l; l.kind = Line::Kind::Warn; l.text = std::move(t); return l; }
inline auto Ok(std::string t) -> Line { Line l; l.kind = Line::Kind::Ok; l.text = std::move(t); return l; }
inline auto Dim(std::string t) -> Line { Line l; l.kind = Line::Kind::Dim; l.text = std::move(t); return l; }
inline auto Check(std::string yes, std::string no, std::function<bool()> done) -> Line {
    Line l; l.kind = Line::Kind::Check; l.text = std::move(yes); l.no = std::move(no); l.done = std::move(done); return l;
}
inline auto Row(std::string label, std::string value) -> Line { Line l; l.kind = Line::Kind::Row; l.text = std::move(label); l.value = std::move(value); return l; }
inline auto Boxes(std::vector<std::pair<std::string, std::string>> b) -> Line { Line l; l.kind = Line::Kind::Boxes; l.boxes = std::move(b); return l; }

// The road map at the foot of every calibration page: the six steps as dots with their names, the ones done filled, the
// current one ringed, so the user always sees where he is. `current` is 1-based (StepIndex); `done_upto` steps are filled.
inline void DrawRoadmap(NVGcontext* vg, Theme* theme, int current, int done_upto, int count) {
    const auto text = theme->GetColour(ThemeEntryID_TEXT);
    const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto good = nvgRGB(0x4C, 0xAF, 0x50);
    const std::string names[] = {"Reset"_i18n, "Cycle 1"_i18n, "Cycle 2"_i18n, "Report"_i18n, "Transfer"_i18n};
    const float y = 603.f, x0 = 150.f, x1 = 1130.f;
    const float step = (x1 - x0) / static_cast<float>(std::max(1, count - 1));
    nvgBeginPath(vg);
    nvgMoveTo(vg, x0, y);
    nvgLineTo(vg, x1, y);
    nvgStrokeColor(vg, nvgTransRGBAf(info, 0.5f));
    nvgStrokeWidth(vg, 3.f);
    nvgStroke(vg);
    for (int i = 1; i <= count; i++) {
        const float x = x0 + static_cast<float>(i - 1) * step;
        const bool done = i <= done_upto;
        const bool now = i == current;
        nvgBeginPath(vg);
        nvgCircle(vg, x, y, now ? 11.f : 8.f);
        nvgFillColor(vg, done ? good : now ? text : nvgTransRGBAf(info, 0.35f));
        nvgFill(vg);
        if (now) {
            nvgBeginPath(vg);
            nvgCircle(vg, x, y, 16.f);
            nvgStrokeColor(vg, text);
            nvgStrokeWidth(vg, 2.f);
            nvgStroke(vg);
        }
        if (done) {
            DrawTick(vg, x - 6.f, y - 6.f, 12.f, nvgRGB(0x10, 0x20, 0x10));
        }
        ui::gfx::drawTextArgs(vg, x, y + 22.f, 15.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, now ? text : info, "%s", i <= 5 ? names[i - 1].c_str() : "");
    }
}

struct Page final : ui::menu::MenuBase {
    using Act = std::function<void(Page&)>;

    // a_label empty: no action on A. on_b empty: B just goes back. The actions get the page, so they can replace it.
    // waiting: true while the page waits for the user (the charger still in, or still out); when it turns false the page
    // does what A does, so the next step starts by itself.
    // road_step: the 1-based step on the road map at the foot (0 = none, the intro pages)
    Page(const std::string& title, const std::string& sub, std::vector<Line> lines, const std::string& a_label, Act on_a, Act on_b = {},
         std::function<bool()> waiting = {}, int road_step = 0)
        : MenuBase{title, ui::menu::MenuFlag_None}, m_lines{std::move(lines)}, m_on_a{std::move(on_a)}, m_on_b{std::move(on_b)},
          m_waiting{std::move(waiting)}, m_road_step{road_step} {
        SetAction(Button::B, Action{"Back"_i18n, [this]() {
            if (m_on_b) {
                m_on_b(*this);
            } else {
                SetPop();
            }
        }});
        if (!a_label.empty()) {
            SetAction(Button::A, Action{a_label, [this]() {
                if (m_on_a) {
                    m_on_a(*this);
                }
            }});
        }
        SetTitleSubHeading(sub, true);
    }

    auto GetShortTitle() const -> const char* override { return "Battery calibration"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        // the checks follow the charger: look about once a second
        if (m_poll.GetSeconds() >= 1 || !m_polled) {
            m_poll.Update();
            m_polled = true;
            m_state.resize(m_lines.size());
            for (size_t i = 0; i < m_lines.size(); i++) {
                if (m_lines[i].done) {
                    m_state[i] = m_lines[i].done();
                }
            }
            if (m_waiting) {
                const bool waiting = m_waiting();
                const bool fire = m_was_waiting && !waiting;
                m_was_waiting = waiting;
                if (fire && m_on_a) {
                    m_on_a(*this);
                    return;
                }
            }
        }
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto good = nvgRGB(0x4C, 0xAF, 0x50);
        const auto warn = nvgRGB(0xF5, 0x9E, 0x0B);
        constexpr float X = 90.f, W = 1100.f;
        float y = 118.f;
        const auto box_height = [&](const std::string& s, float size, float width) {
            float b[4]{};
            const auto wrapped = WrapText(vg, size, width, s);
            nvgFontSize(vg, size);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgTextBoxBounds(vg, 0, 0, width + 40.f, wrapped.c_str(), nullptr, b);
            return b[3] - b[1];
        };
        for (size_t i = 0; i < m_lines.size(); i++) {
            const auto& l = m_lines[i];
            switch (l.kind) {
                case Line::Kind::Head:
                    ui::gfx::drawTextArgs(vg, X, y + 4.f, 21.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", l.text.c_str());
                    y += 32.f;
                    break;
                case Line::Kind::Para:
                case Line::Kind::Item:
                case Line::Kind::Warn:
                case Line::Kind::Ok:
                case Line::Kind::Dim: {
                    const bool bullet = l.kind != Line::Kind::Para;
                    const auto colour = l.kind == Line::Kind::Warn ? warn : l.kind == Line::Kind::Ok ? good : l.kind == Line::Kind::Dim ? info : text;
                    const float x = bullet ? X + 22.f : X;
                    if (bullet) {
                        ui::gfx::drawTextArgs(vg, X, y, 21.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", "•");
                    }
                    DrawWrapped(vg, x, y, 21.f, W - (x - X), colour, l.text);
                    y += box_height(l.text, 21.f, W - (x - X)) + 10.f;
                    break;
                }
                case Line::Kind::Check: {
                    const bool ok = i < m_state.size() && m_state[i];
                    if (ok) {
                        DrawTick(vg, X, y + 2.f, 22.f, good);
                    }
                    ui::gfx::drawTextArgs(vg, ok ? X + 32.f : X, y, 23.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, ok ? good : warn, "%s", (ok ? l.text : l.no).c_str());
                    y += 38.f;
                    break;
                }
                case Line::Kind::Row:
                    ui::gfx::drawTextArgs(vg, X, y, 21.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", l.text.c_str());
                    ui::gfx::drawTextArgs(vg, X + W, y, 21.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, text, "%s", l.value.c_str());
                    y += 34.f;
                    break;
                case Line::Kind::Boxes: {
                    const float n = static_cast<float>(std::max<size_t>(1, l.boxes.size()));
                    const float gap = 16.f, w = (W - gap * (n - 1.f)) / n;
                    for (size_t k = 0; k < l.boxes.size(); k++) {
                        const float x = X + static_cast<float>(k) * (w + gap);
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, x, y, w, 78.f, 10.f);
                        nvgFillColor(vg, nvgTransRGBAf(info, 0.12f));
                        nvgFill(vg);
                        ui::gfx::drawTextArgs(vg, x + 16.f, y + 10.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", l.boxes[k].first.c_str());
                        ui::gfx::drawTextArgs(vg, x + 16.f, y + 36.f, 30.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "%s", l.boxes[k].second.c_str());
                    }
                    y += 94.f;
                    break;
                }
            }
        }
        if (m_road_step > 0) {
            DrawRoadmap(vg, theme, m_road_step, m_road_step - 1, 5);
        }
    }

private:
    std::vector<Line> m_lines;
    Act m_on_a;
    Act m_on_b;
    std::vector<bool> m_state;
    TimeStamp m_poll{};
    bool m_polled{};
    std::function<bool()> m_waiting;
    bool m_was_waiting{};
    int m_road_step{};
};

} // namespace sphaira::battery::calib_ui
