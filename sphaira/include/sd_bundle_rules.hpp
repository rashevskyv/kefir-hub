#pragma once

// microSD bundles from DBI Backend Qt (2.9.11+): a queue entry named "<game> - <option>.kefir-sd.tar"
// is a tar of files for the microSD. Members are written at their own path; ".kefir/rules"
// ("archive prefix<TAB>microSD prefix" lines) maps the entries of the archive member that follows
// it under ".kefir/". Pure logic, host-tested in tests/test_sd_bundle_rules.cpp.

#include "path_util.hpp"
#include "zip_extract_plan.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::sd_bundle {

inline constexpr std::string_view SUFFIX = ".kefir-sd.tar";
inline constexpr std::string_view META_DIR = ".kefir/";
inline constexpr std::string_view RULES = ".kefir/rules";

struct Rule {
    std::string src; // ends in '/' for a folder, else one exact entry
    std::string dst;
};

inline auto IsBundleName(std::string_view name) -> bool {
    return name.size() > SUFFIX.size() && name.ends_with(SUFFIX);
}

inline auto ParseRules(std::string_view text) -> std::vector<Rule> {
    std::vector<Rule> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto tab = line.find('\t');
        if (tab == line.npos || tab == 0) {
            continue;
        }
        out.push_back({std::string{line.substr(0, tab)}, std::string{line.substr(tab + 1)}});
    }
    return out;
}

// a member or archive entry name as a clean relative path ("./a\\b" -> "a/b"); nullopt if unsafe.
inline auto CleanEntry(std::string_view entry) -> std::optional<std::string> {
    auto n = zip_extract::NormalizeZipEntry(entry);
    while (n.starts_with("./")) {
        n.erase(0, 2);
    }
    if (!path::IsSafeArchiveEntry(n)) {
        return std::nullopt;
    }
    return n;
}

// microSD path for an archive entry: the first rule whose src is the entry or a folder holding it.
// nullopt = not part of the option (readme, other language), skip it.
inline auto MapEntry(std::string_view entry, const std::vector<Rule>& rules) -> std::optional<std::string> {
    const auto clean = CleanEntry(entry);
    if (!clean || clean->ends_with('/')) {
        return std::nullopt; // folders come with their files
    }
    for (const auto& r : rules) {
        if (*clean == r.src) {
            return r.dst;
        }
        if (r.src.ends_with('/') && clean->starts_with(r.src) && clean->size() > r.src.size()) {
            auto out = r.dst + clean->substr(r.src.size());
            if (CleanEntry(out)) {
                return out;
            }
        }
    }
    return std::nullopt;
}

// never written from a bundle: system folders of the microSD.
inline auto IsWritable(std::string_view sd_path) -> bool {
    if (!CleanEntry(sd_path) || sd_path.starts_with(META_DIR)) {
        return false;
    }
    const auto first = sd_path.substr(0, sd_path.find('/'));
    return first != "Nintendo" && first != "emuMMC" && first != "bootloader";
}

// a port's own save: kept when the console already has it (a reinstall must not reset progress).
inline auto IsSaveData(std::string_view sd_path) -> bool {
    size_t start = 0;
    while (start < sd_path.size()) {
        auto end = sd_path.find('/', start);
        if (end == sd_path.npos) {
            break; // the file name itself does not count
        }
        auto part = sd_path.substr(start, end - start);
        std::string lower{part};
        for (auto& c : lower) {
            c = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
        }
        if (lower == "save" || lower == "saves" || lower == "savedata" || lower == "userdata") {
            return true;
        }
        start = end + 1;
    }
    return false;
}

} // namespace sphaira::sd_bundle
