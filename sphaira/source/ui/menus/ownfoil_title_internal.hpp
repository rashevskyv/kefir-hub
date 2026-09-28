#pragma once

#include "ui/menus/ownfoil_title.hpp"
#include "ui/menus/ownfoil.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_multi_select.hpp"
#include "ui/progress_box.hpp"
#include "yati/yati.hpp"
#include "yati/source/http.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "image.hpp"
#include "download.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <utility>

namespace sphaira::ui::menu::ownfoil {

namespace api = sphaira::ownfoil::api;
namespace installed = sphaira::ownfoil::installed;

// the left column is a page of its own. unscrolled its top is drawn at PAGE_Y;
// scrolled, it runs up under the heading's line and down to the bottom bar's.
constexpr float PAGE_X = 54.f;
constexpr float PAGE_Y = 104.f;
constexpr float PAGE_W = 720.f;
constexpr float CLIP_Y = 87.f;
constexpr float CLIP_BOTTOM = 646.f;
constexpr float VIEW_H = CLIP_BOTTOM - PAGE_Y;
// a highlight's outline is drawn outside what it surrounds.
constexpr float CLIP_BLEED = 10.f;
// below the last thing on the page, so it doesn't end flush with the bar.
constexpr float PAGE_PAD = 24.f;

// a little under the width of the old image, which leaves room below the
// thumbnails for the tagline to show: that is what says the page goes on.
constexpr float IMAGE_H = 405.f;
constexpr float IMAGE_ROUNDING = 6.f;
constexpr float ICON_SIZE = 256.f;

constexpr float THUMB_GAP_Y = 16.f;
constexpr float THUMB_W = 94.f;
constexpr float THUMB_H = 53.f;
constexpr float THUMB_GAP = 10.f;
// as many as fit under the big image.
constexpr s64 THUMB_COUNT = 7;

constexpr float SECTION_GAP = 30.f;
constexpr float INTRO_SIZE = 24.f;
constexpr float INTRO_LINE = 32.f;
constexpr float INTRO_GAP = 12.f;
constexpr float TEXT_SIZE = 19.f;
constexpr float TEXT_LINE = 30.f;

constexpr float HEADING_SIZE = 24.f;
constexpr float HEADING_H = 44.f;
constexpr float DLC_ROW_H = 92.f;
constexpr float DLC_IMAGE_W = 128.f;
constexpr float DLC_IMAGE_H = 72.f;

// a press moves the page a third of a screen; held, the right stick moves it a
// step every frame.
constexpr float SCROLL_STEP = 180.f;
constexpr float STICK_STEP = 10.f;
// how much of the way to its target the page glides each frame: the stick moves
// the target every frame, a press moves it once and glides there more slowly.
constexpr float STICK_EASE = 0.3f;
constexpr float STEP_EASE = 0.12f;
constexpr float SCROLLBAR_X = 788.f;
constexpr float SCROLLBAR_H = 526.f;

constexpr float COLUMN_X = 806.f;
constexpr float COLUMN_Y = 104.f;
constexpr float COLUMN_W = 420.f;
// short enough that a two-line name and all eight rows fit, one of them a value
// wrapped onto a second line.
constexpr float ROW_H = 38.f;
constexpr float VALUE_SIZE = 18.f;
constexpr float VALUE_LINE = 22.f;
constexpr float GROUP_GAP = 18.f;
// room for the longest label, "Available version"; the value gets the rest.
constexpr float LABEL_W = 150.f;

// the regions whose ratings are pegi ages. anywhere else titledb's number is
// shown as a bare age: naming the wrong board would be worse than naming none.
constexpr const char* PEGI_REGIONS[] = {
    "AT", "BE", "CH", "CZ", "DE", "DK", "ES", "FI", "FR", "GB",
    "GR", "HU", "IE", "IT", "NL", "NO", "PL", "PT", "SE", "SK",
};

constexpr const char* MONTHS[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

// the catalogue spells a release date yyyymmdd, and the version history spells
// one yyyy-mm-dd. either reads as "7 Aug 2018"; anything else is shown as sent.
inline auto FormatDate(const std::string& date) -> std::string {
    std::string digits;
    for (const auto c : date) {
        if (c >= '0' && c <= '9') {
            digits += c;
        } else if (c != '-') {
            return date;
        }
    }

    if (digits.size() != 8) {
        return date;
    }

    const auto year = std::atoi(digits.substr(0, 4).c_str());
    const auto month = std::atoi(digits.substr(4, 2).c_str());
    const auto day = std::atoi(digits.substr(6, 2).c_str());
    if (month < 1 || month > 12) {
        return date;
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d %s %d", day, i18n::get(MONTHS[month - 1]).c_str(), year);
    return buf;
}

// what the game calls itself with the patch level beside it, the version over
// 65536 as ownfoil and the meta menu both spell it. `name` is empty for a version
// the shop doesn't hold, which has no string anywhere.
inline auto FormatVersion(const std::string& name, s64 version) -> std::string {
    char level[24];
    std::snprintf(level, sizeof(level), "v%ld", version >> 16);
    return name.empty() ? std::string{level} : name + " (" + level + ")";
}

// the base game by name, being the one version with no patch level to tell it
// apart, and every update the way the version rows beside it are written.
inline auto FormatInstallVersion(const api::ShopVersion& version) -> std::string {
    if (version.version > 0) {
        return FormatVersion(version.display, version.version);
    }

    const auto base = "Base"_i18n;
    return version.display.empty() ? base : base + " (" + version.display + ")";
}

inline auto FormatRating(const std::string& rating, const std::string& region) -> std::string {
    if (rating.empty()) {
        return {};
    }

    for (const auto pegi : PEGI_REGIONS) {
        if (region == pegi) {
            return "PEGI " + rating;
        }
    }

    return rating + "+";
}

// a dlc's name usually leads with its game's, which the page already says, so
// "Dead Cells: The Bad Seed" is listed as "The Bad Seed".
inline auto StripGameName(const std::string& name, const std::string& game) -> std::string {
    if (game.empty() || name.size() <= game.size() || name.compare(0, game.size(), game) != 0) {
        return name;
    }

    constexpr std::string_view en_dash{"–"};
    std::string_view rest{name};
    rest.remove_prefix(game.size());
    const auto joined = rest.size();

    while (!rest.empty()) {
        if (rest.front() == ' ' || rest.front() == ':' || rest.front() == '-') {
            rest.remove_prefix(1);
        } else if (rest.starts_with(en_dash)) {
            rest.remove_prefix(en_dash.size());
        } else {
            break;
        }
    }

    // nothing joined them, so the game's name was only the start of a word.
    if (rest.empty() || rest.size() == joined) {
        return name;
    }
    return std::string{rest};
}

// the shop spells an id as 16 hex digits, and yati compares the number.
inline auto ParseId(const std::string& id) -> u64 {
    return std::strtoull(id.c_str(), nullptr, 16);
}

inline auto IsSpace(char c) -> bool {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

// the catalogue breaks a tagline wherever the eshop's narrow box wanted it and
// pads it with spaces; as a heading or a row it reads as one run of text.
inline auto Flatten(const std::string& text) -> std::string {
    std::string out;
    for (const auto c : text) {
        if (!IsSpace(c)) {
            out += c;
        } else if (!out.empty() && out.back() != ' ') {
            out += ' ';
        }
    }

    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

// runs on the page's worker, so it decodes with stb rather than the hardware jpeg
// decoder: that is one instance shared with the main thread, and unguarded.
inline auto FetchImage(const sphaira::ownfoil::Config& config, const std::string& id, const api::ShopImage& image, const char* kind, std::stop_token token) -> ImageResult {
    if (image.url.empty()) {
        return {};
    }

    const auto path = BuildImageCache(id, image.url, kind);

    // the cache is keyed on the url, and a url names its bytes, so whatever is
    // already there is the image - unless it is a download that never finished.
    if (fs::FsNativeSd().FileExists(path)) {
        auto cached = ImageLoadFromFile(path, ImageFlag_None);
        if (!cached.data.empty()) {
            return cached;
        }
    }

    // the shop gates its own copies like its catalogue; a hotlink points at a cdn
    // that must not get the shop's credentials.
    const auto result = curl::Api().ToFile(
        curl::Url{image.url},
        curl::Path{path},
        curl::UserPass{image.local ? config.user : "", image.local ? config.pass : ""},
        curl::PreemptiveAuth{image.local},
        curl::StopToken{token}
    );

    if (!result.success) {
        return {};
    }

    return ImageLoadFromFile(path, ImageFlag_None);
}

inline auto CreateTexture(const ImageResult& image) -> int {
    if (image.data.empty()) {
        return 0;
    }
    return nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
}

inline void DeleteTexture(int image) {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

} // namespace sphaira::ui::menu::ownfoil
