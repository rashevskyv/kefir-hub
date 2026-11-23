// Game actions shared by the Games list (Game Options) and Game details (Game Actions).
#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/game/game_edit.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "repack/repack.hpp"
#include "romfs_pack.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "yati/nx/ncm.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace sphaira::ui::menu::game {
namespace {

void BrowseSd(const fs::FsPath& path) {
    constexpr filebrowser::FsEntry sd{"microSD card", "/", filebrowser::FsType::Sd};
    App::Push<filebrowser::Menu>(MenuFlag_None, sd, path);
}

auto Find(std::vector<Entry>* entries, u64 app_id) -> Entry* {
    const auto it = std::ranges::find_if(*entries, [app_id](const auto& e){ return e.app_id == app_id; });
    return it == entries->end() ? nullptr : &*it;
}

} // namespace

auto GameLanguages(u64 app_id) -> std::vector<int> {
    std::vector<int> out;
    auto control = std::make_unique<NsApplicationControlData>();
    u64 size{};
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control.get(), sizeof(*control), &size))) {
        for (int i = 0; i < 16; i++) {
            if (control->nacp.supported_language_flag & (1U << i)) {
                out.push_back(i);
            }
        }
    }
    return out;
}

void OpenGameModsFolder(Entry& entry) {
    const auto path = title::GetContentsPath(entry.app_id);
    if (entry.mods_folder) {
        BrowseSd(path);
        return;
    }
    App::Push<OptionBox>("This game has no mods folder yet. Create it?"_i18n,
        "Back"_i18n, "Create"_i18n, 1, [&entry, path](auto op_index){
            if (!op_index || !*op_index) {
                return;
            }
            const auto rc = fs::FsNativeSd().CreateDirectory(path);
            App::PushErrorBox(rc, "Folder create failed!"_i18n);
            if (R_SUCCEEDED(rc)) {
                // an empty folder is not a mod: layeredfs stays off until something is copied in.
                entry.mods_folder = true;
                BrowseSd(path);
            }
        });
}

void ConfirmDeleteMods(u64 app_id, const std::string& name, std::function<void()> on_done) {
    App::Push<OptionBox>("Delete the mods of "_i18n + name + "?\n" + "Cheats are kept."_i18n,
        "Back"_i18n, "Delete"_i18n, 0, [app_id, name, on_done](auto op_index){
            if (!op_index || !*op_index) {
                return;
            }
            App::Push<ProgressBox>(0, "Deleting"_i18n, name, [app_id](auto) -> Result {
                return DeleteGameMods(app_id);
            }, [on_done](Result rc){
                App::PushErrorBox(rc, "Delete failed!"_i18n);
                on_done();
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Mods deleted"_i18n);
                }
            });
        });
}

void ShowRepackOptions(const Entry& entry, std::function<void(Entry, u32)> on_create) {
    const auto flags = entry.content_flags;
    const bool has_base = flags & title::ContentFlag_Application;
    const bool has_patch = flags & title::ContentFlag_Patch;
    const bool has_dlc = flags & title::ContentFlag_AddOnContent;
    const bool has_mod = repack::HasRomfsMod(entry.app_id);

    if (!has_base && !has_patch && !has_dlc) {
        App::Notify("No repackable content available"_i18n);
        return;
    }

    struct State {
        bool base{true};
        bool patch{true};
        bool dlc{true};
        bool mod{true};
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
    if (has_mod) {
        options->Add<SidebarEntryCheckbox>("Bake in the mod"_i18n, [state](){ return state->mod; }, [state](bool val){ state->mod = val; }, "Put the LayeredFS mod of this game (romfs folder or romfs.bin) into the update, or into the base when the update is left out. The repack then installs only with sigpatches."_i18n);
    }

    options->Add<SidebarEntryCallback>("Create repack"_i18n, [entry, state, has_base, has_patch, has_dlc, has_mod, on_create](){
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
        if (has_mod && state->mod) {
            if (!(selected_flags & (title::ContentFlag_Application | title::ContentFlag_Patch))) {
                App::Notify("The mod needs the base or the update in the repack"_i18n);
                return;
            }
            selected_flags |= repack::FLAG_WITH_MOD;
        }

        on_create(entry, selected_flags);
    }, "Start creating the merged NSP."_i18n);
}

void ShowMetaRecords(const Entry& entry) {
    title::MetaEntries meta_entries;
    if (const auto rc = GetMetaEntries(entry, meta_entries); R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to list application meta entries"_i18n);
        return;
    }
    if (meta_entries.empty()) {
        App::Notify("No meta entries found...\n"_i18n);
        return;
    }
    PopupList::Items items;
    for (const auto& e : meta_entries) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Type: %s Storage: %s [%016lX][v%u]", ncm::GetMetaTypeStr(e.meta_type), ncm::GetStorageIdStr(e.storageID), e.application_id, e.version);
        items.emplace_back(buf);
    }
    App::Push<PopupList>("Entries"_i18n, items, [](auto){});
}

void ShowCreateSave(u64 app_id, std::function<void()> on_done) {
    PopupList::Items items;
    const auto accounts = App::GetAccountList();
    for (const auto& p : accounts) {
        items.emplace_back(p.nickname);
    }
    App::Push<PopupList>("Select user to create save for"_i18n, items, [app_id, accounts, on_done](auto op_index){
        if (!op_index) {
            return;
        }
        App::Push<ProgressBox>(0, "Creating"_i18n, "", [app_id, uid = accounts[*op_index].uid](auto) -> Result {
            const auto rc = CreateSave(app_id, uid);
            // an existing save is not an error.
            return rc == FsError_PathAlreadyExists ? 0 : rc;
        }, [on_done](Result rc){
            App::PushErrorBox(rc, "Save create failed!"_i18n);
            save::SignalChange();
            on_done();
            if (R_SUCCEEDED(rc)) {
                App::Notify("Save create successful!"_i18n);
            }
        });
    });
}

void ConfirmArchiveGame(const Entry& entry, std::function<void()> on_done) {
    App::Push<OptionBox>("Are you sure you want to archive "_i18n + entry.GetName() + "?",
        "Back"_i18n, "Archive"_i18n, 0, [app_id = entry.app_id, on_done](auto op_index){
            if (!op_index || !*op_index) {
                return;
            }
            App::Push<ProgressBox>(0, "Archiving"_i18n, "", [app_id](auto) -> Result {
                return nsDeleteApplicationEntity(app_id);
            }, [on_done](Result rc){
                App::PushErrorBox(rc, "Archive failed!"_i18n);
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Archive successful!"_i18n);
                    on_done();
                }
            });
        }, entry.image);
}

void ConfirmDeleteGame(Entry entry, std::function<void()> on_done) {
    ProbeModsFolder(entry);
    const auto buf = "Are you sure you want to delete "_i18n + entry.GetName() + "?";
    const auto on_pick = [app_id = entry.app_id, with_mods_ok = entry.layeredfs, on_done](auto op_index){
        if (!op_index || !*op_index) {
            return;
        }
        const bool with_mods = with_mods_ok && *op_index == 2;
        App::Push<ProgressBox>(0, "Deleting"_i18n, "", [app_id, with_mods](auto) -> Result {
            R_TRY(DeleteApplicationKeepSave(app_id));
            if (with_mods) {
                R_TRY(DeleteGameMods(app_id));
            }
            R_SUCCEED();
        }, [on_done](Result rc){
            App::PushErrorBox(rc, "Delete failed!"_i18n);
            if (R_SUCCEEDED(rc)) {
                App::Notify("Delete successful!"_i18n);
                on_done();
            }
        });
    };
    if (entry.layeredfs) {
        App::Push<OptionBox>(buf, "Back"_i18n, "Delete"_i18n, "Delete with mods"_i18n, 0, on_pick, entry.image);
    } else {
        App::Push<OptionBox>(buf, "Back"_i18n, "Delete"_i18n, 0, on_pick, entry.image);
    }
}

void AddGameToolEntries(Sidebar* options, std::vector<Entry>* entries, u64 app_id,
    std::function<void(Entry, u32)> on_repack, std::function<void()> on_changed) {
    Entry* const e = Find(entries, app_id);
    if (!e) {
        return;
    }
    ProbeModsFolder(*e);
    const auto name = e->GetName();

    options->Add<SidebarEntryCallback>("Create repack"_i18n, [entries, app_id, on_repack](){
        if (auto* e = Find(entries, app_id)) {
            ShowRepackOptions(*e, on_repack);
        }
    }, true, "Export selected installed components as one merged NSP."_i18n)->SetIcon(ActionIcon::Compress);
    options->Add<SidebarEntryCallback>("Compress game"_i18n, [app_id, name, on_changed](){
        ShowCompressGame(app_id, name, on_changed);
    }, true, "Estimate the saving, then store the game data LZ4-compressed. The game plays the same and takes less space."_i18n)->SetIcon(ActionIcon::Compress);
    options->Add<SidebarEntryCallback>(e->mods_folder ? "Open mods folder"_i18n : "Create mods folder"_i18n, [entries, app_id](){
        if (auto* e = Find(entries, app_id)) {
            OpenGameModsFolder(*e);
        }
    }, "LayeredFS uses this Atmosphere folder to replace game files with mods. Creating an empty folder does not install a mod."_i18n)->SetIcon(ActionIcon::Folder);
    if (e->layeredfs && romfs_pack::CanPack(title::GetContentsPath(app_id))) {
        options->Add<SidebarEntryCallback>("Pack into romfs.bin"_i18n, [app_id, on_changed](){
            romfs_pack::PackTitles({title::GetContentsPath(app_id)}, on_changed);
        }, true, "Pack the mod's romfs folder into one romfs.bin file. Atmosphere then reads one file instead of thousands."_i18n)->SetIcon(ActionIcon::Compress);
    }
    if (e->layeredfs) {
        options->Add<SidebarEntryCallback>("Delete mods"_i18n, [app_id, name, on_changed](){
            ConfirmDeleteMods(app_id, name, on_changed);
        }, true, "Remove this game's mods from the memory card. Cheats are kept."_i18n)->SetIcon(ActionIcon::Delete);
    }
    if (auto langs = GameLanguages(app_id); !langs.empty()) {
        options->Add<SidebarEntryCallback>("Force language"_i18n, [app_id, langs](){
            ShowGameLanguage(app_id, langs);
        }, "Start this game in the chosen language instead of the console language. Applies on the next launch."_i18n)->SetIcon(ActionIcon::Edit);
    }
    options->Add<SidebarEntryCallback>("Restrictions"_i18n, [app_id](){
        ShowGameRestrictions(app_id);
    }, "Linked Nintendo Account, screenshots and video capture. Changing them needs sigpatches."_i18n)->SetIcon(ActionIcon::Edit);
    options->Add<SidebarEntryCallback>("List meta records"_i18n, [entries, app_id](){
        if (auto* e = Find(entries, app_id)) {
            ShowMetaRecords(*e);
        }
    }, "Show all installed content meta records for the selected game."_i18n)->SetIcon(ActionIcon::Edit);
}

} // namespace sphaira::ui::menu::game
