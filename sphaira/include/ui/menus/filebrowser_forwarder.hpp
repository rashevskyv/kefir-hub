#pragma once

#include "ui/sidebar.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "nro.hpp"
#include "fs.hpp"

namespace sphaira::ui::menu::filebrowser {

struct ForwarderForm final : public Sidebar {
    explicit ForwarderForm(const FileAssocEntry& assoc, const detail::RomDatabaseIndexs& db_indexs, const FileEntry& entry, const fs::FsPath& arg_path);

private:
    auto LoadNroMeta() -> Result;

private:
    const FileAssocEntry m_assoc;
    const detail::RomDatabaseIndexs m_db_indexs;
    const fs::FsPath m_arg_path;

    NroEntry m_nro{};
    NacpStruct m_nacp{};

    SidebarEntryTextInput* m_name{};
    SidebarEntryTextInput* m_author{};
    SidebarEntryTextInput* m_version{};
    SidebarEntryFilePicker* m_icon{};
};

} // namespace sphaira::ui::menu::filebrowser
