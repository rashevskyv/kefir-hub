#include "ui/menus/game/game_details_internal.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/list.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "yati/nx/ncm.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::game {

using grid::FormatBytes;
using title::ContentInfoEntry;
using title::BuildContentEntry;

DbiDetailsMenu::DbiDetailsMenu(std::vector<Entry>* entries, s64 index,
        std::function<void(Entry, u32)> dump_callback,
        std::function<void(Entry, u32)> repack_callback,
        std::function<void(s64)> selection_callback)
    : MenuBase{"Game Details"_i18n, MenuFlag_None}
    , m_entries{entries}
    , m_game_index{index}
    , m_dump_callback{std::move(dump_callback)}
    , m_repack_callback{std::move(repack_callback)}
    , m_selection_callback{std::move(selection_callback)} {
        this->SetActions(
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::L, Action{"Previous tab"_i18n, [this](){ ChangeTab(-1); }}),
            std::make_pair(Button::R, Action{"Next tab"_i18n, [this](){ ChangeTab(1); }}),
            std::make_pair(Button::L2, Action{"Previous game"_i18n, [this](){ ChangeGame(-1); }}),
            std::make_pair(Button::R2, Action{"Next game"_i18n, [this](){ ChangeGame(1); }}),
            std::make_pair(Button::L3, Action{"Launch"_i18n, [this](){ LaunchEntry(CurrentEntry()); }}),
            std::make_pair(Button::A, Action{"Actions"_i18n, [this](){ FireA(); }}),
            std::make_pair(Button::START, Action{"Game actions"_i18n, [this](){ ShowGameActions(); }})
        );

        const Vec4 row{45, 382, 1190, 54};
        const Vec2 pad{0, 5};
        m_list = std::make_unique<List>(1, 4, Vec4{40, 97, 1200, 539}, row, pad);
        LoadGame();
    }


void DbiDetailsMenu::OnFocusGained() {
        MenuBase::OnFocusGained();
        // mods may have just been added (or removed) in the file browser this
        // menu pushed, so the folder state is re-read rather than cached.
        ProbeModsFolder(CurrentEntry());
    }

void DbiDetailsMenu::Update(Controller* controller, TouchInfo* touch) {
        SyncActionHint();
        MenuBase::Update(controller, touch);

        // a tap on a summary cell focuses and opens it, from either region.
        if (touch->is_clicked) {
            for (u8 i = 0; i < HeaderItem_Count; i++) {
                if (touch->in_range(HEADER_CELL[i])) {
                    m_header_index = i;
                    SetHeaderFocus(true);
                    ActivateHeaderItem();
                    return;
                }
            }

            if (touch->in_range(TAB_BAR)) {
                const auto tab = std::clamp<s64>((touch->cur.x - BAR_X) / (TAB_W + TAB_GAP), 0, 2);
                SetTab(static_cast<Tab>(tab));
                return;
            }
        }

        if (m_header_focus) {
            s8 next = m_header_index;
            bool moved = true;
            if (controller->GotDown(Button::ANY_UP)) {
                next = HEADER_UP[m_header_index];
            } else if (controller->GotDown(Button::ANY_DOWN)) {
                next = HEADER_DOWN[m_header_index];
            } else if (controller->GotDown(Button::ANY_LEFT)) {
                next = HEADER_LEFT[m_header_index];
            } else if (controller->GotDown(Button::ANY_RIGHT)) {
                next = HEADER_RIGHT[m_header_index];
            } else {
                moved = false;
            }

            if (moved && next >= 0) {
                if (next != m_header_index) {
                    m_header_index = next;
                    App::PlaySoundEffect(SoundEffect_Focus);
                }
                return;
            }

            // leaving the summary: off the bottom of a column, or a tap in the
            // list below (which then falls through and selects that row).
            if (!moved && !(touch->is_clicked && touch->in_range(LIST_AREA))) {
                // everything else (A, L/R, ...) is handled by the actions; the
                // list stays frozen while the summary has focus.
                return;
            }
            SetHeaderFocus(false);
        } else if (controller->GotDown(Button::ANY_UP) && !m_row_index) {
            // up from the first row leaves the list for the summary, landing on
            // the counter of the tab currently open.
            m_header_index = TabHeaderItem();
            SetHeaderFocus(true);
            return;
        }

        const auto count = CurrentCount();
        m_list->OnUpdate(controller, touch, m_row_index, count, [this](bool touch, auto index){
            if (touch && m_row_index == index) {
                FireAction(Button::A);
            } else {
                m_row_index = index;
            }
        }, this);
    }

void DbiDetailsMenu::Draw(NVGcontext* vg, Theme* theme) {
        MenuBase::Draw(vg, theme);

        const auto& entry = CurrentEntry();
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto grid = theme->GetColour(ThemeEntryID_GRID);

        gfx::drawRect(vg, 30, 90, 1220, 550, grid, 5.f);
        const Vec4 cover{50, 108, 190, 190};
        gfx::drawImage(vg, cover, entry.image ? entry.image : App::GetDefaultImage(), 7.f);
        DrawGameBadges(vg, theme, cover, entry);

        gfx::drawTextArgs(vg, 265, 105, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "%s", entry.GetName());
        gfx::drawTextArgs(vg, 265, 140, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", entry.GetAuthor());

        // a stat is a bold label (labels have no bold font, so drawTextBold
        // over-draws them) followed by its value in the normal info weight,
        // aligned to a fixed value column so the numbers line up. values that
        // open something take the accent colour, so what is clickable is
        // readable at a glance. NAND/SD are not repeated here - the header
        // storage bars already show this title's usage.
        constexpr float kStatSize = 18.f;
        const auto accent = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);

        // the focus highlight fills its rect, so it goes down before the stats.
        if (m_header_focus) {
            gfx::drawRectOutline(vg, theme, 4.f, HEADER_CELL[m_header_index], 4.f);
        }

        struct StatItem {
            std::string label;
            std::string value;
            bool clickable{false};
            bool is_languages{false};
            size_t label_scroll_idx{0};
        };

        const auto draw_stat_block = [&](float x, const std::vector<float>& y_positions, float row_w, const std::vector<StatItem>& items) {
            float max_label_w = 0.f;
            nvgFontSize(vg, kStatSize);
            for (const auto& item : items) {
                float b[4];
                gfx::textBounds(vg, 0, 0, b, item.label.c_str());
                max_label_w = std::max(max_label_w, b[2] - b[0]);
            }

            const float max_allowed_label_w = row_w / 3.f;
            const float label_col_w = std::min(max_label_w, max_allowed_label_w);
            const float val_x = x + label_col_w + 12.f;
            const float val_w = (x + row_w) - val_x;

            for (size_t i = 0; i < items.size(); i++) {
                const auto& item = items[i];
                const float y = y_positions[i];

                float b[4];
                nvgFontSize(vg, kStatSize);
                gfx::textBounds(vg, 0, 0, b, item.label.c_str());
                const float label_w = b[2] - b[0];

                if (label_w > max_allowed_label_w && item.label_scroll_idx < m_stat_label_scrolls.size()) {
                    m_stat_label_scrolls[item.label_scroll_idx].Draw(
                        vg, true, x, y, label_col_w, kStatSize, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, item.label, true);
                } else {
                    gfx::drawTextBold(vg, x, y, kStatSize, text, item.label.c_str());
                }

                if (item.is_languages) {
                    m_language_scroll.Draw(vg, true, val_x, y + 1.f, val_w, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, accent, item.value);
                } else {
                    gfx::drawText(vg, val_x, y, kStatSize, item.clickable ? accent : info, item.value.c_str());
                }
            }
        };

        char id_str[24];
        std::snprintf(id_str, sizeof(id_str), "%016lX", entry.app_id);
        char saves_str[48];
        std::snprintf(saves_str, sizeof(saves_str), "%zu (%s allocated)", m_saves.size(), FormatBytes(m_save_allocated_size).c_str());

        // Block 1: Left column, upper pair (Title ID, Version)
        draw_stat_block(265.f, {STAT_ROW_Y[0], STAT_ROW_Y[1]}, 530.f, {
            StatItem{"Title ID"_i18n, id_str, false, false, 0},
            StatItem{"Version"_i18n, m_display_version[0] ? m_display_version : "-", false, false, 1}
        });

        // Block 2: Left column, lower pair (Languages, Mods folder)
        draw_stat_block(265.f, {STAT_ROW_Y[2], STAT_ROW_Y[3]}, 530.f, {
            StatItem{"Languages"_i18n, ForcedLanguageText().empty() ? (m_languages.empty() ? "-" : m_languages) : ForcedLanguageText(), false, true, 2},
            StatItem{"Mods folder"_i18n, !entry.mods_folder ? "Not found"_i18n : (entry.layeredfs ? "Found"_i18n + " · " + FormatBytes(static_cast<u64>(m_mods_size)) : "Empty"_i18n), true, false, 3}
        });

        // Block 3: Right column, upper pair (Play time, Last played)
        draw_stat_block(810.f, {112.f, 142.f}, 425.f, {
            StatItem{"Play time"_i18n, FormatPlaytime(entry.playtime), false, false, 4},
            StatItem{"Last played"_i18n, FormatLastPlayed(entry.last_played), false, false, 5}
        });

        // Block 4: Right column, lower group (Components, Tickets, Saves, Save quota)
        draw_stat_block(810.f, {STAT_ROW_Y[0], STAT_ROW_Y[1], STAT_ROW_Y[2], STAT_ROW_Y[3]}, 425.f, {
            StatItem{"Components"_i18n, std::to_string(m_components.size()), true, false, 6},
            StatItem{"Tickets"_i18n, std::to_string(m_tickets.size()), true, false, 7},
            StatItem{"Saves"_i18n, saves_str, true, false, 8},
            StatItem{"Save quota"_i18n, FormatBytes(m_save_size) + " + " + FormatBytes(m_save_journal_size), false, false, 9}
        });

        const auto hint = m_header_focus ? HeaderItemHint(m_header_index)
            : "Press Up on the first row to use the details above"_i18n;
        gfx::drawText(vg, 265, 289, 14.f, info, hint.c_str());

        const std::array<std::string, 3> tab_names{"Content"_i18n, "Tickets"_i18n, "Saves"_i18n};
        const std::array<size_t, 3> tab_counts{m_components.size(), m_tickets.size(), m_saves.size()};

        // the tab bar and the content panel share the POPUP surface tone, so the
        // selected tab reads as one continuous surface with the list below it.
        // tabs round only their top corners (flat bottom, like a folder tab) and
        // the panel rounds only its bottom corners; the selected tab flows into
        // the panel with no seam, while the others sit lower and recessed.
        const float body_top = TAB_TOP + TAB_H;
        const float radius = 8.f;

        const auto surface = theme->GetColour(ThemeEntryID_POPUP);
        const auto recessed = theme->GetColour(ThemeEntryID_BACKGROUND);

        // content panel (flat top so the selected tab flows into it).
        gfx::drawRectVarying(vg, LIST_AREA, surface, 0.f, 0.f, radius, radius);

        constexpr float tab_w = TAB_W;
        for (size_t i = 0; i < tab_names.size(); i++) {
            const bool selected = i == static_cast<size_t>(m_tab);
            const float x = BAR_X + i * (tab_w + TAB_GAP);
            const float y = selected ? TAB_TOP : TAB_TOP + 5.f;
            const float h = body_top - y; // flat bottom always meets the panel edge.
            gfx::drawRectVarying(vg, Vec4{x, y, tab_w, h}, selected ? surface : recessed, radius, radius, 0.f, 0.f);
            if (selected) {
                gfx::drawRectVarying(vg, Vec4{x, y, tab_w, 3.f}, accent, radius, radius, 0.f, 0.f);
            }
            const auto label_colour = theme->GetColour(selected ? ThemeEntryID_TEXT : ThemeEntryID_TEXT_INFO);
            char label[96];
            std::snprintf(label, sizeof(label), "%s  %zu", tab_names[i].c_str(), tab_counts[i]);
            const float text_y = (y + body_top) * 0.5f;
            if (selected) {
                gfx::drawTextBold(vg, x + tab_w * 0.5f, text_y, 20.f, label_colour, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            } else {
                gfx::drawText(vg, x + tab_w * 0.5f, text_y, 20.f, label_colour, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
        }

        if (R_FAILED(m_load_result) && !m_components.empty()) {
            gfx::drawTextArgs(vg, 810, 294, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                theme->GetColour(ThemeEntryID_ERROR), "Partial metadata read (0x%X)", m_load_result);
        }

        if (R_FAILED(m_load_result) && m_components.empty() && m_tab != Tab::Saves) {
            gfx::drawTextArgs(vg, 55, 405, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info,
                "Unable to read installed metadata (0x%X)", m_load_result);
            return;
        }

        if (!CurrentCount()) {
            const auto empty = m_tab == Tab::Content ? "No installed components"_i18n :
                (m_tab == Tab::Tickets ? "No rights IDs or tickets"_i18n : "No save data found for this title"_i18n);
            gfx::drawText(vg, 640, 475, 22.f, info, empty.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            return;
        }

        m_list->Draw(vg, theme, CurrentCount(), m_row_index, [this](auto* vg, auto* theme, auto v, auto index){
            const bool selected = index == m_row_index;
            const auto primary = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            const auto secondary = theme->GetColour(ThemeEntryID_TEXT_INFO);
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 4.f);
            } else {
                gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_BACKGROUND), 4.f);
            }

            if (m_tab == Tab::Content) {
                const auto& row = m_components[index];
                // DBI-style row: [SD]/[NAND] location, type, readable version on
                // the top line; per-component Title ID, size and content/rights
                // counts underneath.
                const bool on_sd = row.status.storageID == NcmStorageId_SdCard;
                const auto loc_col = theme->GetColour(on_sd ? ThemeEntryID_HIGHLIGHT_1 : ThemeEntryID_HIGHLIGHT_2);
                gfx::drawTextBold(vg, v.x + 15, v.y + 7, 18.f, loc_col, on_sd ? "[SD]" : "[NAND]");

                gfx::drawTextArgs(vg, v.x + 105, v.y + 6, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", ncm::GetMetaTypeStr(row.status.meta_type));

                // the Application row shows the NACP display version (e.g. 1.0.4);
                // updates/DLC show the raw content-meta version.
                if (row.status.meta_type == NcmContentMetaType_Application && m_display_version[0]) {
                    gfx::drawTextArgs(vg, v.x + 430, v.y + 8, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                        "%s (v%u)", m_display_version, row.status.version);
                } else {
                    gfx::drawTextArgs(vg, v.x + 430, v.y + 8, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                        "v%u", row.status.version);
                }

                gfx::drawTextArgs(vg, v.x + 15, v.y + 31, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "Title ID  %016lX", row.status.application_id);
                gfx::drawTextArgs(vg, v.x + 430, v.y + 31, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s · %u contents · %u rights", FormatBytes(row.size).c_str(), row.content_count, row.rights_count);
            } else if (m_tab == Tab::Tickets) {
                const auto& row = m_tickets[index];
                gfx::drawTextArgs(vg, v.x + 15, v.y + 9, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", utils::hexIdToStr(row.id).str);
                gfx::drawTextArgs(vg, v.x + 530, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s | key generation %u", ncm::GetMetaTypeStr(row.meta_type), row.key_generation);
                gfx::drawTextArgs(vg, v.x + 930, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s", row.ticket_size ? "Common" : (row.personalized ? "Personalized" : "Missing"));
            } else {
                const auto& row = m_saves[index];
                const auto space_label = "[" + GetSaveSpaceLabel(row.info.save_data_space_id) + "]";
                gfx::drawTextArgs(vg, v.x + 15, v.y + 7, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", row.account.c_str());
                gfx::drawTextArgs(vg, v.x + 280, v.y + 8, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s  %s", space_label.c_str(), i18n::get(save::GetSaveTypeLabel(row.info.save_data_type)).c_str());
                const auto rank_str = row.info.save_data_rank == FsSaveDataRank_Primary ? "Primary"_i18n : "Secondary"_i18n;
                gfx::drawTextArgs(vg, v.x + 560, v.y + 8, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    ("Rank: %s · Index: %u"_i18n).c_str(), rank_str.c_str(), row.info.save_data_index);
                gfx::drawTextArgs(vg, v.x + 860, v.y + 8, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    ("Save ID: %016lX"_i18n).c_str(), row.info.save_data_id);

                if (row.has_extra) {
                    gfx::drawTextArgs(vg, v.x + 15, v.y + 31, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                        ("Allocated: %s · Data: %s · Journal: %s"_i18n).c_str(),
                        FormatBytes(row.info.size).c_str(),
                        FormatBytes(row.extra.data_size).c_str(),
                        FormatBytes(row.extra.journal_size).c_str());
                } else {
                    gfx::drawTextArgs(vg, v.x + 15, v.y + 31, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                        theme->GetColour(ThemeEntryID_ERROR),
                        ("Allocated: %s · Extra data unreadable (0x%X)"_i18n).c_str(),
                        FormatBytes(row.info.size).c_str(), row.extra_rc);
                }
            }
        });
    }

auto DbiDetailsMenu::CurrentCount() const -> size_t {
    switch (m_tab) {
        case Tab::Content: return m_components.size();
        case Tab::Tickets: return m_tickets.size();
        case Tab::Saves: return m_saves.size();
    }
    return 0;
}


void DbiDetailsMenu::LoadSaves(u64 app_id) {
        LoadGameSaves(app_id, m_saves, m_save_allocated_size);
    }

    // mount this component's NCA files as a read-only fs and open them in the
    // real file browser, so they can be navigated (and later exposed over
    // MTP/FTP) - rather than shown in a throwaway popup.
void DbiDetailsMenu::ShowCurrentActions() {
        if (!CurrentCount()) {
            if (m_tab == Tab::Saves) {
                auto options = std::make_unique<Sidebar>("Save Actions"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Create save slot"_i18n, [this](){
                    PromptCreateSaveSlot(CurrentEntry().app_id, CurrentEntry().GetName(), m_saves, [this](){ LoadGame(); });
                }, "Create a new save data slot for a local user."_i18n)->SetIcon(ActionIcon::Save);

                options->Add<SidebarEntryCallback>("Backup and restore"_i18n, [app_id = CurrentEntry().app_id](){
                    App::Push<save::Menu>(MenuFlag_None, app_id);
                }, "Open the saves menu showing only this game."_i18n);
            }
            return;
        }
        if (m_tab == Tab::Content) {
            const auto component = m_components[m_row_index];
            auto options = std::make_unique<Sidebar>("Component Actions"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));
            options->Add<SidebarEntryCallback>("Open in file browser"_i18n, [this, component](){
                OpenComponentInBrowser(component);
            }, "Mount this component's NCA files and browse them in the file browser."_i18n);
            options->Add<SidebarEntryCallback>("Dump NSP"_i18n, [this, component](){
                m_dump_callback(CurrentEntry(), ContentFlagFromMetaType(component.status.meta_type));
            }, true, "Export only this installed component as an NSP."_i18n);
            if (component.status.storageID == NcmStorageId_BuiltInUser || component.status.storageID == NcmStorageId_SdCard) {
                const auto target = component.status.storageID == NcmStorageId_BuiltInUser
                    ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser;
                const auto label = (target == NcmStorageId_SdCard ? "Move component to SD"_i18n : "Move component to NAND"_i18n);

                options->Add<SidebarEntryCallback>(label, [this, component, target](){
                    App::Push<ProgressBox>(0, MovingToLabel(target), i18n::get(ncm::GetReadableMetaTypeStr(component.status.meta_type)), [component, target](auto pbox) -> Result {
                        DropBoostForMove();
                        return title::MoveComponent(component.status, target, pbox);
                    }, [this, target](Result rc){
                        if (R_SUCCEEDED(rc)) {
                            App::Notify(MovedToLabel(target));
                            LoadGame();
                        } else if (rc != Result_TransferCancelled) {
                            App::PushErrorBox(rc, "Move failed!"_i18n);
                        }
                    }, 1, PRIO_PREEMPTIVE, 1024*128, false);
                }, label + ".");
            }

            options->Add<SidebarEntryCallback>("Content information"_i18n, [component](){
                char message[512];
                std::snprintf(message, sizeof(message),
                    "Title ID: %016lX\nType: %s\nVersion: %u\nStorage: %s\nSize: %s\nContent files: %u\nRights IDs: %u",
                    component.status.application_id, ncm::GetMetaTypeStr(component.status.meta_type), component.status.version,
                    ncm::GetStorageIdStr(component.status.storageID), FormatBytes(component.size).c_str(),
                    component.content_count, component.rights_count);
                App::Push<OptionBox>(message, "OK"_i18n);
            }, "Show installed content metadata."_i18n);
        } else if (m_tab == Tab::Tickets) {
            const auto& ticket = m_tickets[m_row_index];
            char message[512];
            std::snprintf(message, sizeof(message), "Rights ID: %s\nComponent: %s\nKey generation: %u\nTicket: %s\nTicket size: %s",
                utils::hexIdToStr(ticket.id).str, ncm::GetMetaTypeStr(ticket.meta_type), ticket.key_generation,
                ticket.ticket_size ? "Common" : (ticket.personalized ? "Personalized" : "Missing"),
                FormatBytes(ticket.ticket_size).c_str());
            App::Push<OptionBox>(message, "OK"_i18n);
        } else {
            const auto row = m_saves[m_row_index];
            auto options = std::make_unique<Sidebar>("Save Actions"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            options->Add<SidebarEntryCallback>("Backup and restore"_i18n, [app_id = CurrentEntry().app_id](){
                App::Push<save::Menu>(MenuFlag_None, app_id);
            }, "Open the saves menu showing only this game."_i18n);

            options->Add<SidebarEntryCallback>("Increase save size"_i18n, [this, row](){
                PromptIncreaseSaveSize(CurrentEntry().GetName(), row, [this](){ LoadGame(); });
            }, "Increase the allocated data size for this save slot."_i18n)->SetIcon(ActionIcon::Move);

            options->Add<SidebarEntryCallback>("Create save slot"_i18n, [this](){
                PromptCreateSaveSlot(CurrentEntry().app_id, CurrentEntry().GetName(), m_saves, [this](){ LoadGame(); });
            }, "Create a new save data slot for a local user."_i18n)->SetIcon(ActionIcon::Save);

            options->Add<SidebarEntryCallback>("Save information"_i18n, [row](){
                App::Push<OptionBox>(FormatSaveInfoMessage(row), "OK"_i18n);
            }, "Show the raw save data metadata."_i18n);
        }
    }

void DbiDetailsMenu::ChangeGame(s64 delta) {
        if (!m_entries || m_entries->empty()) return;
        const auto count = static_cast<s64>(m_entries->size());
        m_game_index = (m_game_index + delta + count) % count;
        LoadGame();
        m_selection_callback(m_game_index);
    }

void DbiDetailsMenu::ChangeTab(s64 delta) {
        constexpr s64 count = 3;
        SetTab(static_cast<Tab>((static_cast<s64>(m_tab) + delta + count) % count));
    }

void DbiDetailsMenu::SetTab(Tab tab) {
        m_tab = tab;
        m_row_index = 0;
        m_list->SetYoff(0);
        SetHeaderFocus(false);
        if (m_tab == Tab::Saves) {
            SetAction(Button::X, Action{"Create save slot"_i18n, [this](){
                PromptCreateSaveSlot(CurrentEntry().app_id, CurrentEntry().GetName(), m_saves, [this](){ LoadGame(); });
            }});
        } else {
            RemoveAction(Button::X);
        }
    }

auto DbiDetailsMenu::TabHeaderItem() const -> u8 {
    switch (m_tab) {
        case Tab::Tickets: return HeaderItem_Tickets;
        case Tab::Saves: return HeaderItem_Saves;
        default: return HeaderItem_Components;
    }
}

void DbiDetailsMenu::SetHeaderFocus(bool focus) {
        if (m_header_focus != focus) {
            App::PlaySoundEffect(SoundEffect_Focus);
        }
        m_header_focus = focus;
    }

void DbiDetailsMenu::FireA() {
        if (m_header_focus) {
            ActivateHeaderItem();
        } else {
            ShowCurrentActions();
        }
    }

    // A does different things in the two focus regions, so its footer hint
    // follows the focus. Re-registering the action from inside the action's own
    // callback would replace the std::function while it is running, so the hint
    // is synced here instead, before actions are dispatched.
void DbiDetailsMenu::SyncActionHint() {
        if (m_hint_is_header == m_header_focus) {
            return;
        }
        m_hint_is_header = m_header_focus;
        SetAction(Button::A, Action{m_header_focus ? "Open"_i18n : "Actions"_i18n, [this](){ FireA(); }});
    }

auto DbiDetailsMenu::HeaderItemHint(u8 item) -> std::string {
        switch (item) {
            case HeaderItem_Languages: return "Show every language this game ships with"_i18n;
            case HeaderItem_Mods: return "Open /atmosphere/contents/<Title ID>, where LayeredFS loads mods from"_i18n;
            case HeaderItem_Components: return "Show the installed base, updates and DLC"_i18n;
            case HeaderItem_Tickets: return "Show the rights IDs and tickets of this game"_i18n;
            default: return "Show the save data of this game"_i18n;
        }
    }


void OpenGameDetails(
    std::vector<Entry>* entries,
    s64 index,
    std::function<void(Entry, u32)> dump_callback,
    std::function<void(Entry, u32)> repack_callback,
    std::function<void(s64)> selection_callback)
{
    App::Push<DbiDetailsMenu>(entries, index, std::move(dump_callback), std::move(repack_callback), std::move(selection_callback));
}

} // namespace sphaira::ui::menu::game
