#include "ui/menus/game/game_details_internal.hpp"
#include "ui/menus/game_menu.hpp"
#include "forced_language.hpp"
#include "control_patch.hpp"
#include "ui/list.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "yati/nx/es.hpp"
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

void DbiDetailsMenu::ActivateHeaderItem() {
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
            case HeaderItem_Saves: {
                if (m_tab == Tab::Saves) {
                    PromptCreateSaveSlot(CurrentEntry().app_id, CurrentEntry().GetName(), m_saves, [this](){ LoadGame(); });
                } else {
                    SetTab(Tab::Saves);
                }
            } break;
        }
    }

    // the mods folder is what LayeredFS reads replacement game files from;
    // opening it in the file browser is the only way to see (or add) any.
void DbiDetailsMenu::BrowseSdPath(const fs::FsPath& path) {
        constexpr filebrowser::FsEntry sd{"microSD card", "/", filebrowser::FsType::Sd};
        App::Push<filebrowser::Menu>(MenuFlag_None, sd, path);
    }

void DbiDetailsMenu::ShowRestrictions() {
    const auto app_id = CurrentEntry().app_id;
    nacp_patch::State state;
    if (const auto rc = control_patch::ReadState(app_id, state); R_FAILED(rc)) {
        App::PushErrorBox(rc, "Could not read the game's restrictions"_i18n);
        return;
    }
    const auto apply = [app_id](nacp_patch::Patch patch) {
        App::Push<ProgressBox>(0, "Restrictions"_i18n, "", [app_id, patch](auto) -> Result {
            return control_patch::PatchInstalled(app_id, patch);
        }, [](Result rc){
            if (R_SUCCEEDED(rc)) {
                App::Notify("Applies on the next launch"_i18n);
            } else {
                App::PushErrorBox(rc, "Could not change the restrictions"_i18n);
            }
        });
    };
    auto sidebar = std::make_unique<Sidebar>("Restrictions"_i18n, Sidebar::Side::RIGHT);
    sidebar->Add<SidebarEntryBool>("Linked Nintendo Account required"_i18n, state.linked_account_required, [apply](bool& v){
        apply({.linked_account_required = v});
    }, "Off lets the game start without a linked Nintendo Account."_i18n);
    sidebar->Add<SidebarEntryBool>("Screenshots allowed"_i18n, state.screenshots_allowed, [apply](bool& v){
        apply({.screenshots_allowed = v});
    });
    sidebar->Add<SidebarEntryBool>("Video capture allowed"_i18n, state.video_allowed, [apply](bool& v){
        apply({.video_allowed = v});
    }, "Also allows screenshots."_i18n);
    App::Push(std::move(sidebar));
}

auto DbiDetailsMenu::ForcedLanguageText() const -> std::string {
    const auto idx = forced_language::IndexOf(m_forced_language);
    if (idx < 0) {
        return {};
    }
    return "Forced: "_i18n + i18n::get(forced_language::LANGS[idx].name);
}

void DbiDetailsMenu::LoadGame() {
        auto& entry = CurrentEntry();
        LoadControlEntry(entry, true);
        LoadGameSummary(entry);
        // walks the folder, so only for the open card, never for the whole list.
        m_mods_size = entry.layeredfs ? ModsFolderSize(entry.app_id) : 0;

        m_components.clear();
        m_tickets.clear();
        m_saves.clear();
        m_save_allocated_size = 0;
        m_row_index = 0;
        m_list->SetYoff(0);
        m_display_version[0] = '\0';
        m_languages.clear();
        m_language_list.clear();
        m_language_idx.clear();
        m_forced_language = forced_language::Get(entry.app_id);
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
                    m_language_idx.emplace_back(static_cast<int>(i));
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

void DbiDetailsMenu::OpenComponentInBrowser(const GameComponentRow& component) {
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

void DbiDetailsMenu::OpenModsFolder() {
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

void DbiDetailsMenu::ShowCreateRepackSidebar() {
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

void DbiDetailsMenu::ShowGameActions() {
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
        if (!m_language_idx.empty()) {
            options->Add<SidebarEntryCallback>("Force language"_i18n, [this](){
                // "Off" first, then only the languages the game has.
                PopupList::Items items{"Off"_i18n};
                s64 current{};
                for (size_t i = 0; i < m_language_idx.size(); i++) {
                    const auto& lang = forced_language::LANGS[m_language_idx[i]];
                    items.emplace_back(i18n::get(lang.name));
                    if (forced_language::IndexOf(m_forced_language) == m_language_idx[i]) {
                        current = static_cast<s64>(i + 1);
                    }
                }
                App::Push<PopupList>("Force language"_i18n, items, [this](auto op_index){
                    if (!op_index) {
                        return;
                    }
                    const auto code = *op_index ? forced_language::LANGS[m_language_idx[*op_index - 1]].code : "";
                    if (!forced_language::Set(CurrentEntry().app_id, code)) {
                        App::Notify("Could not save the language"_i18n);
                    }
                    m_forced_language = forced_language::Get(CurrentEntry().app_id);
                }, current);
            }, "Start this game in the chosen language instead of the console language. Applies on the next launch."_i18n)->SetIcon(ActionIcon::Edit);
        }
        options->Add<SidebarEntryCallback>("Restrictions"_i18n, [this](){
            ShowRestrictions();
        }, "Linked Nintendo Account, screenshots and video capture. Changing them needs sigpatches."_i18n)->SetIcon(ActionIcon::Edit);
        if (CurrentEntry().layeredfs) {
            options->Add<SidebarEntryCallback>("Delete mods"_i18n, [this](){
                const auto app_id = CurrentEntry().app_id;
                const auto name = CurrentEntry().GetName();
                App::Push<OptionBox>("Delete the mods of "_i18n + name + "?\n" + "Cheats are kept."_i18n,
                    "Back"_i18n, "Delete"_i18n, 0, [this, app_id, name](auto op_index){
                        if (!op_index || !*op_index) {
                            return;
                        }
                        App::Push<ProgressBox>(0, "Deleting"_i18n, name, [app_id](auto) -> Result {
                            return DeleteGameMods(app_id);
                        }, [this](Result rc){
                            App::PushErrorBox(rc, "Delete failed!"_i18n);
                            ProbeModsFolder(CurrentEntry());
                            LoadGame();
                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Mods deleted"_i18n);
                            }
                        });
                    });
            }, true, "Remove this game's mods from the memory card. Cheats are kept."_i18n)->SetIcon(ActionIcon::Delete);
        }
    }


} // namespace sphaira::ui::menu::game
