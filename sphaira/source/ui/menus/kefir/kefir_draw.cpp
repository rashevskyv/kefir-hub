#include "ui/menus/kefir/kefir_internal.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

#include <string_view>

namespace sphaira::ui::menu::kefir {
using namespace detail;

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto tiles = static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles;
    const auto info_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto info_y = GetY() + UPDATER_INFO_Y_OFFSET;
    nvgSave(vg);
    nvgFontSize(vg, 17.f);
    nvgFillColor(vg, info_colour);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    // Current Kefir
    const std::string current_kefir_lbl = "Current Kefir:"_i18n;
    nvgText(vg, 80.f, info_y, current_kefir_lbl.c_str(), nullptr);
    nvgText(vg, 81.f, info_y, current_kefir_lbl.c_str(), nullptr);
    float ck_bounds[4]{};
    nvgTextBounds(vg, 80.f, info_y, current_kefir_lbl.c_str(), nullptr, ck_bounds);
    nvgText(vg, ck_bounds[2] + 7.f, info_y, m_current_kefir.c_str(), nullptr);

    // Latest Kefir
    const std::string latest_kefir_lbl = "Latest Kefir:"_i18n;
    nvgText(vg, 650.f, info_y, latest_kefir_lbl.c_str(), nullptr);
    nvgText(vg, 651.f, info_y, latest_kefir_lbl.c_str(), nullptr);
    float lk_bounds[4]{};
    nvgTextBounds(vg, 650.f, info_y, latest_kefir_lbl.c_str(), nullptr, lk_bounds);
    nvgText(vg, lk_bounds[2] + 7.f, info_y, m_latest_kefir.c_str(), nullptr);

    // Current Firmware
    const std::string current_fw_lbl = "Current Firmware:"_i18n;
    nvgText(vg, 80.f, info_y + UPDATER_INFO_ROW_GAP, current_fw_lbl.c_str(), nullptr);
    nvgText(vg, 81.f, info_y + UPDATER_INFO_ROW_GAP, current_fw_lbl.c_str(), nullptr);
    float cf_bounds[4]{};
    nvgTextBounds(vg, 80.f, info_y + UPDATER_INFO_ROW_GAP, current_fw_lbl.c_str(), nullptr, cf_bounds);
    nvgText(vg, cf_bounds[2] + 7.f, info_y + UPDATER_INFO_ROW_GAP, m_current_firmware.c_str(), nullptr);

    // Console
    const std::string console_lbl = "Console:"_i18n;
    nvgText(vg, 650.f, info_y + UPDATER_INFO_ROW_GAP, console_lbl.c_str(), nullptr);
    nvgText(vg, 651.f, info_y + UPDATER_INFO_ROW_GAP, console_lbl.c_str(), nullptr);
    float c_bounds[4]{};
    nvgTextBounds(vg, 650.f, info_y + UPDATER_INFO_ROW_GAP, console_lbl.c_str(), nullptr, c_bounds);
    nvgText(vg, c_bounds[2] + 7.f, info_y + UPDATER_INFO_ROW_GAP, m_console_revision.c_str(), nullptr);

    nvgRestore(vg);

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading updater links...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, 80.f, GetY() + 71.f, 17.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
    }

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No updater entries found");
        return;
    }

    if (tiles) {
        DrawTiles(vg, theme);
    } else {
        DrawList(vg, theme);
    }
}

void Menu::DrawList(NVGcontext* vg, Theme* theme) {
    m_list->Draw(vg, theme, m_entries.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& entry = m_entries[i];
        if (entry.type == UpdaterEntryType::Section) {
            const auto top_pad = 32.f;
            const Vec4 band{v.x, v.y + top_pad, v.w, 26.f};
            gfx::drawRect(vg, band.x, band.y, band.w, band.h, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 4.f);
            gfx::drawRect(vg, v.x + 15.f, band.y + 7.f, 4.f, band.h - 14.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 2.f);
            gfx::drawTextArgs(vg, v.x + 30.f, band.y + band.h / 2.f, 16.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                "%s", entry.name.c_str());
            return;
        }

        const auto selected = m_index == i;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);
        const auto kefir_update = IsKefirUpdate(entry);
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto name_id = unsupported ? ThemeEntryID_TEXT_INFO : (downgrade ? ThemeEntryID_ERROR : (kefir_update ? ThemeEntryID_TEXT_SELECTED : text_id));

        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_entries.size() - 1) {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        std::string name = EntryDisplayName(entry);
        if (unsupported) {
            name += " [UNSUPPORTED]";
        }

        const auto text_x = v.x + 55.f;
        DrawUpdaterEntryIcon(vg, theme, entry, v.x + 15.f, v.y + 24.f, selected, unsupported);

        gfx::drawTextBox(vg, text_x, v.y + 11.f, 23.f, v.w - 230.f,
            theme->GetColour(name_id), name.c_str());

        if (kefir_update) {
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 17.f, 15.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                "UPDATE");
        } else if (entry.type != UpdaterEntryType::Kefir) {
            const auto label = unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE" : TypeLabel(entry.type));
            const auto label_colour = (entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO);
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 17.f, 15.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, label_colour,
                "%s", label.c_str());
        }

        gfx::drawTextBox(vg, text_x, v.y + 44.f, 16.f, v.w - 70.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), EntryDescription(entry));
    });
}

void Menu::DrawTiles(NVGcontext* vg, Theme* theme) {
    m_list->Draw(vg, theme, m_tile_entries.size(), m_tile_index, [this](auto* vg, auto* theme, Vec4 v, auto tile_i) {
        const auto entry_index = m_tile_entries[tile_i];
        if (entry_index == TILE_EMPTY) {
            return;
        }

        const auto& entry = m_entries[entry_index];
        const auto selected = m_index == entry_index;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);
        const auto kefir_update = IsKefirUpdate(entry);

        s64 previous_entry_index = TILE_EMPTY;
        for (s64 i = tile_i - 1; i >= 0; i--) {
            if (m_tile_entries[i] != TILE_EMPTY) {
                previous_entry_index = m_tile_entries[i];
                break;
            }
        }

        const bool show_group = previous_entry_index == TILE_EMPTY ||
            TileGroupLabel(entry.type) != std::string_view{TileGroupLabel(m_entries[previous_entry_index].type)};

        if (show_group) {
            gfx::drawTextArgs(vg, v.x, v.y - 33.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", TileGroupLabel(entry.type));
        }

        const auto tile = v;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, tile);
        } else {
            gfx::drawRect(vg, tile, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 4.f);
        }

        // Draw icon container frame (subtle background)
        gfx::drawRect(vg, tile.x + 20.f, tile.y + 20.f, 115.f, 115.f, nvgRGBA(0, 0, 0, 25), 8.f);

        // Center and scale the vector icon inside the 115x115 container
        // Original icon size: 28x23. With 2.5x scale: 70x57.5.
        const float ix = tile.x + 20.f + (115.f - 70.f) / 2.f;
        const float iy = tile.y + 20.f + (115.f - 57.5f) / 2.f;

        nvgSave(vg);
        nvgTranslate(vg, ix, iy);
        nvgScale(vg, 2.5f, 2.5f);
        DrawUpdaterEntryIcon(vg, theme, entry, 0.f, 0.f, selected, unsupported);
        nvgRestore(vg);

        // Draw texts on the right side of the card
        const auto name_colour = unsupported ? theme->GetColour(ThemeEntryID_TEXT_INFO) : downgrade ? theme->GetColour(ThemeEntryID_ERROR) :
            (kefir_update ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT));

        std::string name = EntryDisplayName(entry);
        if (unsupported) {
            name += " [UNSUPPORTED]";
        }

        const float text_x = tile.x + 148.f;
        const float text_clip_w = tile.w - 20.f - 148.f;

        // 1. Title/Name
        gfx::drawTextBox(vg, text_x, tile.y + 24.f, 18.f, text_clip_w, name_colour, name.c_str());

        // 2. Type/Status
        const auto type_label = kefir_update ? std::string{"UPDATE"} : (unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE" : TypeLabel(entry.type)));
        const auto type_colour = kefir_update ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : ((entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO));
        gfx::drawTextBox(vg, text_x, tile.y + 68.f, 14.f, text_clip_w, type_colour, type_label.c_str());

        // 3. Description
        const char* description = EntryDescription(entry);
        if (entry.type == UpdaterEntryType::Kefir || entry.type == UpdaterEntryType::Firmware) {
            description = "";
        }
        gfx::drawTextBox(vg, text_x, tile.y + 92.f, 14.f, text_clip_w, theme->GetColour(ThemeEntryID_TEXT_INFO), description, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.5f);
    });
}

} // namespace sphaira::ui::menu::kefir
