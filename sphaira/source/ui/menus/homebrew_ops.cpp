#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/homebrew_internal.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/forwarder_editor.hpp"
#include "nacp_util.hpp"
#include "owo.hpp"
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


void Menu::DeleteHomebrew() {
    const auto targets = GetSelectedEntries();
    if (targets.empty()) {
        return;
    }

    App::Push<ProgressBox>(0, "Deleting"_i18n, "", [targets](auto pbox) -> Result {
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
            App::Push<OptionBox>(
                msg,
                "Back"_i18n, "Delete"_i18n, 0, [this](auto op_index){
                    if (op_index && *op_index) {
                        DeleteHomebrew();
                    }
                }, targets.front().image
            );
        }, true, "Permanently delete all selected homebrew."_i18n)->SetIcon(ActionIcon::Delete);

        if (targets.size() == 1 && m_selected_count == 0) {
            options->Add<SidebarEntryHeader>("FORWARDER"_i18n);

            auto entry = options->Add<SidebarEntryCallback>("Install Forwarder"_i18n, [this](){
                InstallHomebrew();
            }, "Add this homebrew to the HOME menu as its own tile."_i18n);
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);

            options->Add<SidebarEntryBool>("Ask every time"_i18n, App::GetApp()->m_forwarder_ask,
                "Open the forwarder editor when creating a forwarder instead of using the defaults from Settings."_i18n);

            options->Add<SidebarEntryCallback>("Forwarder options"_i18n, [](){
                App::DisplayForwarderOptions(false);
            }, "Defaults baked into forwarders you create."_i18n);
        }
    }

    AddInstallShareOptions(options.get());
    AddSettingsOption(options.get());
}

} // namespace sphaira::ui::menu::homebrew
