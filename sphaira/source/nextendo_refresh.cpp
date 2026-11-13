// Reads the server IPs of the newest Prelude-Nextendo release (see nextendo_refresh.hpp). Runs only while
// Nextendo is active, at most once a day, in the background; a failure keeps the IPs that are already in use.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include "nextendo_refresh.hpp"

#include "download.hpp"
#include "log.hpp"
#include "nextendo_apply.hpp"
#include "nextendo_online.hpp"
#include "option.hpp"

#include <yyjson.h>

#include <ctime>
#include <stop_token>
#include <string>

namespace sphaira::nextendo {

namespace {

option::OptionLong g_last_check{"nextendo", "last_refresh", 0};

// Online counts: main thread only (OnComplete arrives through the event loop, see download.cpp).
OnlineCounts g_online;
bool g_online_on{};
bool g_online_busy{};
std::int64_t g_online_checked{}; // last time Active() was read and a request sent
std::stop_source g_online_stop;

} // namespace

auto Active() -> bool {
    if (!FirmwareOk()) {
        return false;
    }
    switch (CurrentMode()) {
        case Mode::EmuMMC: return EmuEnabled();
        case Mode::SemiStock: return SysEnabled();
        case Mode::Other: break;
    }
    return false;
}

namespace {

auto TagFromRelease(const std::vector<u8>& body) -> std::string {
    yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(body.data()), body.size(), 0);
    if (!doc) {
        return {};
    }
    std::string tag;
    yyjson_val* val = yyjson_obj_get(yyjson_doc_get_root(doc), "tag_name");
    if (yyjson_is_str(val)) {
        tag = yyjson_get_str(val);
    }
    yyjson_doc_free(doc);
    return IsSafeTag(tag) ? tag : std::string{};
}

void OnConfig(const curl::ApiResult& result) {
    if (!result.success) {
        log_write("[nextendo] config download failed (HTTP %ld)\n", result.code);
        return;
    }
    const auto fresh = ParseConfigIps(std::string{result.data.begin(), result.data.end()});
    g_last_check.Set(static_cast<long>(std::time(nullptr)));
    if (!fresh || !ArePublic(*fresh)) {
        log_write("[nextendo] release config has no usable server IPs; keeping the current ones\n");
        return;
    }
    const auto now = ServerIpsInUse();
    if (fresh->main == now.main && fresh->nncs2 == now.nncs2) {
        return;
    }
    log_write("[nextendo] server IP changed: %s -> %s\n", now.main.c_str(), fresh->main.c_str());
    StoreServerIps(*fresh);
    bool changed{};
    if (R_FAILED(Reconcile(changed))) {
        log_write("[nextendo] reconcile after the IP refresh failed\n");
    }
}

} // namespace

void RefreshServerIpsAsync() {
    if (!Active() || !RefreshDue(std::time(nullptr), g_last_check.Get())) {
        return; // Nextendo off: no network call at all
    }
    curl::Api().ToMemoryAsync(
        curl::Url{LatestReleaseUrl()},
        curl::Header{{"Accept", "application/json"}},
        curl::StopToken{},
        curl::OnComplete{[](auto& result) {
            const auto tag = result.success ? TagFromRelease(result.data) : std::string{};
            if (tag.empty()) {
                log_write("[nextendo] latest release lookup failed (HTTP %ld)\n", result.code);
                return false;
            }
            curl::Api().ToMemoryAsync(
                curl::Url{ConfigUrl(tag)},
                curl::StopToken{},
                curl::OnComplete{[](auto& cfg) {
                    OnConfig(cfg);
                    return true;
                }}
            );
            return true;
        }}
    );
}

void OnlineCountsTick() {
    const std::int64_t now = std::time(nullptr);
    if (g_online_busy) {
        if (now - g_online_checked >= kOnlineCountsTimeoutSeconds) {
            g_online_stop.request_stop(); // the curl callbacks abort; a stopped request never calls OnComplete
            g_online_busy = false;
        }
        return;
    }
    if (!OnlineCountsDue(now, g_online_checked)) {
        return;
    }
    g_online_checked = now; // stamped on send: no network means one try a minute, not one a frame
    g_online_on = Active();
    if (!g_online_on) {
        g_online.clear(); // Nextendo off: no network call, no badge
        return;
    }
    g_online_stop = {};
    g_online_busy = curl::Api().ToMemoryAsync(
        curl::Url{std::string{kOnlineCountsUrl}},
        curl::Header{{"Accept", "application/json"}},
        g_online_stop.get_token(),
        curl::OnComplete{[](auto& result) {
            g_online_busy = false;
            if (!result.success) {
                return; // keep the last good counts
            }
            if (auto counts = ParseOnlineCounts({reinterpret_cast<const char*>(result.data.data()), result.data.size()})) {
                g_online = std::move(*counts);
            }
        }}
    );
}

void OnlineCountsRecheck() {
    const bool was_on = g_online_on;
    g_online_on = Active();
    if (!g_online_on) {
        g_online.clear();
    } else if (!was_on && !g_online_busy) {
        g_online_checked = 0; // just switched on: ask now; otherwise the minute between requests holds
    }
}

auto OnlinePlayers(u64 app_id) -> std::optional<int> {
    if (!g_online_on) {
        return std::nullopt;
    }
    const auto it = g_online.find(app_id);
    return it == g_online.end() ? std::nullopt : std::optional<int>{it->second};
}

} // namespace sphaira::nextendo
