#include "forced_language.hpp"
#include "title_info.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "defines.hpp"

#include <minIni.h>
#include <yyjson.h>

namespace sphaira::forced_language {
namespace {

constexpr const char* SECTION = "override_config";
constexpr const char* KEY = "override_language";

auto ConfigPath(std::uint64_t app_id) -> fs::FsPath {
    return fs::AppendPath(title::GetContentsPath(app_id), "config.ini");
}

} // namespace

auto Get(std::uint64_t app_id) -> std::string {
    char buf[16]{};
    ini_gets(SECTION, KEY, "", buf, sizeof(buf), ConfigPath(app_id));
    return buf;
}

bool Set(std::uint64_t app_id, std::string_view code) {
    const auto path = ConfigPath(app_id);
    if (code.empty()) {
        // removing the key leaves the rest of the file (cheats, override_nacp, ...) as it was.
        return Get(app_id).empty() || ini_puts(SECTION, KEY, nullptr, path);
    }
    if (IndexOf(code) < 0) {
        return false;
    }
    fs::FsNativeSd().CreateDirectoryRecursively(title::GetContentsPath(app_id));
    const std::string value{code};
    const bool ok = ini_puts(SECTION, KEY, value.c_str(), path);
    log_write("[LANG] %016lX override_language=%s: %d\n", app_id, value.c_str(), ok);
    return ok;
}

bool ParsePack(std::string_view json, PackInfo& out) {
    auto doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const auto root = yyjson_doc_get_root(doc);
    const auto format = yyjson_get_int(yyjson_obj_get(root, "format"));
    const auto tid = yyjson_get_str(yyjson_obj_get(root, "title_id"));
    const auto lang = yyjson_get_str(yyjson_obj_get(root, "language"));
    if (format < 1 || format > PACK_FORMAT || !tid || !lang) {
        return false;
    }
    out.title_id = ParseTitleId(tid);
    out.language = lang;
    return out.title_id && IndexOf(out.language) >= 0;
}

} // namespace sphaira::forced_language
