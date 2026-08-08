#include "ui/menus/filebrowser_forwarder.hpp"
#include "app.hpp"
#include "nacp_util.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "nro.hpp"
#include "owo.hpp"
#include "i18n.hpp"
#include "ui/forwarder_editor.hpp"
#include <cstdio>

namespace sphaira::ui::menu::filebrowser {

using namespace detail;

void ShowRomForwarderEditor(const FileAssocEntry& assoc, const RomDatabaseIndexs& db_indexs, const FileEntry& entry, const fs::FsPath& arg_path) {
    NroEntry nro{};
    NacpStruct nacp{};
    if (R_FAILED(nro_parse(assoc.path, nro)) || R_FAILED(nro_get_nacp(assoc.path, nacp))) {
        App::Notify("Failed to parse nro"_i18n);
        return;
    }

    auto file_name = assoc.use_base_name ? entry.GetName() : entry.GetInternalName();
    if (const auto pos = file_name.find_last_of('.'); pos != std::string::npos) {
        file_name = file_name.substr(0, pos);
    }

    forwarder::Config editor{};
    editor.values.title = nro.nacp.lang.name + std::string{" | "} + file_name;
    editor.values.author = nacp_util::GetAuthor(nacp);
    editor.values.version = nacp.display_version;
    editor.values.icon = GetRomIcon(file_name, db_indexs, nro);
    editor.values.options = App::GetForwarderOptions();
    // search steamgriddb for the rom, not for "core | rom".
    editor.steam_query = file_name;
    editor.icon_source = db_indexs.empty() ? assoc.name : "Boxart"_i18n;
    editor.show_author = true;
    editor.show_version = true;
    editor.show_forwarder_options = App::GetForwarderAsk();

    editor.on_create = [assoc, arg_path, nacp, db_indexs](const forwarder::Values& values) {
        OwoConfig config{};
        config.nro_path = assoc.path.toString();
        config.args = nro_add_arg_file(arg_path);
        config.nacp = nacp;
        config.name = values.title;
        config.author = values.author;
        config.icon = values.icon;
        std::snprintf(config.nacp.display_version, sizeof(config.nacp.display_version), "%s", values.version.c_str());

        if (App::GetForwarderAsk()) {
            config.options = values.options;
        }

        // roms get the intro logo, if the user dropped one in.
        if (!db_indexs.empty()) {
            fs::FsNativeSd().read_entire_file(paths::LOGO + "/rom/NintendoLogo.png", config.logo);
            fs::FsNativeSd().read_entire_file(paths::LOGO + "/rom/StartupMovie.gif", config.gif);
        }

        if (R_FAILED(App::Install(config))) {
            App::Notify("Failed to install forwarder"_i18n);
            return false;
        }

        return true;
    };

    forwarder::Show(std::move(editor));
}

} // namespace sphaira::ui::menu::filebrowser
