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
    Github, // an app of GITHUB_APPS: its own GitHub release
};

constexpr std::string_view DBI_NRO_PATH = "/switch/DBI/DBI.nro";

// The apps that Tools → Software downloads straight from their GitHub release (they are not in the App Store).
// The Software menu lists them; the Updater finds them on the card by nro_path and checks the same release.
struct GithubApp {
    std::string_view name;        // the menu label (an en.json key)
    std::string_view description; // en.json key
    std::string_view downloading; // en.json key of the progress text
    std::string_view repo;        // owner/name on github.com
    std::string_view asset;       // the nro in the release
    std::string_view nro_path;    // where it goes on the card
    std::string_view icon;
    bool ukrainian_only;          // shown in the Software menu only with the Ukrainian Hub language
};

inline constexpr GithubApp GITHUB_APPS[] = {
    {"UAModDownloader", "Ukrainian mods.", "Downloading UAModDownloader...", "pro100luk/UAModDownloader",
        "UAModDownloader.nro", "/switch/UAModDownloader/UAModDownloader.nro", "romfs:/theme/uamoddownloader.jpg", true},
    // UAPlay's NSP forwarder starts sdmc:/switch/UAPlay.nro, so the file goes there, not to a folder
    {"UAPlay", "Ukrainian films and series.", "Downloading UAPlay...", "Small182/UAPlay-nx",
        "UAPlay.nro", "/switch/UAPlay.nro", "romfs:/theme/uaplay.jpg", true},
    {"ModCD", "ECLIPS graphic mods.", "Downloading ModCD...", "kawaii-flesh/ModCD",
        "ModCD.nro", "/switch/ModCD/ModCD.nro", "romfs:/theme/modcd.jpg", false},
    {"SimpleModDownloader", "Game mods from GameBanana.", "Downloading SimpleModDownloader...", "PoloNX/SimpleModDownloader",
        "SimpleModDownloader.nro", "/switch/SimpleModDownloader/SimpleModDownloader.nro", "romfs:/theme/simplemoddownloader.jpg", false},
};

inline auto GithubDownloadUrl(const GithubApp& app) -> std::string {
    return "https://github.com/" + std::string(app.repo) + "/releases/latest/download/" + std::string(app.asset);
}

inline auto GithubReleaseApiUrl(const GithubApp& app) -> std::string {
    return "https://api.github.com/repos/" + std::string(app.repo) + "/releases/latest";
}

// the temporary file of a download: "/switch/UAPlay.nro" -> "/switch/UAPlay_new.nro"
inline auto GithubTempPath(const GithubApp& app) -> std::string {
    auto path = std::string(app.nro_path);
    return path.insert(path.size() - 4, "_new");
}

// a release tag as a version: "v1.6.0" -> "1.6.0", "UAPlay_0.5.1" -> "0.5.1"
inline auto ReleaseVersion(std::string_view tag) -> std::string {
    const auto digit = tag.find_first_of("0123456789");
    return digit == std::string_view::npos ? std::string{} : std::string(tag.substr(digit));
}

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

inline auto FindGithubApp(std::string_view nro_path) -> const GithubApp* {
    for (const auto& app : GITHUB_APPS) {
        if (SamePath(nro_path, app.nro_path)) {
            return &app;
        }
    }
    return nullptr;
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
    if (FindGithubApp(nro_path)) {
        return AppSource::Github;
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

// RetroArch comes as the buildbot nightly, which has no version number: its Last-Modified is the version.
// "Mon, 26 Oct 2026 00:11:21 GMT" -> "2026-10-26"; empty when it is not an HTTP date
inline auto HttpDateDay(std::string_view date) -> std::string {
    constexpr std::string_view months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto comma = date.find(", ");
    if (comma == std::string_view::npos || date.size() < comma + 13) {
        return {};
    }
    const auto day = date.substr(comma + 2, 2);
    const auto month = date.substr(comma + 5, 3);
    const auto year = date.substr(comma + 9, 4);
    for (size_t m = 0; m < 12; m++) {
        if (month == months[m]) {
            const char mm[] = {static_cast<char>('0' + (m + 1) / 10), static_cast<char>('0' + (m + 1) % 10), 0};
            return std::string(year) + "-" + mm + "-" + std::string(day);
        }
    }
    return {};
}

// installed: the Last-Modified the Hub recorded when it installed the nightly; latest: the buildbot's one now.
// Any other build than the buildbot's current one is an update: the server is the only source.
inline auto NightlyState(std::string_view installed, std::string_view latest) -> AppState {
    if (latest.empty()) {
        return AppState::NoInfo;
    }
    return installed == latest ? AppState::Latest : AppState::Update;
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
