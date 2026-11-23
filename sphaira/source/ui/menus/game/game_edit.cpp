#include "ui/menus/game/game_edit.hpp"
#include "ui/menus/game/game_internal.hpp"
#include "ui/forwarder_editor.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "control_patch.hpp"
#include "forced_language.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "log.hpp"

#include <memory>
#include <string>

namespace sphaira::ui::menu::game {
namespace {

void ReloadEntry(std::vector<Entry>* entries, u64 app_id) {
    title::Forget(app_id);
    for (auto& e : *entries) {
        if (e.app_id == app_id) {
            FreeEntry(App::GetVg(), e);
            e.status = title::NacpLoadStatus::None;
            LoadControlEntry(e, true);
        }
    }
}

auto LanguageText(u64 app_id) -> std::string {
    const auto idx = forced_language::IndexOf(forced_language::Get(app_id));
    return idx < 0 ? "Console language"_i18n : i18n::get(forced_language::LANGS[idx].name);
}

} // namespace

void ShowGameLanguage(u64 app_id, std::vector<int> langs, std::function<void()> on_done) {
    // "Off" first, then only the languages the game has.
    PopupList::Items items{"Off"_i18n};
    s64 current{};
    const auto forced = forced_language::IndexOf(forced_language::Get(app_id));
    for (size_t i = 0; i < langs.size(); i++) {
        items.emplace_back(i18n::get(forced_language::LANGS[langs[i]].name));
        if (forced == langs[i]) {
            current = static_cast<s64>(i + 1);
        }
    }
    App::Push<PopupList>("Force language"_i18n, items, [app_id, langs, on_done](auto op_index){
        if (!op_index) {
            return;
        }
        const auto code = *op_index ? forced_language::LANGS[langs[*op_index - 1]].code : "";
        if (!forced_language::Set(app_id, code)) {
            App::Notify("Could not save the language"_i18n);
        }
        if (on_done) {
            on_done();
        }
    }, current);
}

void ShowGameRestrictions(u64 app_id) {
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

void ShowEditGame(std::vector<Entry>* entries, u64 app_id) {
    auto control = std::make_unique<NsApplicationControlData>();
    u64 size{};
    if (const auto rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control.get(), sizeof(*control), &size);
        R_FAILED(rc) || size < sizeof(NacpStruct)) {
        App::PushErrorBox(R_FAILED(rc) ? rc : Result_NcaBadMagic, "Could not read the game's control data"_i18n);
        return;
    }
    const auto state = nacp_patch::ReadState({reinterpret_cast<const u8*>(&control->nacp), sizeof(NacpStruct)});
    std::vector<int> langs;
    for (int i = 0; i < 16; i++) {
        if (control->nacp.supported_language_flag & (1U << i)) {
            langs.push_back(i);
        }
    }

    forwarder::Config config;
    for (const auto& e : *entries) {
        if (e.app_id == app_id) {
            config.values.title = e.GetName();
            config.values.author = e.GetAuthor();
        }
    }
    config.values.version = state.version;
    config.values.icon.assign(control->icon, control->icon + (size - sizeof(NacpStruct)));
    config.icon_source = "Current icon"_i18n;
    // a changed icon: "Default icon" puts the game's own one back (it is saved with Save).
    std::ignore = control_patch::ReadOriginalIcon(app_id, config.default_icon);
    config.screen_title = "Edit name and icon"_i18n;
    config.title_label = "Name"_i18n;
    config.submit_label = "Save"_i18n;
    config.show_author = true;
    config.show_version = true;
    config.show_forwarder_options = false;
    if (!langs.empty()) {
        config.links.push_back({"Language"_i18n, [app_id]{ return LanguageText(app_id); },
            [app_id, langs]{ ShowGameLanguage(app_id, langs); },
            "Start this game in the chosen language instead of the console language. Applies on the next launch."_i18n});
    }
    config.links.push_back({"Restrictions"_i18n, {}, [app_id]{ ShowGameRestrictions(app_id); },
        "Linked Nintendo Account, screenshots and video capture. Changing them needs sigpatches."_i18n});

    config.on_create = [entries, app_id, before = config.values, editable = state.titles_editable](const forwarder::Values& v) -> bool {
        nacp_patch::Patch p;
        const bool title_changed = v.title != before.title || v.author != before.author;
        if (title_changed && !editable) {
            App::Notify("This game keeps its name in a newer format: only the icon and version change"_i18n);
        } else {
            if (v.title != before.title) p.name = v.title;
            if (v.author != before.author) p.author = v.author;
        }
        if (v.version != before.version) p.version = v.version;
        if (v.icon_changed) {
            // the icon slot of a Control NCA is as big as the icon the game came with.
            p.icon = [jpeg = v.icon](std::size_t max_size){ return ImageFitJpeg(jpeg, max_size); };
        }
        if (!p.EditsTitle()) {
            return true;
        }
        App::Push<ProgressBox>(0, "Saving"_i18n, v.title, [app_id, p](auto) -> Result {
            return control_patch::PatchInstalled(app_id, p);
        }, [entries, app_id](Result rc){
            ReloadEntry(entries, app_id);
            if (R_SUCCEEDED(rc)) {
                App::Notify("Saved"_i18n);
            } else if (rc == Result_IconTooLarge) {
                App::PushErrorBox(rc, "This picture does not fit in the game's icon slot, even at low quality. Try a simpler picture."_i18n);
            } else {
                App::PushErrorBox(rc, "Could not change the game"_i18n);
            }
        });
        return true;
    };
    forwarder::Show(std::move(config));
}

void AddEditGameEntries(Sidebar* options, std::vector<Entry>* entries, u64 app_id) {
    options->Add<SidebarEntryCallback>("Edit name and icon"_i18n, [entries, app_id](){
        ShowEditGame(entries, app_id);
    }, "Name, publisher, version and icon, also in the HOME menu; language and restrictions. Needs sigpatches."_i18n)->SetIcon(ActionIcon::Edit);
    if (control_patch::HasOriginal(app_id)) {
        options->Add<SidebarEntryCallback>("Restore original name and icon"_i18n, [entries, app_id](){
            ShowRestoreOriginal(entries, app_id);
        }, "Put back what the game came with. Restrictions stay as they are."_i18n)->SetIcon(ActionIcon::Refresh);
    }
}

void ShowRestoreOriginal(std::vector<Entry>* entries, u64 app_id) {
    App::Push<OptionBox>("Put back the game's own name, publisher, version and icon?"_i18n,
        "Back"_i18n, "Restore"_i18n, 0, [entries, app_id](auto op_index){
            if (!op_index || !*op_index) {
                return;
            }
            App::Push<ProgressBox>(0, "Restoring"_i18n, "", [app_id](auto) -> Result {
                return control_patch::RestoreOriginal(app_id);
            }, [entries, app_id](Result rc){
                ReloadEntry(entries, app_id);
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Restored"_i18n);
                } else {
                    App::PushErrorBox(rc, "Could not restore the game"_i18n);
                }
            });
        });
}

} // namespace sphaira::ui::menu::game
