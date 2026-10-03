#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "utils/utils.hpp"

#include "ui/menus/game_menu.hpp"
#include "ui/menus/game_list_info.hpp"
#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/game/game_details.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"

#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"

#include <utility>
#include <cstring>
#include <algorithm>
#include <array>
#include <ranges>

namespace sphaira::ui::menu::game {

Menu::Menu(u32 flags) : grid::Menu{"Games"_i18n, flags} {
    this->SetActions(
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){ ToggleCurrentSelection(); }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){ InvertSelection(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
                SetPop();
            }
        }}),
        std::make_pair(Button::A, Action{"Details"_i18n, [this](){
            if (m_entries.empty()) {
                return;
            }
            auto& entry = m_entries[m_index];
            LoadControlEntry(entry, true);
            OpenGameDetails(&m_entries, m_index, [this](Entry entry, u32 flags) mutable {
                std::vector<Entry> targets;
                targets.emplace_back(entry);
                DumpEntries(std::move(targets), flags, false);
            }, [this](Entry entry, u32 flags) {
                CreateRepack(entry, flags);
            }, [this](s64 index) {
                SetIndex(index);
            });
        }}),
        std::make_pair(Button::L3, Action{"Launch"_i18n, [this](){
            if (m_entries.empty()) {
                return;
            }
            LaunchEntry(m_entries[m_index]);
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Game Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            if (m_entries.size()) {
                auto targets = GetSelectedEntries();
                u32 common_flags = title::ContentFlag_All;
                bool all_have_content = !targets.empty();
                for (const auto& target : targets) {
                    const auto entry = std::ranges::find_if(m_entries, [&target](const auto& candidate){
                        return candidate.app_id == target.app_id;
                    });
                    if (entry == m_entries.end()) {
                        all_have_content = false;
                        common_flags = 0;
                        continue;
                    }
                    LoadGameSummary(*entry);
                    if (R_FAILED(entry->summary_result) || !entry->content_flags) {
                        all_have_content = false;
                    }
                    common_flags &= entry->content_flags;
                }

                options->Add<SidebarEntryHeader>("VIEW"_i18n);
                // item order matches LayoutType: List, Grid(Icon), GridDetail(Grid), HbMenu.
                SidebarEntryArray::Items layout_items;
                layout_items.push_back("List"_i18n);
                layout_items.push_back("Icon"_i18n);
                layout_items.push_back("Grid"_i18n);
                layout_items.push_back("HB Menu"_i18n);

                options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
                    m_layout.Set(index_out);
                    OnLayoutChange();
                }, m_layout.Get(), "Choose how games are displayed on screen."_i18n)->SetIcon(ActionIcon::Layout);

                auto sort_entry = options->Add<SidebarEntryCallback>("Sort By"_i18n, [this](){
                    auto options = std::make_unique<Sidebar>("Sort Options"_i18n, Sidebar::Side::RIGHT);
                    ON_SCOPE_EXIT(App::Push(std::move(options)));

                    SidebarEntryArray::Items sort_items;
                    sort_items.push_back("Updated"_i18n);
                    sort_items.push_back("Alphabetical"_i18n);
                    sort_items.push_back("Publisher"_i18n);
                    sort_items.push_back("Storage"_i18n);
                    sort_items.push_back("Last played"_i18n);
                    sort_items.push_back("Play time"_i18n);

                    SidebarEntryArray::Items order_items;
                    order_items.push_back("Descending"_i18n);
                    order_items.push_back("Ascending"_i18n);

                    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
                        m_sort.Set(index_out);
                        SortAndFindLastFile(false);
                    }, m_sort.Get(), "Select which field to sort games by."_i18n)->SetIcon(ActionIcon::Sort);

                    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
                        m_order.Set(index_out);
                        SortAndFindLastFile(false);
                    }, m_order.Get(), "Sort games from newest to oldest or A to Z."_i18n)->SetIcon(ActionIcon::Sort);

                    options->Add<SidebarEntryCallback>("Update play time"_i18n, [this](){
                        LoadPlaytime();
                    }, "Read total play time for every game from the console's play log, then sort by it."_i18n)->SetIcon(ActionIcon::Refresh);

                }, "Change display order for games."_i18n);
                sort_entry->SetIcon(ActionIcon::Sort);
                sort_entry->SetHasSubmenu(true);

                options->Add<SidebarEntryCallback>("Search"_i18n, [this](){
                    SetSearch();
                }, m_search_query.empty()
                    ? "Show only games whose name contains what you type."_i18n
                    : "Searching for: "_i18n + m_search_query)->SetIcon(ActionIcon::Search);

                if (!m_search_query.empty()) {
                    options->Add<SidebarEntryCallback>("Clear search"_i18n, [this](){
                        m_search_query.clear();
                        m_dirty = true;
                    }, "Show every game again."_i18n)->SetIcon(ActionIcon::Delete);
                }

                options->Add<SidebarEntryHeader>("LIBRARY"_i18n);

                options->Add<SidebarEntryBool>("Show unavailable games"_i18n, m_show_unavailable.Get(), [this](bool& v_out){
                    m_show_unavailable.Set(v_out);
                    m_dirty = true;
                }, "Show application records that have no installed content or readable metadata."_i18n)->SetIcon(ActionIcon::Toggle);

                options->Add<SidebarEntryBool>("Hide forwarders"_i18n, m_hide_forwarders.Get(), [this](bool& v_out){
                    m_hide_forwarders.Set(v_out);
                    m_dirty = true;
                }, "Hide game forwarder shortcuts from the list."_i18n)->SetIcon(ActionIcon::Toggle);

                options->Add<SidebarEntryCallback>("Launch random game"_i18n, [this](){
                    const auto random_index = randomGet64() % std::size(m_entries);
                    auto& e = m_entries[random_index];
                    LoadControlEntry(e, true);

                    App::Push<OptionBox>(
                        "Launch "_i18n + e.GetName(),
                        "Back"_i18n, "Launch"_i18n, 1, [this, &e](auto op_index){
                            if (op_index && *op_index) {
                                LaunchEntry(e);
                            }
                        }, e.image
                    );
                }, "Pick and launch a random game from your library."_i18n)->SetIcon(ActionIcon::Random);

                options->Add<SidebarEntryHeader>("SELECTED GAMES"_i18n,
                    std::to_string(targets.size()) + " " + "selected"_i18n);

                if (targets.size() == 1) {
                    options->Add<SidebarEntryCallback>("List meta records"_i18n, [this](){
                    title::MetaEntries meta_entries;
                    const auto target = GetSelectedEntries().front();
                    const auto rc = GetMetaEntries(target, meta_entries);
                    if (R_FAILED(rc)) {
                        App::Push<ui::ErrorBox>(rc,
                            i18n::get("Failed to list application meta entries")
                        );
                        return;
                    }

                    if (meta_entries.empty()) {
                        App::Notify("No meta entries found...\n"_i18n);
                        return;
                    }

                    PopupList::Items items;
                    for (auto& e : meta_entries) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "Type: %s Storage: %s [%016lX][v%u]", ncm::GetMetaTypeStr(e.meta_type), ncm::GetStorageIdStr(e.storageID), e.application_id, e.version);
                        items.emplace_back(buf);
                    }

                    App::Push<PopupList>(
                        "Entries"_i18n, items, [this, meta_entries](auto op_index){
                            #if 0
                            if (op_index) {
                                const auto& e = meta_entries[*op_index];
                            }
                            #endif
                        }
                    );
                    }, "Show all installed content meta records for the selected game."_i18n)->SetIcon(ActionIcon::Edit);
                }

                if (all_have_content) {
                    auto dump_entry = options->Add<SidebarEntryCallback>("Dump"_i18n, [this, common_flags](){
                    auto options = std::make_unique<Sidebar>("Select content to dump"_i18n, Sidebar::Side::RIGHT);
                    ON_SCOPE_EXIT(App::Push(std::move(options)));

                    options->Add<SidebarEntryCallback>("Dump All"_i18n, [this](){
                        DumpGames(title::ContentFlag_All);
                    }, true, "Dump all content: base game, updates, and DLC."_i18n)->SetIcon(ActionIcon::Dump);
                    if (common_flags & title::ContentFlag_Application) {
                        options->Add<SidebarEntryCallback>("Dump Application"_i18n, [this](){
                            DumpGames(title::ContentFlag_Application);
                        }, true, "Dump the base application NSP only."_i18n)->SetIcon(ActionIcon::Dump);
                    }
                    if (common_flags & title::ContentFlag_Patch) {
                        options->Add<SidebarEntryCallback>("Dump Patch"_i18n, [this](){
                            DumpGames(title::ContentFlag_Patch);
                        }, true, "Dump the game update/patch NSP only."_i18n)->SetIcon(ActionIcon::Dump);
                    }
                    if (common_flags & title::ContentFlag_AddOnContent) {
                        options->Add<SidebarEntryCallback>("Dump AddOnContent"_i18n, [this](){
                            DumpGames(title::ContentFlag_AddOnContent);
                        }, true, "Dump downloadable content (DLC) NSP only."_i18n)->SetIcon(ActionIcon::Dump);
                    }
                    if (common_flags & title::ContentFlag_DataPatch) {
                        options->Add<SidebarEntryCallback>("Dump DataPatch"_i18n, [this](){
                            DumpGames(title::ContentFlag_DataPatch);
                        }, true, "Dump data patch NSP only."_i18n)->SetIcon(ActionIcon::Dump);
                    }
                    }, true, "Export content shared by all selected games as NSP files."_i18n);
                    dump_entry->SetIcon(ActionIcon::Dump);
                    dump_entry->SetHasSubmenu(true);
                }

                // same rule as the single-game menu: a direction is only offered
                // when at least one selected title actually has something there
                // to move.
                const auto add_move = [this, targets, &options](NcmStorageId target_storage, const std::string& label, const std::string& hint){
                    const auto movable = std::ranges::any_of(targets, [target_storage](const auto& e){
                        title::MetaEntries meta;
                        title::GetMetaEntries(e.app_id, meta);
                        return std::ranges::any_of(meta, [target_storage](const auto& s){
                            return s.storageID != target_storage &&
                                (s.storageID == NcmStorageId_SdCard || s.storageID == NcmStorageId_BuiltInUser);
                        });
                    });

                    if (!movable) {
                        return;
                    }

                    options->Add<SidebarEntryCallback>(label, [this, targets, target_storage](){
                        App::Push<ProgressBox>(0, MovingToLabel(target_storage), "", [targets, target_storage](auto pbox) -> Result {
                            DropBoostForMove();
                            for (const auto& target : targets) {
                                R_TRY(pbox->ShouldExitResult());
                                pbox->SetTitle(target.GetName());
                                R_TRY(title::MoveApplication(target.app_id, target_storage, pbox));
                            }
                            return 0;
                        }, [this, target_storage](Result rc){
                            m_dirty = true;
                            if (R_SUCCEEDED(rc)) {
                                App::Notify(MovedToLabel(target_storage));
                            } else if (rc != Result_TransferCancelled) {
                                App::PushErrorBox(rc, "Move failed!"_i18n);
                            }
                        }, 1, PRIO_PREEMPTIVE, 1024*128, false);
                    }, true, hint)->SetIcon(ActionIcon::Move);
                };

                add_move(NcmStorageId_SdCard, "Move to SD"_i18n, "Move all selected games to SD card."_i18n);
                add_move(NcmStorageId_BuiltInUser, "Move to NAND"_i18n, "Move all selected games to NAND system memory."_i18n);

                options->Add<SidebarEntryCallback>("Create mods folders"_i18n, [this](){
                    CreateContentsFolders();
                }, "Create Atmosphere LayeredFS folders for all selected games."_i18n)->SetIcon(ActionIcon::Folder);

                options->Add<SidebarEntryCallback>("Create save"_i18n, [this](){
                    ui::PopupList::Items items{};
                    const auto accounts = App::GetAccountList();
                    for (auto& p : accounts) {
                        items.emplace_back(p.nickname);
                    }

                    App::Push<ui::PopupList>(
                        "Select user to create save for"_i18n, items, [this, accounts](auto op_index){
                            if (op_index) {
                                CreateSaves(accounts[*op_index].uid);
                            }
                        }
                    );
                }, "Manually create save data entries for all selected games."_i18n)->SetIcon(ActionIcon::Save);

                // completely deletes the application record and all data.
                options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
                    const auto targets = GetSelectedEntries();
                    const auto buf = targets.size() == 1
                        ? "Are you sure you want to delete "_i18n + targets.front().GetName() + "?"
                        : "Are you sure you want to delete the selected games?"_i18n;
                    const bool has_mods = std::ranges::any_of(targets, [](auto e){ ProbeModsFolder(e); return e.layeredfs; });
                    const auto on_pick = [this](auto op_index){
                        if (op_index && *op_index) {
                            DeleteGames(*op_index == 2);
                        }
                    };
                    if (has_mods) {
                        App::Push<OptionBox>(buf, "Back"_i18n, "Delete"_i18n, "Delete with mods"_i18n, 0, on_pick, targets.front().image);
                    } else {
                        App::Push<OptionBox>(buf, "Back"_i18n, "Delete"_i18n, 0, on_pick, targets.front().image);
                    }
                }, true, "Permanently delete all selected games and their data."_i18n)->SetIcon(ActionIcon::Delete);
            }

            auto advanced_entry = options->Add<SidebarEntryCallback>("Advanced options"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Dump options"_i18n, [this](){
                    App::DisplayDumpOptions(false);
                }, "Configure dump output settings such as folder structure and ticket handling."_i18n)->SetIcon(ActionIcon::Dump);

                options->Add<SidebarEntryCallback>("Refresh"_i18n, [this](){
                    m_dirty = true;
                    App::PopToMenu();
                }, "Rescan the game library and reload the list."_i18n)->SetIcon(ActionIcon::Refresh);

                options->Add<SidebarEntryBool>("Title cache"_i18n, m_title_cache.Get(), [this](bool& v_out){
                    m_title_cache.Set(v_out);
                }, "Cache game names and icons to speed up loading."_i18n)->SetIcon(ActionIcon::Save);

                options->Add<SidebarEntryCallback>("Delete title cache"_i18n, [this](){
                    App::Push<OptionBox>(
                        "Are you sure you want to delete the title cache?"_i18n,
                        "Back"_i18n, "Delete"_i18n, 0, [this](auto op_index){
                            if (op_index && *op_index) {
                                m_dirty = true;
                                title::Clear();
                                App::PopToMenu();
                            }
                        }
                    );
                }, "Clear cached game metadata to force a fresh reload."_i18n)->SetIcon(ActionIcon::Delete);
            }, "Access developer and maintenance tools."_i18n);
            advanced_entry->SetIcon(ActionIcon::Edit);
            advanced_entry->SetHasSubmenu(true);
        }})
    );

    OnLayoutChange();

    nsInitialize();
    es::Initialize();
    title::Init();
    // play statistics are a nice-to-have: if pdm is unavailable the two
    // playtime sorts simply have nothing to sort by.
    m_pdm_initialized = R_SUCCEEDED(pdmqryInitialize());
}

Menu::~Menu() {
    title::Exit();

    if (m_pdm_initialized) {
        pdmqryExit();
    }

    FreeEntries();
    nsExit();
    es::Exit();
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_dirty) {
        App::Notify("Updating application record list"_i18n);
        SortAndFindLastFile(true);
    }

    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    if (m_layout.Get() == grid::LayoutType_HbMenu) {
        auto& e = m_entries[m_index];
        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", e.app_id);
        DrawHbMenuHeader(vg, theme, e.image, e.GetName(), e.GetAuthor(), title_id, e.GetAuthor());
        const Vec4 header_cover{80.f, 120.f, 200.f, 200.f};
        if (e.on_gamecard) {
            DrawGameCardOutline(vg, header_cover);
        }
        DrawGameBadges(vg, theme, header_cover, e);
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;
    int summary_load_count = 0;

    // LoadGameSummary() is ns/ncm i/o and it runs here, on the render thread,
    // one entry per frame until every row has been sized. While a transfer has
    // the storage busy each of those calls queues behind its reads, so a frame
    // that issues one can take far longer than a frame should. The sizes are
    // not urgent - they resume once the transfer ends.
    const bool storage_busy = App::GetProgressActive();

    m_list->Draw(vg, theme, m_entries.size(), m_index, [this, storage_busy, &image_load_count, &summary_load_count](auto* vg, auto* theme, auto v, auto pos) {
        auto& e = m_entries[pos];

        if (e.status == title::NacpLoadStatus::None) {
            title::PushAsync(e.app_id);
            e.status = title::NacpLoadStatus::Progress;
        } else if (e.status == title::NacpLoadStatus::Progress) {
            LoadResultIntoEntry(e, title::GetAsync(e.app_id));
        }

        // lazy load image
        if (image_load_count < image_load_max) {
            if (LoadControlImage(e, title::GetAsync(e.app_id))) {
                image_load_count++;
            }
        }

        if (!storage_busy && !e.summary_attempted && summary_load_count < 1) {
            LoadGameSummary(e);
            summary_load_count++;
            if (pos == m_index && !m_selected_count) {
                SetStorageHighlight(e.nand_size, e.sd_size);
            }
        }

        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", e.app_id);

        const auto layout = m_layout.Get();
        const auto selected = pos == m_index;
        // list rows have room for the same badge pills as the grid (plus SD/NAND);
        // the right column is only the size, badges are painted beside it.
        std::string list_info;
        float extra_right = 0.f;
        const char* version = title_id;
        if (layout == grid::LayoutType_List) {
            const auto bytes = e.nand_size + e.sd_size + e.gc_size;
            list_info = bytes ? grid::FormatBytes(bytes) : std::string{};
            version = list_info.c_str();
            extra_right = MeasureListBadges(vg, e);
        }
        const auto image_v = DrawEntry(vg, theme, layout, v, selected, e.image, e.GetName(), e.GetAuthor(), version, e.selected, extra_right);
        Vec4 cover_v = image_v;
        if (layout == grid::LayoutType_HbMenu) {
            cover_v.y += 28.f;
            cover_v.h -= 28.f;
        }
        if (e.on_gamecard) {
            DrawGameCardOutline(vg, cover_v);
        }
        if (layout == grid::LayoutType_List) {
            DrawListBadges(vg, v, e, !list_info.empty());
        } else {
            DrawGameBadges(vg, theme, cover_v, e);
        }

        DrawSelectionMark(vg, theme, layout, v, v, e.selected, m_selected_count > 0);
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        ScanHomebrew();
        return;
    }
    AppendGameCardEntries();
    SortAndFindLastFile(false);
}

void Menu::SetIndex(s64 index) {
    if (m_entries.empty()) {
        m_index = 0;
        SetTitleSubHeading("");
        SetSubHeading("0 / 0");
        ClearStorageHighlight();
        return;
    }

    index = std::clamp<s64>(index, 0, static_cast<s64>(m_entries.size()) - 1);
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    char title_id[33];
    std::snprintf(title_id, sizeof(title_id), "%016lX", m_entries[m_index].app_id);
    SetTitleSubHeading(title_id);
    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries.size()));

    auto& entry = m_entries[m_index];
    LoadGameSummary(entry);
    UpdateStorageHighlight();
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
    SetIndex(0);
}

void Menu::ToggleCurrentSelection() {
    if (m_entries.empty()) {
        return;
    }

    auto& entry = m_entries[m_index];
    entry.selected ^= 1;
    m_selected_count += entry.selected ? 1 : -1;

    // step onto the next row, the way the file browser does, so a run of
    // entries can be ticked without moving the cursor by hand in between.
    if (m_index + 1 < static_cast<s64>(m_entries.size())) {
        SetIndex(m_index + 1);
        m_list->EnsureVisible(m_index, m_entries.size());
        return;
    }

    UpdateStorageHighlight();
}

void Menu::InvertSelection() {
    m_selected_count = 0;
    for (auto& entry : m_entries) {
        entry.selected ^= 1;
        if (entry.selected) {
            m_selected_count++;
        }
    }
    UpdateStorageHighlight();
}

void Menu::UpdateStorageHighlight() {
    if (m_entries.empty()) {
        ClearStorageHighlight();
        return;
    }

    u64 nand_size{};
    u64 sd_size{};
    if (m_selected_count) {
        for (auto& entry : m_entries) {
            if (!entry.selected) {
                continue;
            }
            LoadGameSummary(entry);
            nand_size += entry.nand_size;
            sd_size += entry.sd_size;
        }
    } else {
        auto& entry = m_entries[m_index];
        LoadGameSummary(entry);
        nand_size = entry.nand_size;
        sd_size = entry.sd_size;
    }

    SetStorageHighlight(nand_size, sd_size);
}

} // namespace sphaira::ui::menu::game