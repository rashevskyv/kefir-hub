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

struct Page final : ui::menu::MenuBase {
    using Act = std::function<void(Page&)>;

    // a_label empty: no action on A. on_b empty: B just goes back. The actions get the page, so they can replace it.
    Page(const std::string& title, const std::string& sub, std::vector<Line> lines, const std::string& a_label, Act on_a, Act on_b = {})
        : MenuBase{title, ui::menu::MenuFlag_None}, m_lines{std::move(lines)}, m_on_a{std::move(on_a)}, m_on_b{std::move(on_b)} {
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
            nvgFontSize(vg, size);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgTextBoxBounds(vg, 0, 0, width, s.c_str(), nullptr, b);
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
                    ui::gfx::drawTextBox(vg, x, y, 21.f, W - (x - X), colour, l.text.c_str());
                    y += box_height(l.text, 21.f, W - (x - X)) + 10.f;
                    break;
                }
                case Line::Kind::Check: {
                    const bool ok = i < m_state.size() && m_state[i];
                    ui::gfx::drawTextArgs(vg, X, y, 23.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, ok ? good : warn, "%s%s", ok ? "✓ " : "", (ok ? l.text : l.no).c_str());
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
    }

private:
    std::vector<Line> m_lines;
    Act m_on_a;
    Act m_on_b;
    std::vector<bool> m_state;
    TimeStamp m_poll{};
    bool m_polled{};
};

} // namespace sphaira::battery::calib_ui
