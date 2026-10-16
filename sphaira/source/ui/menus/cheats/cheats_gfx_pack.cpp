#include "ui/menus/cheats/cheats_gfx_pack.hpp"
#include "ui/menus/cheats/cheats_ops.hpp"
#include "ui/menus/cheats/cheat_text.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheat_game_select_menu.hpp"
#include "ui/menus/cheats/cheats_db.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats_menu.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"

#include <minizip/unzip.h>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>

namespace sphaira::ui::menu::hats {
namespace {

struct PackGame {
    u64 title_id{};
    std::string name;
    // the pack has a file for the installed version's Build ID; nullopt = that Build ID is unknown
    std::optional<bool> for_installed_version{};
    std::vector<std::pair<std::string, std::string>> files; // build id, cheat text
};

// title id -> its cheat files, read from the pack zip in memory (it is under 1 MB).
auto ReadPack(std::vector<u8> zip, std::map<u64, std::vector<std::pair<std::string, std::string>>>& out) -> Result {
    mz::MzMem mem{std::move(zip), 0};
    zlib_filefunc64_def funcs;
    mz::FileFuncMem(&mem, &funcs);
    auto zf = unzOpen2_64("pack.zip", &funcs);
    R_UNLESS(zf, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zf));

    for (auto rc = unzGoToFirstFile(zf); rc == UNZ_OK; rc = unzGoToNextFile(zf)) {
        unz_file_info64 info{};
        char name[512]{};
        if (unzGetCurrentFileInfo64(zf, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK) {
            continue;
        }
        const auto path = detail::ParsePackCheatPath(name);
        if (!path || info.uncompressed_size > 1024 * 1024 || unzOpenCurrentFile(zf) != UNZ_OK) {
            continue;
        }
        std::string text(info.uncompressed_size, '\0');
        const auto read = unzReadCurrentFile(zf, text.data(), text.size());
        unzCloseCurrentFile(zf);
        if (read == static_cast<int>(text.size())) {
            out[path->title_id].emplace_back(path->build_id, std::move(text));
        }
    }
    R_SUCCEED();
}

auto InstallGames(ProgressBox* pbox, const std::vector<PackGame>& games) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    size_t added{}, dropped{};
    for (size_t i = 0; i < games.size(); i++) {
        R_TRY(pbox->ShouldExitResult());
        const auto& game = games[i];
        pbox->NewTransfer(game.name).UpdateTransfer(i, games.size());
        const auto dir = detail::GetCheatsDirPath(game.title_id);
        fs.CreateDirectoryRecursively(dir.c_str());
        for (const auto& [build_id, text] : game.files) {
            fs::FsPath path;
            std::snprintf(path, sizeof(path), "%s/%s.txt", dir.c_str(), build_id.c_str());
            std::vector<u8> existing;
            if (fs.FileExists(path)) {
                fs.read_entire_file(path, existing);
            }
            const auto merged = detail::MergeCheatText({reinterpret_cast<const char*>(existing.data()), existing.size()}, text);
            added += merged.added;
            dropped += merged.dropped;
            if (!merged.added) {
                continue;
            }
            R_TRY(fs.write_entire_file(path, std::vector<u8>(merged.text.begin(), merged.text.end())));
        }
    }
    log_write("[Cheats] GFX pack: %zu games, %zu cheats added, %zu over the limits\n", games.size(), added, dropped);
    R_SUCCEED();
}

void ShowGamePicker(std::shared_ptr<std::vector<PackGame>> games) {
    if (games->empty()) {
        App::Notify("No installed game has cheats in this pack"_i18n);
        return;
    }
    PopupList::Items items;
    std::vector<std::string> descriptions;
    for (const auto& g : *games) {
        items.emplace_back(g.name);
        descriptions.emplace_back(!g.for_installed_version ? "The installed version could not be checked."_i18n
                                  : *g.for_installed_version ? "For the installed version."_i18n
                                  : "Only for other versions of the game; may not work."_i18n);
    }
    items.emplace_back("Install for ticked games"_i18n);
    descriptions.emplace_back("Adds the pack's cheats to these games. Cheats you already have stay as they are."_i18n);
    auto checks = std::make_shared<std::vector<bool>>(games->size(), false);

    auto popup = std::make_unique<PopupList>("60FPS/GFX Cheats"_i18n, std::move(items), [games, checks](auto op_index) {
        if (!op_index) {
            return;
        }
        auto chosen = std::make_shared<std::vector<PackGame>>();
        for (size_t i = 0; i < games->size(); i++) {
            if ((*checks)[i]) {
                chosen->push_back((*games)[i]);
            }
        }
        if (chosen->empty()) {
            App::Notify("Tick at least one game"_i18n);
            return;
        }
        App::Push<ProgressBox>(0, "Installing cheats..."_i18n, "60FPS/GFX Cheats"_i18n,
            [chosen](auto pbox) -> Result { return InstallGames(pbox, *chosen); },
            [](Result rc) {
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Cheats pack installed"_i18n);
                } else {
                    App::PushErrorBox(rc, "Failed to install cheats pack"_i18n);
                }
            });
    });
    popup->SetChecks(checks);
    popup->SetDescriptions(std::move(descriptions));
    App::Push(std::move(popup));
}

} // namespace

void PromptGfxCheatsDownload() {
    auto games = std::make_shared<std::vector<PackGame>>();
    App::Push<ProgressBox>(0, "Downloading..."_i18n, "60FPS/GFX Cheats"_i18n,
        [games](auto pbox) -> Result {
            pbox->NewTransfer("Downloading cheats pack..."_i18n);
            auto result = curl::Api().ToMemory(
                curl::Url{KEFIR_CHEATS_GFX_URL},
                curl::OnProgress{pbox->OnDownloadProgressCallback()}
            );
            R_UNLESS(result.success && !result.data.empty(), Result_CurlFailedEasyInit);

            std::map<u64, std::vector<std::pair<std::string, std::string>>> pack;
            R_TRY(ReadPack(std::move(result.data), pack));

            // only games on the console; the installed version's Build ID comes from the cheat scan cache.
            for (auto& game : detail::EnumerateInstalledGames()) {
                const auto it = pack.find(game.title_id);
                if (it == pack.end()) {
                    continue;
                }
                PackGame g{game.title_id, game.name, std::nullopt, it->second};
                auto bid = GetCachedBuildIdForTitle(game);
                if (bid.empty()) {
                    bid = GetBuildIdFromInstalledNca(game.title_id); // the same lookup as "Download Exact Cheats"
                }
                if (bid.size() >= 16) {
                    bid.resize(16);
                    std::transform(bid.begin(), bid.end(), bid.begin(), ::toupper);
                    g.for_installed_version = std::any_of(g.files.begin(), g.files.end(), [&](const auto& f) { return f.first == bid; });
                }
                games->push_back(std::move(g));
            }
            std::sort(games->begin(), games->end(), [](const auto& a, const auto& b) { return a.name < b.name; });
            R_SUCCEED();
        },
        [games](Result rc) {
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to install cheats pack"_i18n);
                return;
            }
            ShowGamePicker(games);
        });
}

} // namespace sphaira::ui::menu::hats
