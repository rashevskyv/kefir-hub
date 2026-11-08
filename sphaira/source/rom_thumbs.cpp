#include "rom_thumbs.hpp"
#include "rom_library.hpp"
#include "rom_meta_fetch.hpp"
#include "app_paths.hpp"
#include "i18n.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::rom {
namespace {

// the names of a system's folder on the server: from the card when kept there, else read from the server and kept
auto LoadIndex(const std::string& folder, std::string_view kind, std::stop_token token) -> std::vector<std::string> {
    const auto url = ThumbDirUrl(folder, kind);
    if (url.empty()) return {};

    const fs::FsPath path{(paths::DATA_ROOT + "/cache/thumbs/index/" + folder + "-" + std::string{kind} + ".txt").c_str()};
    fs::FsNativeSd sd;
    std::vector<u8> text;
    if (R_FAILED(sd.read_entire_file(path, text)) || text.empty()) {
        const auto result = curl::Api().ToMemory(curl::Url{url}, curl::StopToken{token});
        if (!result.success || result.data.empty()) {
            log_write("[roms] no picture list at %s (%ld)\n", url.c_str(), result.code);
            return {};
        }
        const auto names = ParseThumbIndex({reinterpret_cast<const char*>(result.data.data()), result.data.size()});
        for (const auto& n : names) {
            text.insert(text.end(), n.begin(), n.end());
            text.push_back('\n');
        }
        if (!text.empty()) {
            fs::CreateDirectoryRecursively(fs::FsPath{(paths::DATA_ROOT + "/cache/thumbs/index").c_str()});
            sd.write_entire_file(path, text);
        }
    }

    std::vector<std::string> names;
    const std::string_view all{reinterpret_cast<const char*>(text.data()), text.size()};
    for (size_t at = 0; at < all.size();) {
        const auto end = all.find('\n', at);
        names.emplace_back(all.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return names;
}

// the translated label of a fact (rom_meta.hpp gives the English one)
auto MetaLabel(std::string_view label) -> std::string {
    if (label == "Year") return "Year"_i18n;
    if (label == "Publisher") return "Publisher"_i18n;
    if (label == "Developer") return "Developer"_i18n;
    if (label == "Players") return "Players"_i18n;
    return "Genre"_i18n;
}

// the text lines of a game row: size, system, the facts when the card has them, folder
using Labels = std::vector<std::pair<std::string, std::string>>; // English -> translated, made on the UI thread (the i18n cache is not thread safe)

auto MakeLabels() -> Labels {
    return {{"Year", MetaLabel("Year")}, {"Publisher", MetaLabel("Publisher")}, {"Developer", MetaLabel("Developer")},
        {"Players", MetaLabel("Players")}, {"Genre", MetaLabel("Genre")}};
}

auto BuildLines(const std::string& console, const std::string& file, std::uint64_t size, const Labels& labels) -> std::vector<std::string> {
    std::vector<std::string> lines{ui::menu::grid::FormatBytes(size), std::string{ConsoleName(console)}};
    if (const auto table = LoadMeta(console)) {
        if (const auto* rec = FindMeta(*table, file)) {
            for (const auto& [label, value] : MetaLines(*rec)) {
                std::string name{label};
                for (const auto& [en, tr] : labels) if (en == label) name = tr;
                lines.push_back(name + ": " + value);
            }
        }
    }
    lines.push_back(std::string{ROMS_ROOT} + console + "/");
    return lines;
}

} // namespace

auto ResolveThumbUrl(const std::string& folder, const std::string& file, std::stop_token token) -> std::string {
    for (const std::string_view kind : {"Named_Boxarts", "Named_Snaps"}) {
        if (token.stop_requested()) return {};
        if (const auto pick = PickThumb(LoadIndex(folder, kind, token), file); !pick.empty()) {
            return ThumbDirUrl(folder, kind) + bios::EncodePath(pick + ".png");
        }
    }
    return {};
}

auto GamePreview(const std::string& console, const std::string& file, std::uint64_t size) -> ui::menu::tools::PreviewInfo {
    ui::menu::tools::PreviewInfo info;
    const auto slash = file.rfind('/');
    info.title = ThumbName(slash == std::string::npos ? file : file.substr(slash + 1));
    const auto labels = MakeLabels();
    if (!ThumbDirUrl(console, "Named_Boxarts").empty()) {
        info.key = console + "/" + info.title + ".png";
        // the worker runs when the cursor rests on the row: the picture, then (once per system) the game facts for the next draw
        info.resolve = [console, file](std::stop_token token) { return ResolveThumbUrl(console, file, token); };
        // the facts (once per system), then the lines again; empty when nothing changed, so the pane keeps its text
        info.after = [console, file, size, labels](std::stop_token token, const std::vector<std::string>& shown) -> std::vector<std::string> {
            if (!PrefetchMeta(console, token) || token.stop_requested()) return {};
            auto lines = BuildLines(console, file, size, labels);
            return lines == shown ? std::vector<std::string>{} : lines;
        };
    }
    info.lines = BuildLines(console, file, size, labels);
    return info;
}

bool PrefetchMeta(const std::string& console, std::stop_token token) {
    return FetchMeta(console, token);
}

} // namespace sphaira::rom
