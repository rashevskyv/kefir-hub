#include "demo/demo_data.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <yyjson.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sphaira::demo {
namespace {

constexpr const char* DEMO_DIR = "/config/kefir/demo/";
constexpr const char* TITLES_JSON = "/config/kefir/demo/titles.json";

struct Data {
    std::vector<Title> titles;
    s64 nand_free{};
    s64 nand_total{};
    bool emummc{};
};

auto Str(yyjson_val* obj, const char* key) -> std::string {
    const auto s = yyjson_get_str(yyjson_obj_get(obj, key));
    return s ? s : "";
}

auto StorageId(const std::string& s) -> u8 {
    if (s == "nand") return NcmStorageId_BuiltInUser;
    if (s == "gamecard") return NcmStorageId_GameCard;
    return NcmStorageId_SdCard;
}

Data Load() {
    Data out;
    auto doc = yyjson_read_file(TITLES_JSON, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        log_write("[demo] no %s\n", TITLES_JSON);
        return out;
    }

    const auto root = yyjson_doc_get_root(doc);
    size_t idx, max;
    yyjson_val* j;
    yyjson_arr_foreach(yyjson_obj_get(root, "titles"), idx, max, j) {
        Title t;
        t.id = std::strtoull(Str(j, "id").c_str(), nullptr, 16);
        if (!t.id) {
            continue;
        }

        size_t i2, m2;
        yyjson_val *k, *v;
        yyjson_obj_foreach(yyjson_obj_get(j, "name"), i2, m2, k, v) {
            t.names.emplace_back(yyjson_get_str(k), yyjson_get_str(v) ? yyjson_get_str(v) : "");
        }
        t.publisher = Str(j, "publisher");
        t.version = Str(j, "version");
        t.icon = Str(j, "icon");
        t.build_id = Str(j, "build_id");
        t.storage = StorageId(Str(j, "storage"));
        t.size = yyjson_get_sint(yyjson_obj_get(j, "size"));
        t.dlc = yyjson_get_int(yyjson_obj_get(j, "dlc"));

        yyjson_val* e;
        yyjson_arr_foreach(yyjson_obj_get(j, "updates"), i2, m2, e) {
            t.updates.push_back({(u32)yyjson_get_uint(yyjson_obj_get(e, "version")), Str(e, "display"), yyjson_get_sint(yyjson_obj_get(e, "size"))});
        }
        yyjson_arr_foreach(yyjson_obj_get(j, "saves"), i2, m2, e) {
            t.saves.push_back({yyjson_get_int(yyjson_obj_get(e, "user")), yyjson_get_sint(yyjson_obj_get(e, "size"))});
        }
        out.titles.emplace_back(std::move(t));
    }

    const auto nand = yyjson_obj_get(root, "nand");
    out.nand_free = yyjson_get_sint(yyjson_obj_get(nand, "free"));
    out.nand_total = yyjson_get_sint(yyjson_obj_get(nand, "total"));
    out.emummc = yyjson_get_bool(yyjson_obj_get(nand, "emummc"));

    yyjson_doc_free(doc);
    log_write("[demo] %zu titles\n", out.titles.size());
    return out;
}

const Data& Get() {
    static const auto data = Load();
    return data;
}

} // namespace

std::span<const Title> Titles() {
    return Get().titles;
}

const Title* FindTitle(u64 id) {
    const u64 base = id & ~0x1FFFull;
    for (const auto& t : Titles()) {
        if (t.id == base) {
            return &t;
        }
    }
    return nullptr;
}

std::string Name(const Title& t) {
    const auto code = i18n::GetCurrentLanguageCode();
    std::string en;
    for (const auto& [lang, name] : t.names) {
        if (lang == code) return name;
        if (lang == "en") en = name;
    }
    return en;
}

std::vector<u8> Icon(const Title& t) {
    std::vector<u8> out;
    const auto path = std::string{DEMO_DIR} + t.icon;
    if (auto f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        out.resize(std::max(0l, std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        out.resize(std::fread(out.data(), 1, out.size(), f));
        std::fclose(f);
    }
    return out;
}

bool NandSpace(s64* free, s64* total) {
    const auto& d = Get();
    if (d.nand_total <= 0) {
        return false;
    }
    if (free) *free = d.nand_free;
    if (total) *total = d.nand_total;
    return true;
}

bool EmuNand() {
    return Get().emummc;
}

} // namespace sphaira::demo
