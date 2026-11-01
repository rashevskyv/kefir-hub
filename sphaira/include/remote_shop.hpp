#pragma once

// What a Tinfoil style shop lists, in plain functions. Two kinds of server answer with it:
//   an AeroFoil shop:  {"sections": [{"id": "all", "title": "All", "items": [{"url": "...", "size": 1, "name": "..."}]}]}
//   a legacy index:    {"files": ["url" | {"url": "...", "size": 1}], "directories": ["url"], "success": "message of the day"}
// A file url may be relative to the index, and may end in "#name.nsp": the part after the hash is the file's name.
// Nothing here touches switch.h: it is tested on the host.

#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::shop {

enum class Kind { Base, Update, Dlc };

struct Item {
    std::string name{};
    std::string url{};         // absolute
    std::uint64_t size{};      // 0 when the shop did not say
    std::uint64_t title_id{};  // from the name, 0 when it holds none
    std::uint32_t version{};
    bool has_version{};
};

inline auto UrlDecode(std::string_view s) -> std::string {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// the url without its "#fragment"
inline auto SplitFragment(std::string_view url, std::string& fragment) -> std::string {
    const auto hash = url.find('#');
    if (hash == std::string_view::npos) {
        fragment.clear();
        return std::string{url};
    }
    fragment = std::string{url.substr(hash + 1)};
    return std::string{url.substr(0, hash)};
}

// an absolute url from one that may be relative to the index it came from
inline auto ResolveUrl(std::string_view base, std::string_view ref) -> std::string {
    if (ref.empty()) {
        return {};
    }
    if (ref.find("://") != std::string_view::npos) {
        return std::string{ref};
    }

    // the index url without its query or fragment
    base = base.substr(0, base.find_first_of("?#"));
    const auto scheme_end = base.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string{ref};
    }

    if (ref.starts_with("//")) {
        return std::string{base.substr(0, scheme_end + 1)} + std::string{ref};
    }

    const auto path_start = base.find('/', scheme_end + 3);
    const auto origin = base.substr(0, path_start);
    if (ref.starts_with('/')) {
        return std::string{origin} + std::string{ref};
    }

    // relative to the folder of the index
    std::string_view dir{"/"};
    if (path_start != std::string_view::npos) {
        const auto path = base.substr(path_start);
        dir = path.substr(0, path.rfind('/') + 1);
    }
    return std::string{origin} + std::string{dir} + std::string{ref};
}

// the file name to show: the shop's own, else the "#name" of the url, else the last piece of its path
inline auto NameOf(std::string_view url, std::string_view fragment, std::string_view explicit_name) -> std::string {
    if (!explicit_name.empty()) {
        return std::string{explicit_name};
    }
    if (!fragment.empty()) {
        return UrlDecode(fragment);
    }

    url = url.substr(0, url.find_first_of("?#"));
    const auto slash = url.rfind('/');
    return UrlDecode(slash == std::string_view::npos ? url : url.substr(slash + 1));
}

// the first "[0100000000000000]" of the name: 16 hex digits between brackets
inline auto ParseTitleId(std::string_view name, std::uint64_t& id) -> bool {
    for (size_t i = 0; i + 18 <= name.size(); i++) {
        if (name[i] != '[' || name[i + 17] != ']') {
            continue;
        }

        std::uint64_t value{};
        bool ok = true;
        for (size_t j = 1; j <= 16 && ok; j++) {
            const char c = name[i + j];
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            if (digit < 0) {
                ok = false;
            } else {
                value = value * 16 + static_cast<std::uint64_t>(digit);
            }
        }
        if (ok) {
            id = value;
            return true;
        }
    }
    return false;
}

// "[v65536]"
inline auto ParseVersion(std::string_view name, std::uint32_t& version) -> bool {
    for (size_t i = 0; i + 3 <= name.size(); i++) {
        if (name[i] != '[' || name[i + 1] != 'v') {
            continue;
        }

        std::uint64_t value{};
        size_t j = i + 2;
        for (; j < name.size() && name[j] >= '0' && name[j] <= '9' && value < 0xFFFFFFFFull; j++) {
            value = value * 10 + static_cast<std::uint64_t>(name[j] - '0');
        }
        if (j > i + 2 && j < name.size() && name[j] == ']' && value <= 0xFFFFFFFFull) {
            version = static_cast<std::uint32_t>(value);
            return true;
        }
    }
    return false;
}

// a game ends in 000 in its title id, its update in 800, an add-on in whatever follows
inline auto KindOf(std::uint64_t title_id) -> Kind {
    const auto low = title_id & 0xFFF;
    if (low == 0) return Kind::Base;
    if (low == 0x800) return Kind::Update;
    return Kind::Dlc;
}

// nsp, nsz, xci or xcz; empty when the name says nothing of the kind: yati picks the container by it
inline auto Extension(std::string_view name) -> std::string {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return {};
    }

    std::string ext{name.substr(dot + 1)};
    for (auto& c : ext) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return ext == "nsp" || ext == "nsz" || ext == "xci" || ext == "xcz" ? ext : std::string{};
}

// one entry of an index, from what its json gave
inline auto MakeItem(std::string_view base, std::string_view raw_url, std::string_view explicit_name, std::uint64_t size) -> Item {
    Item item;
    std::string fragment;
    const auto path = SplitFragment(raw_url, fragment);
    item.url = ResolveUrl(base, path);
    item.name = NameOf(item.url, fragment, explicit_name);
    item.size = size;
    item.has_version = ParseVersion(item.name, item.version);
    ParseTitleId(item.name, item.title_id);
    return item;
}

} // namespace sphaira::shop
