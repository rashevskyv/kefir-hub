#include "ui/about_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "app.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "fs.hpp"

#include <yyjson.h>
#include <algorithm>
#include <cmath>

namespace sphaira::ui {
namespace {

constexpr const char* GITHUB_RELEASES_URL{"https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest"};
constexpr fs::FsPath CACHE_PATH{"/switch/sphaira/cache/sphaira_latest.json"};

} // namespace

AboutBox::AboutBox() {
    m_version_str = "v" + std::string(APP_VERSION);
    m_pos = Vec4{70.f, 42.f, 1140.f, 636.f};
    SetUiButtonPos({m_pos.x + m_pos.w - 30.f, m_pos.y + m_pos.h - 49.f});

    SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            FetchLatestChangelog();
        }}),
        std::make_pair(Button::UP | Button::LS_UP | Button::RS_UP, Action{static_cast<u8>(ActionType::DOWN | ActionType::HELD), [this](){
            ScrollBy(-SCROLL_STEP);
        }}),
        std::make_pair(Button::DOWN | Button::LS_DOWN | Button::RS_DOWN, Action{static_cast<u8>(ActionType::DOWN | ActionType::HELD), [this](){
            ScrollBy(SCROLL_STEP);
        }}),
        std::make_pair(Button::L | Button::L2, Action{[this](){
            ScrollBy(-m_text_area.h * 0.8f);
        }}),
        std::make_pair(Button::R | Button::R2, Action{[this](){
            ScrollBy(m_text_area.h * 0.8f);
        }})
    );

    LoadChangelog();
}

void AboutBox::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);

    if (touch->is_touching && touch->in_range(m_text_area)) {
        if (!m_touch_dragging) {
            m_touch_dragging = true;
            m_touch_last_y = static_cast<float>(touch->cur.y);
        } else {
            const float diff = m_touch_last_y - static_cast<float>(touch->cur.y);
            m_touch_last_y = static_cast<float>(touch->cur.y);
            ScrollBy(diff);
        }
    } else {
        m_touch_dragging = false;
    }
}

void AboutBox::ScrollBy(float amount) {
    if (m_loading || m_max_scroll <= 0.f) {
        return;
    }

    const auto old_scroll = m_scroll;
    m_scroll = std::clamp(m_scroll + amount, 0.f, m_max_scroll);
    if (old_scroll != m_scroll) {
        App::PlaySoundEffect(SoundEffect_Scroll);
    } else {
        App::PlaySoundEffect(SoundEffect_Limit);
    }
}

void AboutBox::LoadChangelog() {
    fs::FsNativeSd fs;
    if (fs.FileExists(CACHE_PATH)) {
        auto json = yyjson_read_file(CACHE_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
        if (json) {
            ON_SCOPE_EXIT(yyjson_doc_free(json));
            auto root = yyjson_doc_get_root(json);
            if (root) {
                auto tag_key = yyjson_obj_get(root, "tag_name");
                auto body_key = yyjson_obj_get(root, "body");
                const char* tag = tag_key ? yyjson_get_str(tag_key) : nullptr;
                const char* body = body_key ? yyjson_get_str(body_key) : nullptr;

                if (body && *body) {
                    m_text = std::string(tag ? ("**Release " + std::string(tag) + "**\n\n") : "") + std::string(body);
                    m_loading = false;
                    m_scroll = 0.f;
                    m_max_scroll = 0.f;
                    return;
                }
            }
        }
    }

    FetchLatestChangelog();
}

void AboutBox::FetchLatestChangelog() {
    m_loading = true;
    m_text.clear();
    m_scroll = 0.f;
    m_max_scroll = 0.f;

    fs::FsNativeSd().CreateDirectoryRecursively("/switch/sphaira/cache");

    curl::Api().ToFileAsync(
        curl::Url{GITHUB_RELEASES_URL},
        curl::Path{CACHE_PATH},
        curl::StopToken{this->GetToken()},
        curl::Header{
            { "Accept", "application/vnd.github+json" },
        },
        curl::OnComplete{[this](auto& result) {
            if (!result.success) {
                m_text = "Failed to download changelog from GitHub.\nCheck your internet connection and press X to retry."_i18n;
                m_loading = false;
                return false;
            }

            auto json = yyjson_read_file(CACHE_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
            if (!json) {
                m_text = "Failed to parse release notes."_i18n;
                m_loading = false;
                return false;
            }
            ON_SCOPE_EXIT(yyjson_doc_free(json));

            auto root = yyjson_doc_get_root(json);
            if (!root) {
                m_text = "Invalid release information."_i18n;
                m_loading = false;
                return false;
            }

            auto tag_key = yyjson_obj_get(root, "tag_name");
            auto body_key = yyjson_obj_get(root, "body");
            const char* tag = tag_key ? yyjson_get_str(tag_key) : nullptr;
            const char* body = body_key ? yyjson_get_str(body_key) : nullptr;

            if (body && *body) {
                m_text = std::string(tag ? ("**Release " + std::string(tag) + "**\n\n") : "") + std::string(body);
            } else {
                m_text = "No changelog entries found for the latest release."_i18n;
            }

            m_loading = false;
            return true;
        }}
    );
}

void AboutBox::DrawChangelogText(NVGcontext* vg, Theme* theme) {
    if (m_text.empty()) {
        m_text = "No changelog entries available."_i18n;
    }

    nvgSave(vg);
    m_text_height = menu::kefir::detail::RenderChangelogText(vg, theme, m_text, Vec4{0.f, 0.f, m_text_area.w, m_text_area.h}, 0.f, false,
        FONT_SIZE, LINE_HEIGHT, HEADER_FONT_SIZE, FONT_SIZE + 2.f);
    m_max_scroll = std::max(0.f, m_text_height - m_text_area.h + 14.f);
    m_scroll = std::clamp(m_scroll, 0.f, m_max_scroll);

    nvgScissor(vg, m_text_area.x, m_text_area.y, m_text_area.w, m_text_area.h);
    menu::kefir::detail::RenderChangelogText(vg, theme, m_text, m_text_area, m_scroll, true,
        FONT_SIZE, LINE_HEIGHT, HEADER_FONT_SIZE, FONT_SIZE + 2.f);
    nvgRestore(vg);

    if (m_max_scroll > 0.f) {
        const auto count = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_height / SCROLL_STEP)));
        const auto page = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_area.h / SCROLL_STEP)));
        const auto index = std::min<s64>(std::max<s64>(0, count - page), static_cast<s64>(std::ceil(m_scroll / SCROLL_STEP)));
        gfx::drawScrollbar2(vg, theme, m_text_area.x + m_text_area.w + 14.f, m_text_area.y, m_text_area.h, index, count, 1, page);
    }
}

void AboutBox::Draw(NVGcontext* vg, Theme* theme) {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

    // Title
    gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_pos.y + 24.f, 27.f,
        theme->GetColour(ThemeEntryID_TEXT_SELECTED), "About Kefir Hub"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    // Version and GitHub Subtitle
    nvgSave(vg);
    nvgFontSize(vg, 16.f);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT_INFO));

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    const std::string ver_display = "Version: "_i18n + m_version_str;
    nvgText(vg, m_pos.x + 42.f, m_pos.y + 64.f, ver_display.c_str(), nullptr);

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgText(vg, m_pos.x + m_pos.w - 42.f, m_pos.y + 64.f, "github.com/rashevskyv/kefir-hub", nullptr);
    nvgRestore(vg);

    // Dividers
    gfx::drawRect(vg, m_pos.x, m_pos.y + 94.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    gfx::drawRect(vg, m_pos.x, m_pos.y + m_pos.h - 68.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    m_text_area = Vec4{m_pos.x + 42.f, m_pos.y + 108.f, m_pos.w - 96.f, m_pos.h - 188.f};

    if (m_loading) {
        gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_text_area.y + m_text_area.h / 2.f, 22.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading changelog..."_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    } else {
        DrawChangelogText(vg, theme);
    }

    Widget::Draw(vg, theme);
}

} // namespace sphaira::ui
