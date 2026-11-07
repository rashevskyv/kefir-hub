#include "rom_files.hpp"
#include "rom_library.hpp"
#include "rom_thumbs.hpp"
#include "nro.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/remote_input.hpp"
#include "ui/sidebar.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/filebrowser/filebrowser_internal.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace sphaira::rom {
namespace {

using ui::menu::settings::SettingsItem;
using ui::menu::tools::SystemToolsMenu;

struct Entry {
    std::string name{};
    bool dir{};
    std::uint64_t size{};
};

constexpr size_t PAGE_ROWS = 1000;
// rows of the list: 0 is the header, 1 the filter row, the entries follow
constexpr size_t FIRST_ROW = 2;

auto FolderPath(const std::string& folder) -> fs::FsPath {
    return fs::FsPath{(std::string{ROMS_ROOT} + folder).c_str()};
}

auto List(const std::string& folder) -> std::vector<Entry> {
    std::vector<Entry> out;
    fs::FsNativeSd sd;
    fs::Dir dir;
    if (R_FAILED(sd.OpenDirectory(FolderPath(folder), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        return out;
    }

    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return out;
    }
    for (const auto& e : entries) {
        if (e.name[0] != '.') {
            out.push_back({e.name, e.type == FsDirEntryType_Dir, e.type == FsDirEntryType_Dir ? 0 : static_cast<std::uint64_t>(e.file_size)});
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

auto Describe(const std::vector<Entry>& entries) -> std::string {
    std::uint64_t bytes{};
    for (const auto& e : entries) bytes += e.size;
    return std::to_string(entries.size()) + " " + "files"_i18n + (bytes ? ", " + ui::menu::grid::FormatBytes(bytes) : std::string{});
}

void Delete(const std::string& folder, std::vector<Entry> targets, std::function<void()> done) {
    App::Push<ui::ProgressBox>(0, "Deleting"_i18n, "", [folder, targets](auto pbox) -> Result {
        fs::FsNativeSd sd;
        const auto root = FolderPath(folder);
        for (size_t i = 0; i < targets.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& e = targets[i];
            pbox->SetTitle(e.name);
            pbox->UpdateTransfer(static_cast<s64>(i + 1), static_cast<s64>(targets.size()));
            const auto path = fs::AppendPath(root, e.name.c_str());
            log_write("[roms] delete %s\n", path.s);
            R_TRY(e.dir ? sd.DeleteDirectoryRecursively(path) : sd.DeleteFile(path));
        }
        R_SUCCEED();
    }, [done](Result rc) {
        if (rc == Result_TransferCancelled) {
            done();
            return;
        }
        App::PushErrorBox(rc, "Delete failed!"_i18n);
        if (R_SUCCEEDED(rc)) App::Notify("Deleted"_i18n);
        done();
    });
}

// the question before a delete: the files are named when there are few, counted when there are many
void ConfirmDelete(const std::string& folder, const std::vector<Entry>& targets, std::function<void()> done) {
    const auto what = targets.size() == 1 ? targets.front().name : Describe(targets);
    App::Push<ui::OptionBox>("Delete "_i18n + what + "?\n" + "This cannot be undone."_i18n, "Back"_i18n, "Delete"_i18n, 0,
        [folder, targets, done](auto op) {
            if (op && *op == 1) Delete(folder, targets, done);
        });
}

// A on a game: the emulators that open it (the same list as the file browser), then the game starts in the one picked
void Launch(const std::string& folder, const Entry& e) {
    using namespace ui::menu::filebrowser;
    const auto dot = e.name.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : e.name.substr(dot + 1);
    const auto list = FindAssocFor(LoadAssocList(), std::string{ROMS_ROOT} + folder, ext, ext);
    if (list.empty()) {
        App::Notify("No emulator found for this file"_i18n);
        return;
    }
    const fs::FsPath rom{(std::string{ROMS_ROOT} + folder + "/" + e.name).c_str()};
    const auto run = [rom](const FileAssocEntry& assoc) { nro_launch(assoc.path, assoc.GetRomArgs(rom)); };
    if (list.size() == 1) {
        run(list.front());
        return;
    }
    ui::PopupList::Items items;
    for (const auto& assoc : list) items.emplace_back(MakeLauncherLabel(assoc));
    App::Push<ui::PopupList>("Launch option for: "_i18n + e.name, items, [list, run](auto op) {
        if (op) run(list[*op]);
    });
}

void Show(const std::string& folder, std::function<void()> refreshed, const std::string& words) {
    std::vector<Entry> hits;
    for (const auto& e : List(folder)) {
        if (MatchesWords(e.name, words)) hits.push_back(e);
    }
    const size_t shown = std::min(hits.size(), PAGE_ROWS);

    auto self = std::make_shared<SystemToolsMenu*>(nullptr);
    // the page is drawn again in place of this one: after a filter, after a delete
    const std::function<void(std::string)> redraw = [self, folder, refreshed](std::string w) {
        if (*self) (*self)->SetPop();
        Show(folder, refreshed, w);
    };
    const std::function<void()> done = [redraw, refreshed, words]() {
        if (refreshed) refreshed();
        redraw(words);
    };

    const auto* console = FindConsole(folder);
    const std::string title = console ? console->name : folder;
    std::vector<SettingsItem> rows;
    rows.push_back(ui::menu::settings::MakeHeader(title + (words.empty() ? "" : " · " + words) + " → " + ROMS_ROOT + folder + "/ (" + Describe(hits) + ")"));
    SettingsItem filter{"Filter by words"_i18n, words.empty() ? "Type words from the name to narrow the list"_i18n : "[" + words + "]"};
    filter.action = [redraw]() {
        ui::remote_input::Options opts;
        opts.title = "Filter by words"_i18n;
        opts.guide = "Words from the file name, for example a game; empty shows every file"_i18n;
        ui::remote_input::PromptTextInput(opts, [redraw](const std::string& text) { redraw(text); });
    };
    rows.push_back(std::move(filter));
    if (hits.empty()) {
        SettingsItem row{"Nothing found"_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    }
    for (size_t i = 0; i < shown; i++) {
        const auto& e = hits[i];
        SettingsItem row{e.dir ? e.name + "/" : e.name, e.dir ? "folder"_i18n : ui::menu::grid::FormatBytes(e.size)};
        row.selectable = true;
        // A starts a game; a folder asks to be deleted. START deletes the marked files (or the one under the cursor)
        row.action = e.dir ? std::function<void()>{[folder, e, done]() { ConfirmDelete(folder, {e}, done); }}
                           : std::function<void()>{[folder, e]() { Launch(folder, e); }};
        rows.push_back(std::move(row));
    }
    if (shown < hits.size()) {
        SettingsItem row{std::to_string(shown) + " " + "shown. Narrow the list with the filter above."_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    }

    auto menu = std::make_unique<SystemToolsMenu>(title, std::move(rows));
    *self = menu.get();
    menu->SetNameShare(0.8f);
    menu->SetPreview([folder, hits, shown](size_t row) -> std::optional<ui::menu::tools::PreviewInfo> {
        if (row < FIRST_ROW || row - FIRST_ROW >= shown || hits[row - FIRST_ROW].dir) return std::nullopt;
        const auto& h = hits[row - FIRST_ROW];
        return GamePreview(folder, h.name, h.size);
    });
    menu->SetOptions([folder, hits, shown, done](const std::vector<size_t>& marked) {
        auto options = std::make_unique<ui::Sidebar>("Options"_i18n, ui::Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        std::vector<Entry> picked;
        for (auto i : marked) {
            if (i >= FIRST_ROW && i - FIRST_ROW < shown) picked.push_back(hits[i - FIRST_ROW]);
        }
        if (!picked.empty()) {
            options->Add<ui::SidebarEntryCallback>("Delete the marked files"_i18n, [folder, picked, done]() {
                ConfirmDelete(folder, picked, done);
            }, true, Describe(picked))->SetIcon(ActionIcon::Delete);
        }
        const std::vector<Entry> all(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(shown));
        if (!all.empty()) {
            options->Add<ui::SidebarEntryCallback>("Delete all shown"_i18n, [folder, all, done]() {
                ConfirmDelete(folder, all, done);
            }, true, Describe(all))->SetIcon(ActionIcon::Delete);
        }
    });
    App::Push(std::move(menu));
}

} // namespace

auto Stat(const std::string& folder) -> FolderStat {
    FolderStat out;
    for (const auto& e : List(folder)) {
        out.entries++;
        out.bytes += e.size;
    }
    return out;
}

void OpenFiles(const std::string& folder, std::function<void()> refreshed) {
    Show(folder, std::move(refreshed), "");
}

} // namespace sphaira::rom
