#pragma once

// The pure part of the Updater's Apps tab: which updater an app uses and what its row says.
// libnx-free, so tests/test_kefir_apps_state.cpp runs it on the host.

#include "version_compare.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace sphaira::ui::menu::kefir {

// what the row of one app says
enum class AppState {
    Update, // a newer version is out
    Latest, // the installed version is the newest
    NoInfo, // no source knows this app, or its source did not answer
};

// where an app gets its update from
enum class AppSource {
    Hub,   // Kefir Hub itself: its GitHub release
    Dbi,   // DBI: the translated build of DBIPatcher, with the language the card has
    Store, // every other app: the App Store sources, matched by the nro path
};

constexpr std::string_view DBI_NRO_PATH = "/switch/DBI/DBI.nro";

inline auto SamePath(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// hub_path: the file the Hub's own update writes. The Hub and DBI never take a store entry,
// so a store package cannot replace the running Hub or the translated DBI.
inline auto SourceOf(std::string_view nro_path, std::string_view hub_path) -> AppSource {
    if (SamePath(nro_path, hub_path)) {
        return AppSource::Hub;
    }
    if (SamePath(nro_path, DBI_NRO_PATH)) {
        return AppSource::Dbi;
    }
    return AppSource::Store;
}

// Hub and DBI: the installed version against the newest release tag. Never offers a downgrade.
inline auto AppStateOf(const std::string& installed, const std::string& latest) -> AppState {
    if (latest.empty()) {
        return AppState::NoInfo;
    }
    return version::IsNewer(installed, latest) ? AppState::Update : AppState::Latest;
}

// the record of the DBI language: the release asset of the translation, "translation_ua.bin".
// It is written beside translation.bin when a language is installed; an empty record is no record.
constexpr std::string_view DBI_LANGUAGE_RECORD = "/switch/DBI/translation.lang";
constexpr std::string_view DBI_RELEASE_DOWNLOAD = "https://github.com/rashevskyv/DBIPatcher/releases/latest/download/";

inline auto DbiLanguageAsset(std::string_view record) -> std::string {
    while (!record.empty() && (record.back() == '\n' || record.back() == '\r' || record.back() == ' ')) {
        record.remove_suffix(1);
    }
    // only a plain asset name: the record is a file on the card, anything else is not ours
    if (!record.starts_with("translation_") || !record.ends_with(".bin") ||
        record.find_first_of("/\\") != std::string_view::npos) {
        return {};
    }
    return std::string(record);
}

} // namespace sphaira::ui::menu::kefir
