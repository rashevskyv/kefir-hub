#include "ui/menus/game/game_details.hpp"
#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/list.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/error_box.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/es.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::game {

using grid::FormatBytes;
using title::ContentInfoEntry;
using title::BuildContentEntry;

struct DbiDetailsMenu final : MenuBase {
    enum class Tab : u8 { Content, Tickets, Saves };

    DbiDetailsMenu(std::vector<Entry>* entries, s64 index,
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

    auto GetShortTitle() const -> const char* override { return "Game Details"; }

    void OnFocusGained() override {
        MenuBase::OnFocusGained();
        // mods may have just been added (or removed) in the file browser this
        // menu pushed, so the folder state is re-read rather than cached.
        ProbeModsFolder(CurrentEntry());
    }

    void Update(Controller* controller, TouchInfo* touch) override {
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

    void Draw(NVGcontext* vg, Theme* theme) override {
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
            StatItem{"Languages"_i18n, m_languages.empty() ? "-" : m_languages, false, true, 2},
            StatItem{"Mods folder"_i18n, !entry.mods_folder ? "Not found"_i18n : (entry.layeredfs ? "Found"_i18n : "Empty"_i18n), true, false, 3}
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
            const char* empty = m_tab == Tab::Content ? "No installed components" :
                (m_tab == Tab::Tickets ? "No rights IDs or tickets" : "No save data");
            gfx::drawText(vg, 640, 475, 22.f, info, empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            return;
        }

        m_list->Draw(vg, theme, CurrentCount(), [this](auto* vg, auto* theme, auto v, auto index){
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
                gfx::drawTextArgs(vg, v.x + 15, v.y + 9, 19.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", row.account.c_str());
                gfx::drawTextArgs(vg, v.x + 350, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s | %s", save::GetSaveTypeLabel(row.info.save_data_type), FormatBytes(row.info.size).c_str());
                gfx::drawTextArgs(vg, v.x + 760, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "Save ID %016lX", row.info.save_data_id);
            }
        });
    }

private:
    auto CurrentEntry() -> Entry& { return (*m_entries)[m_game_index]; }
    auto CurrentEntry() const -> const Entry& { return (*m_entries)[m_game_index]; }

    auto CurrentCount() const -> size_t {
        switch (m_tab) {
            case Tab::Content: return m_components.size();
            case Tab::Tickets: return m_tickets.size();
            case Tab::Saves: return m_saves.size();
        }
        return 0;
    }

    void ChangeGame(s64 delta) {
        if (!m_entries || m_entries->empty()) return;
        const auto count = static_cast<s64>(m_entries->size());
        m_game_index = (m_game_index + delta + count) % count;
        LoadGame();
        m_selection_callback(m_game_index);
    }

    void ChangeTab(s64 delta) {
        constexpr s64 count = 3;
        SetTab(static_cast<Tab>((static_cast<s64>(m_tab) + delta + count) % count));
    }

    void SetTab(Tab tab) {
        m_tab = tab;
        m_row_index = 0;
        m_list->SetYoff(0);
        SetHeaderFocus(false);
    }

    auto TabHeaderItem() const -> u8 {
        switch (m_tab) {
            case Tab::Tickets: return HeaderItem_Tickets;
            case Tab::Saves: return HeaderItem_Saves;
            default: return HeaderItem_Components;
        }
    }

    void SetHeaderFocus(bool focus) {
        if (m_header_focus != focus) {
            App::PlaySoundEffect(SoundEffect_Focus);
        }
        m_header_focus = focus;
    }

    void FireA() {
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
    void SyncActionHint() {
        if (m_hint_is_header == m_header_focus) {
            return;
        }
        m_hint_is_header = m_header_focus;
        SetAction(Button::A, Action{m_header_focus ? "Open"_i18n : "Actions"_i18n, [this](){ FireA(); }});
    }

    static auto HeaderItemHint(u8 item) -> std::string {
        switch (item) {
            case HeaderItem_Languages: return "Show every language this game ships with"_i18n;
            case HeaderItem_Mods: return "Open /atmosphere/contents/<Title ID>, where LayeredFS loads mods from"_i18n;
            case HeaderItem_Components: return "Show the installed base, updates and DLC"_i18n;
            case HeaderItem_Tickets: return "Show the rights IDs and tickets of this game"_i18n;
            default: return "Show the save data of this game"_i18n;
        }
    }

    void ActivateHeaderItem() {
        switch (m_header_index) {
            case HeaderItem_Languages: {
                if (m_language_list.empty()) {
                    App::Notify("No language data"_i18n);
                    break;
                }
                App::Push<PopupList>("Supported languages"_i18n, m_language_list, [](auto){});
            }   break;
            case HeaderItem_Mods: OpenModsFolder(); break;
            case HeaderItem_Components: SetTab(Tab::Content); break;
            case HeaderItem_Tickets: SetTab(Tab::Tickets); break;
            case HeaderItem_Saves: SetTab(Tab::Saves); break;
        }
    }

    // the mods folder is what LayeredFS reads replacement game files from;
    // opening it in the file browser is the only way to see (or add) any.
    void OpenModsFolder() {
        const auto path = title::GetContentsPath(CurrentEntry().app_id);
        if (CurrentEntry().mods_folder) {
            BrowseSdPath(path);
            return;
        }

        App::Push<OptionBox>("This game has no mods folder yet. Create it?"_i18n,
            "Back"_i18n, "Create"_i18n, 1, [this, path](auto op_index){
                if (!op_index || !*op_index) {
                    return;
                }
                const auto rc = fs::FsNativeSd().CreateDirectory(path);
                App::PushErrorBox(rc, "Folder create failed!"_i18n);
                if (R_SUCCEEDED(rc)) {
                    // an empty folder is not a mod: layeredfs stays off until
                    // something is actually copied in (OnFocusGained re-reads
                    // it when the browser is closed).
                    CurrentEntry().mods_folder = true;
                    BrowseSdPath(path);
                }
            });
    }

    static void BrowseSdPath(const fs::FsPath& path) {
        constexpr filebrowser::FsEntry sd{"microSD card", "/", filebrowser::FsType::Sd};
        App::Push<filebrowser::Menu>(MenuFlag_None, sd, path);
    }

    void LoadGame() {
        auto& entry = CurrentEntry();
        LoadControlEntry(entry, true);
        LoadGameSummary(entry);

        m_components.clear();
        m_tickets.clear();
        m_saves.clear();
        m_save_allocated_size = 0;
        m_row_index = 0;
        m_list->SetYoff(0);
        m_display_version[0] = '\0';
        m_languages.clear();
        m_language_list.clear();
        m_language_scroll.Reset();
        for (auto& scroll : m_stat_label_scrolls) {
            scroll.Reset();
        }
        m_save_size = 0;
        m_save_journal_size = 0;

        auto control = std::make_unique<NsApplicationControlData>();
        u64 actual_size{};
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, entry.app_id, control.get(), sizeof(*control), &actual_size))) {
            std::snprintf(m_display_version, sizeof(m_display_version), "%s", control->nacp.display_version);
            m_save_size = control->nacp.user_account_save_data_size;
            m_save_journal_size = control->nacp.user_account_save_data_journal_size;

            constexpr std::array<const char*, 16> language_names{
                "US English", "UK English", "Japanese", "French", "German", "Latin Spanish", "Spanish", "Italian",
                "Dutch", "Canadian French", "Portuguese", "Russian", "Korean", "Traditional Chinese", "Simplified Chinese", "Brazilian Portuguese"
            };
            for (size_t i = 0; i < language_names.size(); i++) {
                if (control->nacp.supported_language_flag & (1U << i)) {
                    if (!m_languages.empty()) m_languages += ", ";
                    m_languages += language_names[i];
                    m_language_list.emplace_back(language_names[i]);
                }
            }
        }

        std::vector<FsRightsId> personalized_ids;
        s32 personalized_count{};
        if (R_SUCCEEDED(es::CountPersonalizedTicket(&personalized_count)) && personalized_count > 0) {
            personalized_ids.resize(personalized_count);
            s32 written{};
            if (R_FAILED(es::ListPersonalizedTicket(&written, personalized_ids.data(), personalized_ids.size()))) {
                personalized_ids.clear();
            } else {
                personalized_ids.resize(written);
            }
        }

        title::MetaEntries entries;
        m_load_result = GetMetaEntries(entry, entries);
        if (R_SUCCEEDED(m_load_result)) {
            for (const auto& status : entries) {
                ContentInfoEntry info;
                if (const auto rc = BuildContentEntry(status, info); R_FAILED(rc)) {
                    m_load_result = rc;
                    continue;
                }

                GameComponentRow component{};
                component.status = status;
                component.content_count = info.content_infos.size();
                component.rights_count = info.ncm_rights_id.size();
                for (const auto& content : info.content_infos) {
                    u64 size{};
                    ncmContentInfoSizeToU64(&content, &size);
                    component.size += size;
                }
                m_components.emplace_back(component);

                for (const auto& rights : info.ncm_rights_id) {
                    const auto duplicate = std::ranges::find_if(m_tickets, [&rights](const auto& ticket){
                        return !std::memcmp(&ticket.id, &rights.rights_id, sizeof(ticket.id));
                    });
                    if (duplicate != m_tickets.end()) continue;

                    GameTicketRow ticket{};
                    ticket.id = rights.rights_id;
                    ticket.key_generation = rights.key_generation;
                    ticket.meta_type = status.meta_type;
                    es::GetCommonTicketSize(&ticket.ticket_size, &ticket.id);
                    ticket.personalized = std::ranges::any_of(personalized_ids, [&ticket](const auto& id){
                        return !std::memcmp(&id, &ticket.id, sizeof(id));
                    });
                    m_tickets.emplace_back(ticket);
                }
            }
        }

        LoadSaves(entry.app_id);
        SetTitleSubHeading(entry.GetName(), true);
        SetSubHeading(std::to_string(m_game_index + 1) + " / " + std::to_string(m_entries->size()));
        SetStorageHighlight(entry.nand_size, entry.sd_size);
    }

    void LoadSaves(u64 app_id) {
        constexpr std::array<FsSaveDataSpaceId, 4> spaces{
            FsSaveDataSpaceId_System, FsSaveDataSpaceId_User,
            FsSaveDataSpaceId_Temporary, FsSaveDataSpaceId_SdUser
        };
        const auto accounts = App::GetAccountList();

        for (const auto space : spaces) {
            FsSaveDataFilter filter{};
            filter.attr.application_id = app_id;
            filter.filter_by_application_id = true;

            FsSaveDataInfoReader reader;
            if (R_FAILED(fsOpenSaveDataInfoReaderWithFilter(&reader, space, &filter))) continue;

            std::array<FsSaveDataInfo, 32> rows{};
            while (true) {
                s64 read{};
                if (R_FAILED(fsSaveDataInfoReaderRead(&reader, rows.data(), rows.size(), &read)) || !read) break;
                for (s64 i = 0; i < read; i++) {
                    const auto& save_info = rows[i];
                    if (save_info.application_id != app_id) continue;
                    if (std::ranges::any_of(m_saves, [&save_info](const auto& row){
                        return row.info.save_data_id == save_info.save_data_id;
                    })) continue;

                    GameSaveRow row{};
                    row.info = save_info;
                    row.account = save::GetSaveTypeLabel(save_info.save_data_type);
                    if (save_info.save_data_type == FsSaveDataType_Account) {
                        const auto account = std::ranges::find_if(accounts, [&save_info](const auto& candidate){
                            return !std::memcmp(&candidate.uid, &save_info.uid, sizeof(save_info.uid));
                        });
                        if (account != accounts.end()) row.account = account->nickname;
                    }
                    m_save_allocated_size += save_info.size;
                    m_saves.emplace_back(std::move(row));
                }
            }
            fsSaveDataInfoReaderClose(&reader);
        }
    }

    // mount this component's NCA files as a read-only fs and open them in the
    // real file browser, so they can be navigated (and later exposed over
    // MTP/FTP) - rather than shown in a throwaway popup.
    void OpenComponentInBrowser(const GameComponentRow& component) {
        filebrowser::FsEntry entry{};
        entry.name = ncm::GetMetaTypeStr(component.status.meta_type);
        entry.root = "/";
        entry.type = filebrowser::FsType::Content;
        entry.flags = filebrowser::FsEntryFlag_ReadOnly;
        entry.content_app_id = component.status.application_id;
        entry.content_meta_type = component.status.meta_type;
        entry.content_storage_id = component.status.storageID;
        App::Push<filebrowser::Menu>(MenuFlag_None, entry, "/");
    }

    void ShowCurrentActions() {
        if (!CurrentCount()) return;
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

            // backup/restore live in the saves menu (with its own locations,
            // compression and WebDAV sync); open it filtered to this game
            // rather than growing a second copy of that machinery here.
            options->Add<SidebarEntryCallback>("Backup and restore"_i18n, [app_id = CurrentEntry().app_id](){
                App::Push<save::Menu>(MenuFlag_None, app_id);
            }, "Open the saves menu showing only this game."_i18n);

            options->Add<SidebarEntryCallback>("Save information"_i18n, [row](){
                char message[512];
                std::snprintf(message, sizeof(message), "Account: %s\nType: %s\nSave ID: %016lX\nSize: %s\nStorage space: %u",
                    row.account.c_str(), save::GetSaveTypeLabel(row.info.save_data_type), row.info.save_data_id,
                    FormatBytes(row.info.size).c_str(), row.info.save_data_space_id);
                App::Push<OptionBox>(message, "OK"_i18n);
            }, "Show the raw save data metadata."_i18n);
        }
    }

    void ShowGameActions() {
        auto options = std::make_unique<Sidebar>("Game Actions"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));
        options->Add<SidebarEntryCallback>("Launch"_i18n, [this](){ LaunchEntry(CurrentEntry()); }, "Launch this game."_i18n)->SetIcon(ActionIcon::Launch);

        const auto& entry = CurrentEntry();

        // only offer the direction that has something to move: a title entirely
        // on one storage has no "move here" to speak of, and one that is split
        // gets both entries plus a breakdown of what each one would do.
        const auto add_move = [this, app_id = entry.app_id, name = entry.GetName(), &options](NcmStorageId target, const std::string& label, const std::string& hint){
            title::MovePlan plan;
            if (R_FAILED(title::GetMovePlan(app_id, target, plan)) || plan.move.empty()) {
                return;
            }

            options->Add<SidebarEntryCallback>(label, [this, app_id, name, target, plan](){
                App::Push<OptionBox>(BuildMoveSummary(plan, target), "Back"_i18n, "Move"_i18n, 1, [this, app_id, name, target](auto op_index){
                    if (!op_index || !*op_index) {
                        return;
                    }

                    App::Push<ProgressBox>(0, MovingToLabel(target), name, [app_id, target](auto pbox) -> Result {
                        DropBoostForMove();
                        return title::MoveApplication(app_id, target, pbox);
                    }, [this, target](Result rc){
                        if (R_SUCCEEDED(rc)) {
                            App::Notify(MovedToLabel(target));
                            LoadGame();
                        } else if (rc != Result_TransferCancelled) {
                            App::PushErrorBox(rc, "Move failed!"_i18n);
                        }
                    }, 1, PRIO_PREEMPTIVE, 1024*128, false);
                });
            }, true, hint)->SetIcon(ActionIcon::Move);
        };

        add_move(NcmStorageId_SdCard, "Move to SD"_i18n, "Move game components from NAND to SD card."_i18n);
        add_move(NcmStorageId_BuiltInUser, "Move to NAND"_i18n, "Move game components from SD card to NAND system memory."_i18n);

        options->Add<SidebarEntryCallback>("Dump all components"_i18n, [this](){
            m_dump_callback(CurrentEntry(), title::ContentFlag_All);
        }, true, "Export base, updates, DLC and data patches."_i18n)->SetIcon(ActionIcon::Dump);
        options->Add<SidebarEntryCallback>("Create repack"_i18n, [this](){
            ShowCreateRepackSidebar();
        }, true, "Export selected installed components as one merged NSP."_i18n)->SetIcon(ActionIcon::Compress);
        options->Add<SidebarEntryCallback>(CurrentEntry().mods_folder ? "Open mods folder"_i18n : "Create mods folder"_i18n, [this](){
            OpenModsFolder();
        }, "LayeredFS uses this Atmosphere folder to replace game files with mods. Creating an empty folder does not install a mod."_i18n)->SetIcon(ActionIcon::Folder);
    }

    void ShowCreateRepackSidebar() {
        const auto& entry = CurrentEntry();
        const auto flags = entry.content_flags;

        const bool has_base = flags & title::ContentFlag_Application;
        const bool has_patch = flags & title::ContentFlag_Patch;
        const bool has_dlc = flags & title::ContentFlag_AddOnContent;

        if (!has_base && !has_patch && !has_dlc) {
            App::Notify("No repackable content available"_i18n);
            return;
        }

        struct State {
            bool base{true};
            bool patch{true};
            bool dlc{true};
        };
        auto state = std::make_shared<State>();

        auto options = std::make_unique<Sidebar>("Create repack"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        if (has_base) {
            options->Add<SidebarEntryCheckbox>("Application/BASE"_i18n, [state](){ return state->base; }, [state](bool val){ state->base = val; }, "Include base application."_i18n);
        }
        if (has_patch) {
            options->Add<SidebarEntryCheckbox>("Patch/Update"_i18n, [state](){ return state->patch; }, [state](bool val){ state->patch = val; }, "Include highest installed update."_i18n);
        }
        if (has_dlc) {
            options->Add<SidebarEntryCheckbox>("AddOnContent/DLC"_i18n, [state](){ return state->dlc; }, [state](bool val){ state->dlc = val; }, "Include all installed DLC."_i18n);
        }

        options->Add<SidebarEntryCallback>("Create repack"_i18n, [this, entry, state, has_base, has_patch, has_dlc](){
            u32 selected_flags = 0;
            if (has_base && state->base) {
                selected_flags |= title::ContentFlag_Application;
            }
            if (has_patch && state->patch) {
                selected_flags |= title::ContentFlag_Patch;
            }
            if (has_dlc && state->dlc) {
                selected_flags |= title::ContentFlag_AddOnContent;
            }

            if (selected_flags == 0) {
                App::Notify("No components selected"_i18n);
                return;
            }

            m_repack_callback(entry, selected_flags);
        }, "Start creating the merged NSP."_i18n);
    }

private:
    std::vector<Entry>* m_entries{};
    s64 m_game_index{};
    s64 m_row_index{};
    Tab m_tab{};
    std::unique_ptr<List> m_list{};
    std::vector<GameComponentRow> m_components{};
    std::vector<GameTicketRow> m_tickets{};
    std::vector<GameSaveRow> m_saves{};
    std::function<void(Entry, u32)> m_dump_callback{};
    std::function<void(Entry, u32)> m_repack_callback{};
    std::function<void(s64)> m_selection_callback{};
    Result m_load_result{};
    char m_display_version[sizeof(NacpStruct::display_version) + 1]{};
    std::string m_languages{};
    PopupList::Items m_language_list{};
    ScrollingText m_language_scroll{};
    std::array<ScrollingText, 10> m_stat_label_scrolls{};
    // focus lives either in the summary block (m_header_index) or in the list.
    bool m_header_focus{};
    bool m_hint_is_header{};
    u8 m_header_index{};
    u64 m_save_size{};
    u64 m_save_journal_size{};
    u64 m_save_allocated_size{};
};

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