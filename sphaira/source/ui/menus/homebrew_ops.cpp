#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/homebrew_internal.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/menus/console_files_transfer.hpp"
#include "files_transfer_plan.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/forwarder_editor.hpp"
#include "nacp_util.hpp"
#include "owo.hpp"
#include "forwarder_auto_install.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"

#include <minIni.h>
#include <cstdio>
#include <optional>
#include <utility>
#include <algorithm>
#include <functional>

namespace sphaira::ui::menu::homebrew {

void Menu::InstallHomebrew() {
    const auto& nro = GetEntry();
    if (R_FAILED(InstallHomebrew(nro.path, nro_get_icon(nro.path, nro.icon_size, nro.icon_offset)))) {
        log_write("failed to create forwarder\n");
    }
}

// rewrites the name and icon inside the nro itself, so the change shows up in
// every launcher, not just here.
void Menu::CustomizeHomebrew() {
    const auto& nro = GetEntry();
    const auto path = nro.path;

    forwarder::Config editor{};
    editor.values.title = nro.GetName();
    editor.values.icon = nro_get_icon(path, nro.icon_size, nro.icon_offset);
    editor.steam_query = editor.values.title;
    editor.screen_title = "Edit name and icon"_i18n;
    editor.submit_label = "Save Changes"_i18n;
    editor.on_create = [weak_alive = std::weak_ptr<bool>(m_alive), this, path](const forwarder::Values& values) {
        const auto alive = weak_alive.lock();
        if (!alive || !*alive) {
            return false;
        }
        const auto title = values.title;
        const auto icon = values.icon;

        App::Push<ProgressBox>(
            0, "Updating Homebrew"_i18n, title,
            [path, title, icon](auto pbox) -> Result {
                pbox->NewTransfer(title);
                return nro_update_info(path, title, icon);
            },
            [weak_alive, this](Result rc) {
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Failed to update the homebrew"_i18n);
                    return;
                }
                const auto alive = weak_alive.lock();
                if (!alive || !*alive) {
                    return;
                }
                App::Notify("Homebrew updated"_i18n);
                SortAndFindLastFile(true);
            }
        );
        return true;
    };

    forwarder::Show(std::move(editor));
}


Result Menu::InstallHomebrew(const fs::FsPath& path, const std::vector<u8>& icon) {
    OwoConfig config{};
    config.nro_path = path.toString();
    R_TRY(nro_get_nacp(path, config.nacp));
    config.icon = icon;

    // the default is a silent install using the settings defaults.
    if (!App::GetForwarderAsk()) {
        return App::Install(config);
    }

    forwarder::Config editor{};
    editor.values.title = nacp_util::GetName(config.nacp);
    editor.values.author = nacp_util::GetAuthor(config.nacp);
    editor.values.version = std::string(config.nacp.display_version, strnlen(config.nacp.display_version, sizeof(config.nacp.display_version)));
    editor.values.icon = icon;
    editor.values.options = App::GetForwarderOptions();
    editor.steam_query = editor.values.title;
    editor.show_author = true;
    editor.show_version = true;
    editor.on_create = [config](const forwarder::Values& values) mutable {
        config.name = values.title;
        config.author = values.author;
        std::snprintf(config.nacp.display_version, sizeof(config.nacp.display_version), "%s", values.version.c_str());
        config.icon = values.icon;
        config.options = values.options;
        return R_SUCCEEDED(App::Install(config));
    };

    forwarder::Show(std::move(editor));
    R_SUCCEED();
}

Result Menu::InstallHomebrewFromPath(const fs::FsPath& path) {
    return InstallHomebrew(path, nro_get_icon(path));
}


void Menu::StarHomebrew(bool star) {
    const auto targets = GetSelectedEntries();
    if (targets.empty()) {
        return;
    }

    fs::FsNativeSd fs;
    size_t count = 0;
    for (const auto& e : targets) {
        if (IsKefirUpdaterStub(e)) {
            continue;
        }
        const auto star_path = GenerateStarPath(e.path);
        const bool exists = fs.FileExists(star_path);
        if (star) {
            if (!exists) {
                auto rc = fs.CreateFile(star_path);
                if (R_SUCCEEDED(rc) || rc == FsError_PathAlreadyExists) {
                    count++;
                }
            }
        } else {
            if (exists) {
                auto rc = fs.DeleteFile(star_path);
                if (R_SUCCEEDED(rc) || rc == FsError_PathNotFound) {
                    count++;
                }
            }
        }
    }

    if (count > 0) {
        if (targets.size() == 1) {
            App::Notify((star ? "Starred "_i18n : "Unstarred "_i18n) + targets.front().GetName());
        } else {
            App::Notify(std::to_string(count) + " " + (star ? "homebrew starred"_i18n : "homebrew unstarred"_i18n));
        }
    }

    ClearSelection();
    SortAndFindLastFile();
}


// Declared in app.cpp.
} // namespace sphaira::ui::menu::homebrew
namespace sphaira { bool IsKefirHubNacp(const NacpStruct& nacp); }
namespace sphaira::ui::menu::homebrew {

auto Menu::OfferDeleteHubCopy(const NroEntry& e) -> bool {
    // Only when Kefir Hub really is /hbmenu.nro (Replace hbmenu on exit): it then starts from
    // there and from its HOME icon, and a second copy among the apps is clutter that starts
    // Hub inside Hub. Never the running file (the exit copy needs it) and never /hbmenu.nro.
    // the list entry keeps only the short NACP: compare the name the same way IsKefirHubNacp does.
    const char* entry_name = e.GetName();
    if (!App::GetReplaceHbmenuEnable() ||
        (std::strcmp(entry_name, "Kefir Hub") != 0 && std::strcmp(entry_name, "sphaira") != 0)) {
        return false;
    }
    if (!strcasecmp(e.path.s, "/hbmenu.nro") || !strcasecmp(e.path.s, App::GetExePath().s)) {
        return false;
    }
    NacpStruct hbmenu_nacp{};
    if (R_FAILED(nro_get_nacp("/hbmenu.nro", hbmenu_nacp)) || !IsKefirHubNacp(hbmenu_nacp)) {
        return false;
    }
    // A HOME tile made for this very file (its title id comes from the path) would stop
    // working: leave the file and say nothing, as before.
    const u64 own_tile_tid = owo_default_title_id(e.path.toString(), e.path.toString());
    bool own_tile = false;
    if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(own_tile_tid, &own_tile)) && own_tile) {
        return false;
    }
    // The HOME icon of Kefir Hub is reinstalled to point at /hbmenu.nro before the delete;
    // that needs the emuMMC (the same rule as the automatic forwarder install).
    if (forwarder_auto::IsHubForwarderInstalled() && !App::IsEmummc()) {
        return false;
    }

    const fs::FsPath path = e.path;
    // Launch stays possible: a second copy can be a debug build the user wants to run.
    App::Push<OptionBox>(
        "Kefir Hub is already installed as your Homebrew Menu.\nThis file is a second copy. Launch it or delete it?"_i18n,
        "Keep"_i18n, "Launch"_i18n, "Delete"_i18n, 1, [this, path](auto op_index){
            if (!op_index || !*op_index) {
                return;
            }
            if (*op_index == 1) {
                nro_launch(path);
                return;
            }
            // a thread: the icon reinstall takes a few seconds. The file is deleted only after
            // the icon is known to point at /hbmenu.nro.
            App::Push<ProgressBox>(0, "Deleting"_i18n, "", [path](auto pbox) -> Result {
                if (!forwarder_auto::PointHubForwarderAtHbmenu()) {
                    R_THROW(Result_FsInvalidType);
                }
                fs::FsNativeSd fs;
                const auto star_path = GenerateStarPath(path);
                if (fs.FileExists(star_path)) {
                    R_TRY(fs.DeleteFile(star_path));
                }
                R_TRY(fs.DeleteFile(path));
                R_SUCCEED();
            }, [this](Result rc){
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Delete failed!"_i18n);
                } else {
                    App::Notify("Delete successful!"_i18n);
                }
                ScanHomebrew();
            });
        }, e.image
    );
    return true;
}

void Menu::DeleteHomebrew(const DeletePlan& plan, bool with_folder) {
    const auto targets = GetSelectedEntries();
    if (targets.empty()) {
        return;
    }

    App::Push<ProgressBox>(0, "Deleting"_i18n, "", [targets, plan, with_folder](auto pbox) -> Result {
        fs::FsNativeSd fs;
        for (size_t i = 0; i < targets.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& e = targets[i];
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, targets.size());

            const auto star_path = GenerateStarPath(e.path);
            if (fs.FileExists(star_path)) {
                R_TRY(fs.DeleteFile(star_path));
            }

            R_TRY(fs.DeleteFile(e.path));
        }
        // the plan exists for one app only: its HOME tiles go with it, its folder when the user chose that
        DeleteTiles(plan);
        if (with_folder && !plan.folder.empty()) {
            R_TRY(fs::DeleteDirectoryRecursively(plan.folder));
        }
        return 0;
    }, [this](Result rc){
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Delete failed!"_i18n);
        } else {
            App::Notify("Delete successful!"_i18n);
        }

        ClearSelection();
        ScanHomebrew();
    });
}


void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    // the actions on the homebrew: first in the list when some are selected, else after the view options
    const auto add_targets = [&]() {
        const auto targets = GetSelectedEntries();
        if (!targets.empty()) {
            fs::FsNativeSd fs;
            bool has_unstarred = false;
            bool has_starred = false;
            for (const auto& t : targets) {
                if (fs.FileExists(GenerateStarPath(t.path))) {
                    has_starred = true;
                } else {
                    has_unstarred = true;
                }
            }

            if (m_selected_count > 0) {
                options->Add<SidebarEntryHeader>("SELECTED HOMEBREW"_i18n,
                    std::to_string(targets.size()) + " " + "selected"_i18n);
            } else {
                options->Add<SidebarEntryHeader>("THIS HOMEBREW"_i18n);
            }

            if (targets.size() == 1 && m_selected_count == 0) {
                options->Add<SidebarEntryCallback>("Edit name and icon"_i18n, [this](){
                    CustomizeHomebrew();
                }, "Change the name and icon stored inside the nro file itself. Affects every launcher, not just this one."_i18n)->SetIcon(ActionIcon::Edit);
            }

            if (has_unstarred) {
                options->Add<SidebarEntryCallback>("Star"_i18n, [this](){
                    StarHomebrew(true);
                }, "Mark as favorite."_i18n)->SetIcon(ActionIcon::Star);
            }

            if (has_starred) {
                options->Add<SidebarEntryCallback>("Unstar"_i18n, [this](){
                    StarHomebrew(false);
                }, "Remove favorite mark."_i18n)->SetIcon(ActionIcon::Star);
            }

            options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
                const auto targets = GetSelectedEntries();
                if (targets.empty()) {
                    return;
                }
                const auto msg = targets.size() == 1
                    ? "Are you sure you want to delete "_i18n + targets.front().GetName() + "?"
                    : "Are you sure you want to delete the selected homebrew?"_i18n;
                if (targets.size() != 1) {
                    App::Push<OptionBox>(
                        msg,
                        "Back"_i18n, "Delete"_i18n, 0, [this](auto op_index){
                            if (op_index && *op_index) {
                                DeleteHomebrew();
                            }
                        }, targets.front().image
                    );
                    return;
                }

                // one app: its own folder and the HOME icons that start it are offered too
                const auto plan = PlanDelete(targets.front().path);
                auto text = msg;
                if (!plan.folder.empty()) {
                    text += "\n\n" + "Its folder:"_i18n + " " + plan.folder.toString() + " (" + grid::FormatBytes(static_cast<u64>(plan.folder_bytes)) + ")";
                }
                if (!plan.tiles.empty()) {
                    text += "\n\n" + "The HOME icon that starts it is deleted too."_i18n;
                }
                if (plan.folder.empty()) {
                    App::Push<OptionBox>(
                        text,
                        "Back"_i18n, "Delete"_i18n, 0, [this, plan](auto op_index){
                            if (op_index && *op_index) {
                                DeleteHomebrew(plan, false);
                            }
                        }, targets.front().image
                    );
                } else {
                    App::Push<OptionBox>(
                        text,
                        "Back"_i18n, "Only the app"_i18n, "App and folder"_i18n, 0, [this, plan](auto op_index){
                            if (op_index && *op_index) {
                                DeleteHomebrew(plan, *op_index == 2);
                            }
                        }, targets.front().image
                    );
                }
            }, true, "Permanently delete all selected homebrew."_i18n)->SetIcon(ActionIcon::Delete);

            options->Add<SidebarEntryCallback>("Send to another console"_i18n, [this](){
                std::vector<std::string> paths;
                for (const auto& t : GetSelectedEntries()) {
                    paths.push_back(files_transfer::HomebrewSendPath(t.path.toString()));
                }
                console_files::Send(std::move(paths));
            }, true, "Send the selected homebrew to another console on the same Wi-Fi. Its whole folder is packed."_i18n);

            if (targets.size() == 1 && m_selected_count == 0) {
                options->Add<SidebarEntryHeader>("FORWARDER"_i18n);

                options->Add<SidebarEntryCallback>("Install Forwarder"_i18n, [this](){
                    App::ConfirmInstall([this](){ InstallHomebrew(); });
                }, "Add this homebrew to the HOME menu as its own tile."_i18n);

                options->Add<SidebarEntryBool>("Ask every time"_i18n, App::GetApp()->m_forwarder_ask,
                    "Open the forwarder editor when creating a forwarder instead of using the defaults from Settings."_i18n);

                options->Add<SidebarEntryCallback>("Forwarder options"_i18n, [](){
                    App::DisplayForwarderOptions(false);
                }, "Defaults baked into forwarders you create."_i18n);
            }
        }
    };

    if (m_selected_count > 0) {
        add_targets();
    }

    options->Add<SidebarEntryHeader>("SORT"_i18n);

    SidebarEntryArray::Items sort_items;
    sort_items.push_back("Updated"_i18n);
    sort_items.push_back("Alphabetical"_i18n);
    sort_items.push_back("Size"_i18n);
    sort_items.push_back("Updated (Star)"_i18n);
    sort_items.push_back("Alphabetical (Star)"_i18n);
    sort_items.push_back("Size (Star)"_i18n);

    SidebarEntryArray::Items order_items;
    order_items.push_back("Descending"_i18n);
    order_items.push_back("Ascending"_i18n);

    // item order matches LayoutType: List, Grid(Icon), GridDetail(Grid), HbMenu.
    SidebarEntryArray::Items layout_items;
    layout_items.push_back("List"_i18n);
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);

    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this, sort_items](s64& index_out){
        m_sort.Set(index_out);
        SortAndFindLastFile();
    }, m_sort.Get(), "Select which field to sort homebrew by."_i18n)->SetIcon(ActionIcon::Sort);

    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this, order_items](s64& index_out){
        m_order.Set(index_out);
        SortAndFindLastFile();
    }, m_order.Get(), "Display entries in Ascending or Descending order."_i18n)->SetIcon(ActionIcon::Sort);

    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_layout.Set(index_out);
        OnLayoutChange();
    }, m_layout.Get(), "Choose how apps are displayed on screen."_i18n)->SetIcon(ActionIcon::Layout);

    options->Add<SidebarEntryBool>("Show Hidden"_i18n, m_show_hidden.Get(), [this](bool& enable){
        m_show_hidden.Set(enable);
        SortAndFindLastFile();
    }, "Shows all hidden homebrew."_i18n)->SetIcon(ActionIcon::Toggle);

    if (m_selected_count == 0) {
        add_targets();
    }

    AddInstallShareOptions(options.get());
    AddSettingsOption(options.get());
}

} // namespace sphaira::ui::menu::homebrew
