#include "rom_collections.hpp"
#include "rom_collections_ui.hpp"
#include "rom_library.hpp"
#include "rom_thumbs.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "log.hpp"
#include "nso_ui.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/menus/filebrowser/filebrowser_internal.hpp"

#include <memory>
#include <stop_token>

namespace sphaira::rom {
namespace {

using ui::menu::filebrowser::FileAssocEntry;

const char* RootOfFrontend(Frontend f) {
    return f == Frontend::RetroArch ? "retroarch" : "tico";
}

auto FileName(const std::string& file) -> std::string {
    const auto slash = file.rfind('/');
    return slash == std::string::npos ? file : file.substr(slash + 1);
}

// the first emulator of this frontend for the file whose program is on the card; nullopt when there is none
auto FindCore(const std::vector<FileAssocEntry>& all, Frontend f, const std::string& console, const std::string& file) -> std::optional<FileAssocEntry> {
    const auto name = FileName(file);
    const auto dot = name.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    fs::FsNativeSd sd;
    for (const auto& assoc : ui::menu::filebrowser::FindAssocFor(all, std::string{ROMS_ROOT} + console, ext, ext)) {
        if (coll::RootOf(assoc.path.s) == RootOfFrontend(f) && sd.FileExists(assoc.path)) {
            return assoc;
        }
    }
    return std::nullopt;
}

// the RetroArch database (playlist) name of a console folder, without ".lpl"
auto DatabaseName(const std::string& console, const FileAssocEntry& core) -> std::string {
    if (const std::string_view repo = ThumbRepo(console); !repo.empty()) {
        return coll::DbNameFromRepo(repo);
    }
    if (const auto* c = FindConsole(console)) {
        for (const auto& db : core.database) {
            if (db == c->name) return db;
        }
    }
    return core.database.empty() ? std::string{} : core.database.front();
}

auto CorePathString(const FileAssocEntry& core) -> std::string {
    return core.path.s;
}

auto CoreDisplayName(const FileAssocEntry& core) -> std::string {
    const auto stem = coll::CoreStem(core.path.s);
    std::vector<u8> info;
    if (R_SUCCEEDED(fs::FsNativeSd().read_entire_file(fs::FsPath{(std::string{coll::RA_INFO} + stem + "_libretro.info").c_str()}, info)) && !info.empty()) {
        const auto name = coll::InfoDisplayName({reinterpret_cast<const char*>(info.data()), info.size()});
        if (!name.empty()) return name;
    }
    return stem;
}

// the PNG cover of a game: from the preview cache, else fetched and kept there. Empty when the game has none.
auto GetCover(ui::ProgressBox* pbox, const std::string& console, const std::string& file) -> std::vector<u8> {
    fs::FsNativeSd sd;
    const std::string cache = paths::DATA_ROOT + "/cache/thumbs/" + console + "/" + ThumbName(FileName(file)) + ".png";
    std::vector<u8> png;
    if (sd.FileExists(fs::FsPath{cache.c_str()}) && R_SUCCEEDED(sd.read_entire_file(fs::FsPath{cache.c_str()}, png)) && !png.empty()) {
        return png;
    }
    png.clear();

    pbox->NewTransfer("Looking for the cover"_i18n);
    const auto url = ResolveThumbUrl(console, FileName(file), std::stop_token{});
    if (url.empty() || pbox->ShouldExit()) return {};
    const auto result = curl::Api().ToMemory(curl::Url{url}, curl::OnProgress{pbox->OnDownloadProgressCallback()});
    if (!result.success || result.data.empty()) {
        log_write("[roms] no cover from %s (%ld)\n", url.c_str(), result.code);
        return {};
    }
    sd.CreateDirectoryRecursivelyWithPath(fs::FsPath{cache.c_str()});
    sd.write_entire_file(fs::FsPath{cache.c_str()}, result.data);
    return result.data;
}

void Finish(Frontend f, int added, int total, Result rc) {
    if (R_FAILED(rc) && rc != Result_TransferCancelled) {
        App::PushErrorBox(rc, "Add failed!"_i18n);
        return;
    }
    if (added == 0) {
        App::Notify("Nothing was added"_i18n);
        return;
    }
    App::Notify((f == Frontend::RetroArch ? "Added to RetroArch"_i18n : "Added to Tico"_i18n) + ": " + std::to_string(added) + " / " + std::to_string(total));
}

// RetroArch: playlist per database; the cover goes to Named_Boxarts under the label
auto RunRetroArch(ui::ProgressBox* pbox, const std::string& console, const std::vector<std::string>& files, int& added) -> Result {
    const auto all = ui::menu::filebrowser::LoadAssocList();
    fs::FsNativeSd sd;
    std::string db;
    std::string text;
    fs::FsPath list_path{};
    bool loaded = false;
    bool changed = false;
    Result rc = 0;

    for (size_t i = 0; i < files.size(); i++) {
        rc = pbox->ShouldExitResult();
        if (R_FAILED(rc)) break; // what is done so far is kept
        const auto name = FileName(files[i]);
        pbox->SetTitle(name);
        pbox->UpdateTransfer(static_cast<s64>(i + 1), static_cast<s64>(files.size()));

        const auto core = FindCore(all, Frontend::RetroArch, console, name);
        if (!core) continue;
        if (!loaded) {
            db = DatabaseName(console, *core);
            if (db.empty()) {
                log_write("[roms] retroarch: no database name for %s\n", console.c_str());
                added = 0;
                return Result_FsInvalidType;
            }
            list_path = fs::FsPath{(std::string{coll::RA_PLAYLISTS} + db + ".lpl").c_str()};
            std::vector<u8> bytes;
            if (sd.FileExists(list_path)) {
                // never overwrite a list that could not be read
                if (const auto read_rc = sd.read_entire_file(list_path, bytes); R_FAILED(read_rc)) {
                    log_write("[roms] retroarch: cannot read %s\n", list_path.s);
                    added = 0;
                    return read_rc;
                }
                text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            loaded = true;
        }

        coll::PlaylistGame game;
        game.path = std::string{ROMS_ROOT} + console + "/" + name;
        game.label = coll::Stem(name);
        game.core_path = CorePathString(*core);
        game.core_name = CoreDisplayName(*core);
        game.db_name = db + ".lpl";
        const auto next = coll::AddToPlaylist(text, game);
        if (!next) {
            // an unreadable list is never overwritten
            log_write("[roms] retroarch: %s is not a playlist this code can read\n", list_path.s);
            added = 0;
            return Result_FsInvalidType;
        }
        text = *next;
        changed = true;
        added++;

        // the cover: RetroArch finds it by the label
        const fs::FsPath thumb{(std::string{coll::RA_THUMBNAILS} + db + "/Named_Boxarts/" + coll::ThumbFile(game.label) + ".png").c_str()};
        if (!sd.FileExists(thumb)) {
            const auto png = GetCover(pbox, console, name);
            if (!png.empty()) {
                sd.CreateDirectoryRecursivelyWithPath(thumb);
                sd.write_entire_file(thumb, png);
            }
        }
    }

    if (changed) {
        sd.CreateDirectoryRecursivelyWithPath(list_path);
        std::vector<u8> bytes(text.begin(), text.end());
        R_TRY(sd.write_entire_file(list_path, bytes));
    }
    return rc;
}

// Tico: the cover only (JPG named like the ROM file without its extension)
auto RunTico(ui::ProgressBox* pbox, const std::string& console, const std::vector<std::string>& files, int& added) -> Result {
    const auto all = ui::menu::filebrowser::LoadAssocList();
    fs::FsNativeSd sd;
    for (size_t i = 0; i < files.size(); i++) {
        R_TRY(pbox->ShouldExitResult());
        const auto name = FileName(files[i]);
        pbox->SetTitle(name);
        pbox->UpdateTransfer(static_cast<s64>(i + 1), static_cast<s64>(files.size()));
        if (!FindCore(all, Frontend::Tico, console, name)) continue;

        const fs::FsPath cover{(std::string{coll::TICO_COVERS} + console + "/" + coll::Stem(name) + ".jpg").c_str()};
        if (sd.FileExists(cover)) {
            added++;
            continue;
        }
        const auto png = GetCover(pbox, console, name);
        if (png.empty()) continue;
        const auto image = ImageLoadFromMemory(png);
        if (image.data.empty()) continue;
        const auto jpg = ImageConvertToJpg(image.data, image.w, image.h);
        if (jpg.data.empty()) continue;
        sd.CreateDirectoryRecursivelyWithPath(cover);
        R_TRY(sd.write_entire_file(cover, jpg.data));
        added++;
    }
    R_SUCCEED();
}

void Start(Frontend f, const std::string& console, const std::vector<std::string>& files) {
    auto added = std::make_shared<int>(0);
    const int total = static_cast<int>(files.size());
    App::Push<ui::ProgressBox>(0, f == Frontend::RetroArch ? "Adding to RetroArch"_i18n : "Adding to Tico"_i18n, "",
        [f, console, files, added](auto pbox) -> Result {
            return f == Frontend::RetroArch ? RunRetroArch(pbox, console, files, *added) : RunTico(pbox, console, files, *added);
        },
        [f, added, total](Result rc) { Finish(f, *added, total, rc); });
}

} // namespace

void AddFrontendOptions(ui::Sidebar* sidebar, const std::string& console, const std::vector<std::string>& files) {
    if (files.empty()) return;
    const auto all = ui::menu::filebrowser::LoadAssocList(); // once for both entries
    if (FindCore(all, Frontend::RetroArch, console, files.front())) {
        sidebar->Add<ui::SidebarEntryCallback>("Add to RetroArch"_i18n, [console, files]() {
            AddToRetroArch(console, files);
        }, true, std::to_string(files.size()) + " " + "files"_i18n);
    }
    if (FindCore(all, Frontend::Tico, console, files.front())) {
        sidebar->Add<ui::SidebarEntryCallback>("Add to Tico"_i18n, [console, files]() {
            AddToTico(console, files);
        }, true, "Cover only: Tico reads the games from its game folders"_i18n);
    }
    nso_ui::AddOptions(sidebar, std::string{ROMS_ROOT} + console, console, files);
}

void AddFrontendOptionsFor(ui::Sidebar* sidebar, const std::string& dir, const std::vector<ui::menu::filebrowser::FileEntry>& entries) {
    const auto console = coll::RomConsoleOf(dir);
    std::vector<std::string> files;
    for (const auto& e : entries) {
        if (e.IsFile()) files.push_back(e.name);
    }
    if (console.empty() || !FindConsole(console)) {
        nso_ui::AddOptions(sidebar, dir, "", files); // a ROM in any folder can go to Nintendo Switch Online
        return;
    }
    AddFrontendOptions(sidebar, console, files);
}

auto FetchCover(ui::ProgressBox* pbox, const std::string& console, const std::string& file) -> std::vector<u8> {
    return GetCover(pbox, console, file);
}

void AddToRetroArch(const std::string& console, const std::vector<std::string>& files) {
    Start(Frontend::RetroArch, console, files);
}

void AddToTico(const std::string& console, const std::vector<std::string>& files) {
    Start(Frontend::Tico, console, files);
}

} // namespace sphaira::rom
