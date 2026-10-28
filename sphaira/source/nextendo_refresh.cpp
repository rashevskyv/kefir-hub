// Reads the server IPs of the newest Prelude-Nextendo release (see nextendo_refresh.hpp). Runs only while
// Nextendo is active, at most once a day, in the background; a failure keeps the IPs that are already in use.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include "nextendo_refresh.hpp"

#include "download.hpp"
#include "log.hpp"
#include "nextendo_apply.hpp"
#include "option.hpp"

#include <yyjson.h>

#include <ctime>
#include <string>

namespace sphaira::nextendo {

namespace {

option::OptionLong g_last_check{"nextendo", "last_refresh", 0};

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
        curl::Url{"https://api.github.com/repos/" + std::string{kRepo} + "/releases/latest"},
        curl::Header{{"Accept", "application/vnd.github+json"}},
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

} // namespace sphaira::nextendo
