// The Kefir tab of the Updater: the changelog as a page, with the install button at the end of the text.
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cmath>

namespace sphaira::ui::menu::kefir {
namespace {

enum class KefirState { NotInstalled, Update, Latest };

auto StateOf(const std::string& current, const std::string& target) -> KefirState {
    if (!detail::IsKnownVersion(current)) {
        return KefirState::NotInstalled;
    }
    if (detail::IsKnownVersion(target) && detail::IsVersionLower(current, target)) {
        return KefirState::Update;
    }
    return KefirState::Latest;
}

constexpr float BUTTON_W = 260.f;
constexpr float BUTTON_H = 46.f;
constexpr float BUTTON_GAP = 24.f; // between the last line of the text and the button

} // namespace

auto KefirChangelogBox::ButtonLabel() const -> std::string {
    switch (StateOf(m_current_version, m_target_version)) {
        case KefirState::Update: return "Update"_i18n;
        case KefirState::Latest: return "Reinstall"_i18n;
        case KefirState::NotInstalled: break;
    }
    return "Install"_i18n;
}

void KefirChangelogBox::Activate() {
    if (m_loading) {
        App::PlaySoundEffect(SoundEffect_Limit);
        return;
    }
    if (!AtEnd()) {
        App::Notify("Read the changelog to the end, then press"_i18n + " «" + ButtonLabel() + "» " + "at the very bottom."_i18n);
        return;
    }
    m_callback();
}

void KefirChangelogBox::DrawPage(NVGcontext* vg, Theme* theme) {
    const auto state = StateOf(m_current_version, m_target_version);
    const auto target = "Kefir " + (m_target_version.empty() ? std::string{"?"} : m_target_version);
    const std::string head = state == KefirState::Update ? "Update available:"_i18n
        : state == KefirState::NotInstalled ? "Kefir is not installed. Available:"_i18n
        : "You have the latest Kefir:"_i18n;
    const auto head_text = head + " " + target;
    const auto installed = "Installed:"_i18n + " " +
        (detail::IsKnownVersion(m_current_version) ? "Kefir " + m_current_version : std::string{"not installed"_i18n});

    gfx::drawTextBold(vg, m_pos.x + 8.f, m_pos.y + 6.f, 24.f,
        theme->GetColour(state == KefirState::Latest ? ThemeEntryID_TEXT : ThemeEntryID_TEXT_SELECTED), head_text.c_str());
    gfx::drawText(vg, m_pos.x + 8.f, m_pos.y + 40.f, 17.f, theme->GetColour(ThemeEntryID_TEXT_INFO), installed.c_str());
    gfx::drawRect(vg, m_pos.x, m_pos.y + 70.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    m_text_area = Vec4{m_pos.x + 8.f, m_pos.y + 82.f, m_pos.w - 48.f, m_pos.h - 82.f};
    if (m_loading) {
        gfx::drawText(vg, m_text_area.x + m_text_area.w / 2.f, m_text_area.y + m_text_area.h / 2.f, 24.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading changelog..."_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }
    if (m_text.empty()) {
        m_text = "No changelog entries found.";
    }

    nvgSave(vg);
    m_text_height = detail::RenderChangelogText(vg, theme, m_text, Vec4{0.f, 0.f, m_text_area.w, m_text_area.h}, 0.f, false,
        CHANGELOG_FONT_SIZE, CHANGELOG_LINE_HEIGHT, CHANGELOG_HEADER_FONT_SIZE, CHANGELOG_PREAMBLE_FONT_SIZE);
    const float content_h = m_text_height + BUTTON_GAP + BUTTON_H + 8.f;
    m_max_scroll = std::max(0.f, content_h - m_text_area.h);
    m_scroll = std::clamp(m_scroll, 0.f, m_max_scroll);

    nvgScissor(vg, m_text_area.x, m_text_area.y, m_text_area.w, m_text_area.h);
    detail::RenderChangelogText(vg, theme, m_text, m_text_area, m_scroll, true,
        CHANGELOG_FONT_SIZE, CHANGELOG_LINE_HEIGHT, CHANGELOG_HEADER_FONT_SIZE, CHANGELOG_PREAMBLE_FONT_SIZE);

    // the button is the end of the text: it turns green once the reader is there
    m_install_button_rect = Vec4{m_text_area.x + (m_text_area.w - BUTTON_W) / 2.f,
        m_text_area.y + m_text_height + BUTTON_GAP - m_scroll, BUTTON_W, BUTTON_H};
    const auto label = ButtonLabel();
    if (AtEnd()) {
        gfx::drawRect(vg, m_install_button_rect, nvgRGBA(46, 125, 50, 255), 4.f);
        gfx::drawRectOutline(vg, theme, 4.f, m_install_button_rect);
        gfx::drawText(vg, m_install_button_rect.x + BUTTON_W / 2.f, m_install_button_rect.y + BUTTON_H / 2.f, 18.f,
            nvgRGBA(255, 255, 255, 255), label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    } else {
        gfx::drawRect(vg, m_install_button_rect, nvgRGBA(60, 60, 64, 255), 4.f);
        gfx::drawText(vg, m_install_button_rect.x + BUTTON_W / 2.f, m_install_button_rect.y + BUTTON_H / 2.f, 18.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);

    const auto count = std::max<s64>(1, static_cast<s64>(std::ceil(content_h / CHANGELOG_SCROLL_STEP)));
    const auto page = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_area.h / CHANGELOG_SCROLL_STEP)));
    const auto index = std::min<s64>(std::max<s64>(0, count - page), static_cast<s64>(std::ceil(m_scroll / CHANGELOG_SCROLL_STEP)));
    gfx::drawScrollbar2(vg, theme, m_text_area.x + m_text_area.w + 20.f, m_text_area.y, m_text_area.h, index, count, 1, page);
}

} // namespace sphaira::ui::menu::kefir
