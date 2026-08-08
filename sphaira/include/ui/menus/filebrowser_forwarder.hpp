#pragma once

#include "ui/menus/filebrowser.hpp"
#include "ui/menus/filebrowser_assoc.hpp"
#include "fs.hpp"

namespace sphaira::ui::menu::filebrowser {

// opens the same full screen editor that nro forwarders use, pre-filled from
// the rom and the launcher picked for it.
void ShowRomForwarderEditor(const FileAssocEntry& assoc, const detail::RomDatabaseIndexs& db_indexs, const FileEntry& entry, const fs::FsPath& arg_path);

} // namespace sphaira::ui::menu::filebrowser
