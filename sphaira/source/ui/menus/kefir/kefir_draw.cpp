#include "ui/menus/kefir/kefir_internal.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"

#include <string_view>

namespace sphaira::ui::menu::kefir {
using namespace detail;

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const std::string tab_names[UPDATER_TAB_COUNT]{"Kefir"_i18n, "Firmware"_i18n};
    const char* tab_labels[UPDATER_TAB_COUNT]{tab_names[0].c_str(), tab_names[1].c_str()};
    gfx::drawTabBar(vg, theme, UPDATER_TAB_BAR, tab_labels, UPDATER_TAB_COUNT, static_cast<s64>(m_tab));

    if (m_tab == UpdaterTab::Kefir) {
        DrawKefirTab(vg, theme);
        return;
    }

    const auto tiles = static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles;
    const auto info_y = GetY() + UPDATER_INFO_Y_OFFSET;
    nvgSave(vg);
    nvgFontSize(vg, 17.f);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT_INFO));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    // a bold label and its value
    const auto info = [vg, info_y](float x, const std::string& label, const std::string& value) {
        nvgText(vg, x, info_y, label.c_str(), nullptr);
        nvgText(vg, x + 1.f, info_y, label.c_str(), nullptr);
        float bounds[4]{};
        nvgTextBounds(vg, x, info_y, label.c_str(), nullptr, bounds);
        nvgText(vg, bounds[2] + 7.f, info_y, value.c_str(), nullptr);
    };
    info(80.f, "Current Firmware:"_i18n, m_current_firmware);
    info(650.f, "Console:"_i18n, m_console_revision);
    nvgRestore(vg);

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading updater links...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, 80.f, info_y + 25.f, 17.f,
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

void Menu::DrawKefirTab(NVGcontext* vg, Theme* theme) {
    if (m_kefir_page) {
        m_kefir_page->Draw(vg, theme);
        return;
    }
    const auto& page = UPDATER_KEFIR_PAGE;
    const std::string text = m_loading ? std::string{"Loading updater links..."_i18n}
        : !m_error_message.empty() ? m_error_message : std::string{"No Kefir download found."_i18n};
    gfx::drawTextBox(vg, page.x + 100.f, page.y + page.h / 2.f - 20.f, 22.f, page.w - 200.f,
        theme->GetColour(m_loading ? ThemeEntryID_TEXT_INFO : ThemeEntryID_ERROR), text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
}

// the firmware the console runs now: its entry is yellow, so it is clear it is the same version
const NVGcolor INSTALLED_COLOUR = nvgRGB(0xF5, 0xC5, 0x18);

bool Menu::IsInstalledFirmware(const UpdaterEntry& entry) const {
    if (entry.type != UpdaterEntryType::Firmware || m_current_firmware.empty()) {
        return false;
    }
    return entry.name == m_current_firmware || entry.name.rfind(m_current_firmware + " ", 0) == 0;
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
                "%s", i18n::get(entry.name).c_str());
            return;
        }

        const auto selected = m_index == i;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);
        // no Kefir on the card: its pack is offered as an install, not an update
        const bool kefir_install = entry.type == UpdaterEntryType::Kefir && !detail::IsKnownVersion(m_current_kefir);
        const auto kefir_update = IsKefirUpdate(entry) || kefir_install;
        const auto kefir_tag = kefir_install ? "INSTALL"_i18n : "UPDATE"_i18n;
        const bool installed = IsInstalledFirmware(entry);
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
            installed ? INSTALLED_COLOUR : theme->GetColour(name_id), name.c_str());

        if (kefir_update) {
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 17.f, 15.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                kefir_tag.c_str());
        } else if (entry.type != UpdaterEntryType::Kefir) {
            const auto label = installed ? std::string{"INSTALLED"_i18n} : unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE"_i18n : std::string{TypeLabel(entry.type)});
            const auto label_colour = installed ? INSTALLED_COLOUR : (entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO);
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
        // no Kefir on the card: its pack is offered as an install, not an update
        const bool kefir_install = entry.type == UpdaterEntryType::Kefir && !detail::IsKnownVersion(m_current_kefir);
        const auto kefir_update = IsKefirUpdate(entry) || kefir_install;
        const auto kefir_tag = kefir_install ? "INSTALL"_i18n : "UPDATE"_i18n;

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
                theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", i18n::get(TileGroupLabel(entry.type)).c_str());
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
        const bool installed = IsInstalledFirmware(entry);
        const auto name_colour = installed ? INSTALLED_COLOUR : unsupported ? theme->GetColour(ThemeEntryID_TEXT_INFO) : downgrade ? theme->GetColour(ThemeEntryID_ERROR) :
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
        const auto type_label = installed ? std::string{"INSTALLED"_i18n} : kefir_update ? kefir_tag : (unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE"_i18n : std::string{TypeLabel(entry.type)}));
        const auto type_colour = installed ? INSTALLED_COLOUR : kefir_update ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : ((entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO));
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
