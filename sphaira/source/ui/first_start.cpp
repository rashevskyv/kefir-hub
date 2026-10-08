#include "ui/first_start.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"

namespace sphaira::ui {

FirstStart::FirstStart() {
    m_pos = Vec4{0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT};

    SetActions(
        std::make_pair(Button::A, Action{"Choose language"_i18n, [](){
            App::ShowInitialLanguageSelection();
        }})
    );
}

void FirstStart::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
    // the language dialog marks the choice; nothing else is left to do here.
    if (!App::NeedsLanguageSelection()) {
        SetPop();
    }
}

void FirstStart::Draw(NVGcontext* vg, Theme* theme) {
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_BACKGROUND));

    const float x = 120.f;
    const float w = SCREEN_WIDTH - 2.f * x;
    float y = 90.f;

    gfx::drawText(vg, x, y, 44.f, theme->GetColour(ThemeEntryID_TEXT), "Kefir Hub");
    gfx::drawTextArgs(vg, x + w, y + 14.f, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "v%s", APP_VERSION);
    y += 70.f;
    gfx::drawRect(vg, x, y, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    y += 36.f;

    gfx::drawText(vg, x, y, 30.f, theme->GetColour(ThemeEntryID_TEXT), "First start"_i18n.c_str());
    y += 60.f;

    const auto para = [&](const std::string& text) {
        float bounds[4];
        nvgFontSize(vg, 22.f);
        nvgTextLineHeight(vg, 1.35f);
        nvgTextBoxBounds(vg, x, y, w, text.c_str(), nullptr, bounds);
        gfx::drawTextBox(vg, x, y, 22.f, w, theme->GetColour(ThemeEntryID_TEXT), text.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.35f);
        y += (bounds[3] - bounds[1]) + 28.f;
    };

    para("Kefir Hub adds its own icon to the HOME Menu. From now on start Kefir Hub from that icon: it then gets all the memory and the USB port. The Homebrew Menu and the Album give it only a part."_i18n);
    para("Choose a language. If it differs from the language of this page, Kefir Hub restarts once to apply it. Start it again from the HOME Menu icon."_i18n);

    gfx::drawRect(vg, 0.f, SCREEN_HEIGHT - 70.f, SCREEN_WIDTH, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    gfx::drawTextArgs(vg, SCREEN_WIDTH - 60.f, SCREEN_HEIGHT - 35.f, 22.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
        theme->GetColour(ThemeEntryID_TEXT), "  %s", "Choose language"_i18n.c_str());
}

} // namespace sphaira::ui
