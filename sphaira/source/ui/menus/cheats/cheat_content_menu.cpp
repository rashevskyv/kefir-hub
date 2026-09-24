#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_db.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/error_box.hpp"
#include "ui/scrollable_text.hpp"

#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <sstream>
#include <switch.h>
#include <cstdio>
#include <cstring>

namespace sphaira::ui::menu::hats {

using namespace detail;

// ============================================================
// CheatContentMenu - View cheat file content (shows titles in list)
// ============================================================

CheatContentMenu::CheatContentMenu(const GameCheatInfo& game, const std::string& build_id, const std::string& content)
    : MenuBase{"Cheat Content", MenuFlag_None}, m_game(game), m_build_id(build_id) {

    this->SetActions(
        std::make_pair(Button::A, Action{"View Code"_i18n, [this](){
            OnViewCheat();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    // Parse the cheat content to extract titles
    ParseCheatContent(content);

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatContentMenu::~CheatContentMenu() {
}

void CheatContentMenu::ParseCheatContent(const std::string& content) {
    m_cheats.clear();

    // First, scan for source footer comment to determine default source
    // Format: // source: CheatSlips or // source: nx-cheats-db
    CheatSource default_source = CheatSource::NxDb; // Default source
    std::string lower_content = content;
    std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);

    if (lower_content.find("// source: cheatslips") != std::string::npos) {
        default_source = CheatSource::Cheatslips;
    } else if (lower_content.find("// source: nx-cheats-db") != std::string::npos ||
               lower_content.find("// source: nxdb") != std::string::npos) {
        default_source = CheatSource::NxDb;
    } else if (lower_content.find("// source: gbatemp") != std::string::npos) {
        default_source = CheatSource::Gbatemp;
    }

    // Parse cheat file format
    std::istringstream stream(content);
    std::string line;
    CheatTitle current_cheat;
    bool in_cheat = false;
    CheatSource current_source = default_source; // Use detected source

    // Helper function to check if a line should be skipped (non-cheat entries)
    auto should_skip_entry = [](const std::string& name) -> bool {
        if (name.empty()) return true;

        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        // Skip common non-cheat entries
        if (lower_name.find("www.") != std::string::npos) return true;  // Website URLs (e.g., www.cheatslips.com)
        if (lower_name.find("credits:") == 0) return true;  // Credits entries (e.g., credits: author)
        if (lower_name.find("credit:") == 0) return true;   // Credits entries (e.g., credit: author)
        if (lower_name == "credits") return true;           // Just "Credits"
        if (lower_name == "credit") return true;            // Just "Credit"

        return false;
    };

    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Check for source comment: // cheats from: xxx
        if (!line.empty() && line.substr(0, 2) == "//") {
            std::string lower_line = line;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

            if (lower_line.find("cheatslips") != std::string::npos) {
                current_source = CheatSource::Cheatslips;
            } else if (lower_line.find("nx-cheats-db") != std::string::npos || lower_line.find("nxdb") != std::string::npos) {
                current_source = CheatSource::NxDb;
            } else if (lower_line.find("gbatemp") != std::string::npos) {
                current_source = CheatSource::Gbatemp;
            }
            continue;
        }

        // Check for cheat title/master-code format [Title] or {Title}
        if (IsCheatHeaderLine(line)) {
            // Save previous cheat if exists
            if (in_cheat && !current_cheat.name.empty()) {
                // Skip non-cheat entries like credits, website headers
                if (!should_skip_entry(current_cheat.name)) {
                    // Determine if cheat is empty (no actual code)
                    std::string content_check = current_cheat.content;
                    size_t start_pos = content_check.find('\n');
                    if (start_pos != std::string::npos) {
                        content_check = content_check.substr(start_pos + 1);
                    }
                    // Check if empty, whitespace only, or contains "Quota exceeded" message
                    std::string content_lower = content_check;
                    std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
                    current_cheat.is_empty = content_check.empty() ||
                        content_check.find_first_not_of(" \t\r\n") == std::string::npos ||
                        content_lower.find("quota exceeded") != std::string::npos ||
                        content_lower.find("quotaexceeded") != std::string::npos;

                    // Only add if it's a valid cheat (not empty/whitespace)
                    if (!current_cheat.name.empty() && current_cheat.name.find_first_not_of(" \t\r\n") != std::string::npos) {
                        m_cheats.push_back(current_cheat);
                    }
                }
            }

            // Start new cheat
            current_cheat.name = GetCheatHeaderName(line);
            current_cheat.content = line + "\n";
            current_cheat.source = current_source;
            current_cheat.is_empty = false;
            in_cheat = true;
        } else if (in_cheat) {
            // Add line to current cheat content
            current_cheat.content += line + "\n";
        }
    }

    // Don't forget the last cheat
    if (in_cheat && !current_cheat.name.empty()) {
        // Skip non-cheat entries
        if (!should_skip_entry(current_cheat.name)) {
            // Determine if cheat is empty
            std::string content_check = current_cheat.content;
            size_t start_pos = content_check.find('\n');
            if (start_pos != std::string::npos) {
                content_check = content_check.substr(start_pos + 1);
            }
            // Check if empty, whitespace only, or contains "Quota exceeded" message
            std::string content_lower = content_check;
            std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
            current_cheat.is_empty = content_check.empty() ||
                content_check.find_first_not_of(" \t\r\n") == std::string::npos ||
                content_lower.find("quota exceeded") != std::string::npos ||
                content_lower.find("quotaexceeded") != std::string::npos;

            // Only add if it's a valid cheat (not empty/whitespace)
            if (!current_cheat.name.empty() && current_cheat.name.find_first_not_of(" \t\r\n") != std::string::npos) {
                m_cheats.push_back(current_cheat);
            }
        }
    }

    log_write("[Cheats] Parsed %zu cheat titles (filtered out non-cheat entries)\n", m_cheats.size());
}

void CheatContentMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_cheats.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_cheats.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                // On touch, could show full cheat content in a popup
                App::Notify("Press A to view cheat code");
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void CheatContentMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // Draw header info
    gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "%s | %s | %zu cheats", m_game.name.c_str(), m_build_id.c_str(), m_cheats.size());

    if (m_cheats.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "Cheats Not Found"_i18n.c_str());
        return;
    }

    // Save and restore scissor to clip list drawing area
    nvgSave(vg);
    // Clip area starts below the header text; inflated by the selection outline
    // pad so the highlight of edge rows isn't clipped.
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, 75.f - p, GetY() + 40.f - p, 1220.f - 150.f + p * 2, SCREEN_HEIGHT - 100.f - (GetY() + 40.f) + p * 2);
    ON_SCOPE_EXIT(nvgRestore(vg));

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_cheats.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& cheat = m_cheats[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_cheats.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        // Draw source badge
        const char* source_badge = "";
        NVGcolor source_color = theme->GetColour(ThemeEntryID_TEXT_INFO);
        if (cheat.source == CheatSource::Cheatslips) {
            source_badge = "CS";  // Removed brackets to fix rendering
            source_color = nvgRGB(0x4A, 0x90, 0xE2); // Blue for CheatSlips
        } else if (cheat.source == CheatSource::NxDb) {
            source_badge = "NX";  // Removed brackets to fix rendering
            source_color = nvgRGB(0x6B, 0xC6, 0x58); // Green for nx-cheats-db
        } else if (cheat.source == CheatSource::Gbatemp) {
            source_badge = "GB";  // Removed brackets to fix rendering
            source_color = nvgRGB(0xE2, 0x7D, 0x4A); // Orange for GBATemp
        }

        // Cheat name (truncated if too long)
        std::string name = cheat.name;
        if (name.length() > 50) {
            name = name.substr(0, 47) + "...";
        }

        float text_offset = text_xoffset;

        // Draw source badge first (without brackets to avoid rendering issues)
        if (strlen(source_badge) > 0) {
            gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                source_color,
                "%s", source_badge);
            text_offset += 28.f; // Width of CS/NX/GB text
        }

        // Draw empty indicator if cheat has no content (without brackets)
        if (cheat.is_empty) {
            gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                nvgRGB(0xFF, 0x00, 0x00), // Red for empty
                "EMPTY");  // Removed brackets to fix rendering
            text_offset += 45.f; // Width of EMPTY text
        }

        // Add extra space before cheat name
        text_offset += 8.f;

        // Draw cheat name with proper spacing
        gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            cheat.is_empty ? nvgRGB(0xFF, 0x66, 0x66) : theme->GetColour(text_id), // Red tint if empty
            "[%s]", name.c_str());
    });
}

void CheatContentMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

void CheatContentMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatContentMenu::OnViewCheat() {
    if (m_cheats.empty() || m_index < 0 || m_index >= (s64)m_cheats.size()) {
        return;
    }

    const auto& cheat = m_cheats[m_index];
    App::Push<CheatCodeViewerMenu>(cheat.name, cheat.content, cheat.is_empty);
}

// ============================================================
// CheatCodeViewerMenu - View individual cheat code (scrollable)
// ============================================================

CheatCodeViewerMenu::CheatCodeViewerMenu(const std::string& title, const std::string& content, bool is_empty)
    : MenuBase{"Cheat Code", MenuFlag_None}, m_title(title), m_content(content), m_is_empty(is_empty) {

    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    // Calculate content height for scrolling
    // Rough estimation: each line is about 20px tall
    size_t line_count = std::count(m_content.begin(), m_content.end(), '\n') + 1;
    m_content_height = line_count * 20.f;
    if (m_content_height < 100.f) {
        m_content_height = 100.f;
    }
}

CheatCodeViewerMenu::~CheatCodeViewerMenu() {
}

void CheatCodeViewerMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    // Handle scrolling with joystick
    if (controller->GotDown(Button::LS_UP) ||
        controller->GotDown(Button::RS_UP) ||
        controller->GotHeld(Button::LS_UP) ||
        controller->GotHeld(Button::RS_UP)) {
        m_scroll_offset -= 5.f;
    }
    if (controller->GotDown(Button::LS_DOWN) ||
        controller->GotDown(Button::RS_DOWN) ||
        controller->GotHeld(Button::LS_DOWN) ||
        controller->GotHeld(Button::RS_DOWN)) {
        m_scroll_offset += 5.f;
    }

    // Clamp scroll offset
    float max_scroll = m_content_height - (SCREEN_HEIGHT - 150.f);
    if (max_scroll < 0) max_scroll = 0;
    if (m_scroll_offset < 0) m_scroll_offset = 0;
    if (m_scroll_offset > max_scroll) m_scroll_offset = max_scroll;
}

void CheatCodeViewerMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const float margin = 80.f;
    const float top_margin = GetY() + 50.f;
    const float content_width = SCREEN_WIDTH - 150.f;
    const float max_height = SCREEN_HEIGHT - 150.f;

    // Draw title
    gfx::drawTextArgs(vg, margin, GetY() + 20.f, 20.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_HIGHLIGHT_1),
        "[%s]", m_title.c_str());

    // Draw empty warning if applicable
    if (m_is_empty) {
        gfx::drawTextArgs(vg, margin, GetY() + 120.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            nvgRGB(0xFF, 0x00, 0x00),
            "⚠️ EMPTY CHEAT CODE or QUOTA EXCEEDED!");
    }

    // Save and clip for scrolling
    nvgSave(vg);
    nvgScissor(vg, margin, top_margin, content_width, max_height);

    // Draw cheat code content with scrolling
    float y = top_margin - m_scroll_offset;
    constexpr float line_height = 20.f;

    std::istringstream stream(m_content);
    std::string line;
    while (std::getline(stream, line)) {
        if (y + line_height > top_margin - 20.f && y < top_margin + max_height) {
            // Use monospace-like font for code
            NVGcolor color = theme->GetColour(ThemeEntryID_TEXT);

            // Highlight empty quota message
            std::string line_lower = line;
            std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
            if (line_lower.find("quota") != std::string::npos) {
                color = nvgRGB(0xFF, 0x66, 0x66);
            }

            gfx::drawTextArgs(vg, margin, y, 16.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                color,
                "%s", line.c_str());
        }
        y += line_height;
    }

    nvgRestore(vg);

    // Draw scroll indicator if content is scrollable
    if (m_content_height > max_height) {
        float scroll_bar_height = (max_height / m_content_height) * max_height;
        float scroll_bar_y = top_margin + (m_scroll_offset / m_content_height) * max_height;

        gfx::drawRect(vg, SCREEN_WIDTH - margin + 10.f, scroll_bar_y, 5.f, scroll_bar_height,
            nvgRGBA(0x80, 0x80, 0x80, 0x80));
    }
}

} // namespace sphaira::ui::menu::hats
