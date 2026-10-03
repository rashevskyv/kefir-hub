#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/menus/save_menu.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "dumper.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include <memory>
#include <utility>

namespace sphaira::ui::menu::game {

void Menu::CreateContentsFolders() {
    const auto targets = GetSelectedEntries();
    size_t created{};
    for (const auto& target : targets) {
        const auto path = title::GetContentsPath(target.app_id);
        auto rc = fs::FsNativeSd().CreateDirectory(path);
        if (rc == FsError_PathAlreadyExists) {
            rc = 0;
        }
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Mods folder create failed!"_i18n);
            return;
        }

        const auto entry = std::ranges::find_if(m_entries, [&target](const auto& candidate){
            return candidate.app_id == target.app_id;
        });
        if (entry != m_entries.end()) {
            entry->mods_folder = true;
        }
        created++;
    }

    ClearSelection();
    App::Notify(std::to_string(created) + " " + "mods folder(s) ready"_i18n);
}

void Menu::DeleteGames(bool with_mods) {
    App::Push<ProgressBox>(0, "Deleting"_i18n, "", [this, with_mods](auto pbox) -> Result {
        auto targets = GetSelectedEntries();

        for (s64 i = 0; i < std::size(targets); i++) {
            auto& e = targets[i];

            LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, std::size(targets));
            R_TRY(DeleteApplicationKeepSave(e.app_id));
            if (with_mods) {
                ProbeModsFolder(e);
                if (e.layeredfs) {
                    R_TRY(DeleteGameMods(e.app_id));
                }
            }
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Delete failed!"_i18n);

        ClearSelection();
        m_dirty = true;

        if (R_SUCCEEDED(rc)) {
            App::Notify("Delete successful!"_i18n);
        }
    });
}

void Menu::DumpGames(u32 flags) {
    DumpEntries(GetSelectedEntries(), flags, true);
}

void Menu::DumpEntries(std::vector<Entry> targets, u32 flags, bool clear_selection) {
    std::vector<NspEntry> nsp_entries;
    for (auto& e : targets) {
        if (const auto rc = BuildNspEntries(e, flags, nsp_entries); R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to prepare NSP dump"_i18n);
            return;
        }
    }

    if (nsp_entries.empty()) {
        App::Notify("No matching installed content to dump"_i18n);
        return;
    }

    std::vector<fs::FsPath> paths;
    for (auto& e : nsp_entries) {
        paths.emplace_back(fs::AppendPath("/dumps/NSP", e.path));
    }

    auto source = std::make_shared<NspSource>(nsp_entries);
    dump::Dump(source, paths, [this, clear_selection](Result rc){
        if (clear_selection) {
            ClearSelection();
        }
    });
}

void Menu::CreateRepack(Entry entry, u32 flags) {
    LoadControlEntry(entry);

    title::NspEntry nsp_entry;
    if (const auto rc = title::BuildMergedNspEntry(entry.app_id, entry.GetName(), flags, nsp_entry); R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to prepare repack NSP"_i18n);
        return;
    }

    nsp_entry.icon = entry.image;

    std::vector<title::NspEntry> entries;
    entries.emplace_back(std::move(nsp_entry));

    std::vector<fs::FsPath> paths;
    paths.emplace_back(fs::AppendPath("/games", entries[0].path));

    auto source = std::make_shared<NspSource>(entries);

    dump::DumpLocation location{};
    location.entry = {dump::DumpLocationType_SdCard, 0};

    dump::Dump(source, location, paths, [](Result){});
}

void Menu::CreateSaves(AccountUid uid) {
    App::Push<ProgressBox>(0, "Creating"_i18n, "", [this, uid](auto pbox) -> Result {
        auto targets = GetSelectedEntries();

        for (s64 i = 0; i < std::size(targets); i++) {
            auto& e = targets[i];

            LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, std::size(targets));
            const auto rc = CreateSave(e.app_id, uid);

            // don't error if the save already exists.
            if (R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
                R_THROW(rc);
            }
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Save create failed!"_i18n);

        ClearSelection();
        save::SignalChange();

        if (R_SUCCEEDED(rc)) {
            App::Notify("Save create successful!"_i18n);
        }
    });
}

} // namespace sphaira::ui::menu::game