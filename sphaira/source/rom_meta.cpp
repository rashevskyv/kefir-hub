#include "rom_meta_fetch.hpp"
#include "app_paths.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "defines.hpp"

#include <mutex>
#include <set>
#include <string_view>
#include <vector>

namespace sphaira::rom {
namespace {

std::mutex g_mutex;                       // guards the three below
std::string g_console;                    // the system held in g_table
std::shared_ptr<const MetaTable> g_table; // null: the card has no file for g_console
std::set<std::string> g_failed;           // systems whose download failed in this run
std::mutex g_fetch_mutex;                 // one download at a time

auto CachePath(const std::string& console) -> std::string {
    return paths::DATA_ROOT + "/cache/meta/" + console + ".txt";
}

} // namespace

auto LoadMeta(const std::string& console) -> std::shared_ptr<const MetaTable> {
    {
        std::scoped_lock lock{g_mutex};
        if (g_console == console) return g_table;
    }

    std::shared_ptr<const MetaTable> table;
    fs::FsNativeSd sd;
    std::vector<u8> text;
    if (R_SUCCEEDED(sd.read_entire_file(fs::FsPath{CachePath(console).c_str()}, text)) && !text.empty()) {
        auto parsed = ParseCompact({reinterpret_cast<const char*>(text.data()), text.size()});
        if (!parsed.recs.empty()) table = std::make_shared<const MetaTable>(std::move(parsed));
    }

    std::scoped_lock lock{g_mutex};
    g_console = console;
    g_table = table;
    return table;
}

bool FetchMeta(const std::string& console, std::stop_token token) {
    std::scoped_lock fetch{g_fetch_mutex};
    if (LoadMeta(console)) return true;
    {
        std::scoped_lock lock{g_mutex};
        if (g_failed.count(console)) return false;
    }

    std::vector<std::string> texts;
    for (int f = 0; f < META_FIELDS; f++) {
        if (token.stop_requested()) return false; // a cursor move: not a failure, try again later
        const auto url = MetaDatUrl(console, static_cast<MetaField>(f));
        if (url.empty()) return false;
        const auto result = curl::Api().ToMemory(curl::Url{url}, curl::StopToken{token});
        if (token.stop_requested()) return false;
        if (!result.success || result.data.empty()) {
            log_write("[roms] no game facts at %s (%ld)\n", url.c_str(), result.code);
            std::scoped_lock lock{g_mutex};
            g_failed.insert(console);
            return false;
        }
        texts.emplace_back(reinterpret_cast<const char*>(result.data.data()), result.data.size());
    }

    if (token.stop_requested()) return false;
    const std::vector<std::string_view> views(texts.begin(), texts.end());
    const auto compact = FormatCompact(MergeDats(views));
    const std::vector<u8> bytes(compact.begin(), compact.end());
    if (token.stop_requested()) return false;
    fs::CreateDirectoryRecursively(fs::FsPath{(paths::DATA_ROOT + "/cache/meta").c_str()});
    fs::FsNativeSd sd;
    // an empty merge or a full card will not mend itself in this run: do not download again on the next dwell
    if (compact.empty() || R_FAILED(sd.write_entire_file(fs::FsPath{CachePath(console).c_str()}, bytes))) {
        log_write("[roms] cannot keep the game facts of %s\n", console.c_str());
        std::scoped_lock lock{g_mutex};
        g_failed.insert(console);
        return false;
    }

    std::scoped_lock lock{g_mutex};
    g_console.clear(); // read the new file on the next LoadMeta
    g_table.reset();
    return true;
}

} // namespace sphaira::rom
