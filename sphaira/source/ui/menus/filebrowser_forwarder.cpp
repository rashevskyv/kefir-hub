#include "ui/menus/filebrowser_forwarder.hpp"
#include "app.hpp"
#include "nacp_util.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "owo.hpp"
#include "i18n.hpp"
#include <cstdio>

namespace sphaira::ui::menu::filebrowser {

using namespace detail;

ForwarderForm::ForwarderForm(const FileAssocEntry& assoc, const RomDatabaseIndexs& db_indexs, const FileEntry& entry, const fs::FsPath& arg_path)
: Sidebar{"Forwarder Creation", Side::RIGHT}
, m_assoc{assoc}
, m_db_indexs{db_indexs}
, m_arg_path{arg_path} {
    log_write("parsing nro\n");
    if (R_FAILED(LoadNroMeta())) {
        App::Notify("Failed to parse nro"_i18n);
        SetPop();
        return;
    }

    log_write("got nro data\n");
    auto file_name = m_assoc.use_base_name ? entry.GetName() : entry.GetInternalName();

    if (auto pos = file_name.find_last_of('.'); pos != std::string::npos) {
        log_write("got filename\n");
        file_name = file_name.substr(0, pos);
        log_write("got filename2: %s\n\n", file_name.c_str());
    }

    const auto name = m_nro.nacp.lang.name + std::string{" | "} + file_name;
    const auto author = nacp_util::GetAuthor(m_nacp);
    const auto version = m_nacp.display_version;
    const auto icon = m_assoc.path;

    m_name = this->Add<SidebarEntryTextInput>(
        "Name", name, "", -1, sizeof(NacpLanguageEntry::name) - 1,
        "Set the name of the application"_i18n
    );

    m_author = this->Add<SidebarEntryTextInput>(
        "Author", author, "", -1, sizeof(NacpLanguageEntry::author) - 1,
        "Set the author of the application"_i18n
    );

    m_version = this->Add<SidebarEntryTextInput>(
        "Version", version, "", -1, sizeof(NacpStruct::display_version) - 1,
        "Set the display version of the application"_i18n
    );

    const std::vector<std::string> filters{".nro", ".png", ".jpg"};
    m_icon = this->Add<SidebarEntryFilePicker>(
        "Icon", icon, filters,
        "Set the path to the icon for the forwarder"_i18n
    );

    auto callback = this->Add<SidebarEntryCallback>("Create", [this, file_name](){
        OwoConfig config{};
        config.nro_path = m_assoc.path.toString();
        config.args = nro_add_arg_file(m_arg_path);
        config.nacp = m_nacp;

        // patch the name.
        config.name = m_name->GetValue();

        // patch the author.
        config.author = m_author->GetValue();

        // patch the display version.
        std::snprintf(config.nacp.display_version, sizeof(config.nacp.display_version), "%s", m_version->GetValue().c_str());

        // load icon fron nro or image.
        if (m_icon->GetValue().ends_with(".nro")) {
            // if path was left as the default, try and load the icon from rom db.
            if (config.nro_path == m_icon->GetValue()) {
                config.icon = GetRomIcon(file_name, m_db_indexs, m_nro);
            } else {
                config.icon = nro_get_icon(m_icon->GetValue());
            }
        } else {
            // try and read icon file into memory, bail if this fails.
            const auto rc = fs::FsStdio().read_entire_file(m_icon->GetValue(), config.icon);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to load icon");
                return;
            }
        }

        // if this is a rom, load intro logo.
        if (!m_db_indexs.empty()) {
            fs::FsNativeSd().read_entire_file(paths::LOGO + "/rom/NintendoLogo.png", config.logo);
            fs::FsNativeSd().read_entire_file(paths::LOGO + "/rom/StartupMovie.gif", config.gif);
        }

        // try and install.
        if (R_FAILED(App::Install(config))) {
            App::Notify("Failed to install forwarder"_i18n);
        } else {
            SetPop();
        }
    }, "Create the forwarder."_i18n);

    // ensure that all fields are valid.
    callback->Depends([this](){
        return
            !m_name->GetValue().empty() &&
            !m_author->GetValue().empty() &&
            !m_version->GetValue().empty() &&
            !m_icon->GetValue().empty();
    }, "All fields must be non-empty!"_i18n);
}

auto ForwarderForm::LoadNroMeta() -> Result {
    // try and load nro meta data.
    R_TRY(nro_parse(m_assoc.path, m_nro));
    R_TRY(nro_get_nacp(m_assoc.path, m_nacp));
    R_SUCCEED();
}

} // namespace sphaira::ui::menu::filebrowser
