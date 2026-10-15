#include "ui/menus/filebrowser.hpp"
#include "ui/sidebar.hpp"
#include "romfs_pack.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cstring>

namespace sphaira::ui::menu::filebrowser {

// the selected title folders, else the highlighted one, else the title folder being viewed
// (a highlighted "romfs" folder stands for the title folder it is in).
void FsView::AddRomfsPackOption(Sidebar* options) {
    if (!IsSd() || !m_path.toString().starts_with("/atmosphere/contents")) {
        return;
    }

    std::vector<fs::FsPath> targets;
    const auto add = [&targets](const fs::FsPath& dir) {
        if (std::ranges::find(targets, dir) == targets.end() && romfs_pack::CanPack(dir)) {
            targets.push_back(dir);
        }
    };
    for (const auto& e : GetSelectedEntries()) {
        if (e.IsDir()) {
            add(!std::strcmp(e.name, "romfs") ? m_path : GetNewPath(e));
        }
    }
    if (targets.empty()) {
        add(m_path);
    }
    if (targets.empty()) {
        return;
    }

    options->Add<SidebarEntryCallback>("Pack into romfs.bin"_i18n, [this, targets](){
        romfs_pack::PackTitles(targets, [this](){
            m_menu->RefreshViews();
        });
    }, true, "Pack the mod's romfs folder into one romfs.bin file. Atmosphere then reads one file instead of thousands."_i18n);
}

} // namespace sphaira::ui::menu::filebrowser
