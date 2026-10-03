#include "demo/demo_data.hpp"
#include "log.hpp"

#include <yyjson.h>
#include <cstdlib>
#include <vector>

namespace sphaira::demo {
namespace {

constexpr const char* TITLES_JSON = "/config/kefir/demo/titles.json";

struct Data {
    std::vector<u64> ids;
    s64 nand_free{};
    s64 nand_total{};
    bool emummc{};
};

Data Load() {
    Data out;
    auto doc = yyjson_read_file(TITLES_JSON, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        log_write("[demo] no %s\n", TITLES_JSON);
        return out;
    }

    const auto root = yyjson_doc_get_root(doc);
    size_t idx, max;
    yyjson_val* title;
    yyjson_arr_foreach(yyjson_obj_get(root, "titles"), idx, max, title) {
        if (const auto id = yyjson_get_str(yyjson_obj_get(title, "id"))) {
            out.ids.emplace_back(std::strtoull(id, nullptr, 16));
        }
    }

    const auto nand = yyjson_obj_get(root, "nand");
    out.nand_free = yyjson_get_sint(yyjson_obj_get(nand, "free"));
    out.nand_total = yyjson_get_sint(yyjson_obj_get(nand, "total"));
    out.emummc = yyjson_get_bool(yyjson_obj_get(nand, "emummc"));

    yyjson_doc_free(doc);
    log_write("[demo] %zu titles\n", out.ids.size());
    return out;
}

const Data& Get() {
    static const auto data = Load();
    return data;
}

} // namespace

std::span<const u64> TitleIds() {
    return Get().ids;
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
