#include "net.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/types.hpp"

#include <mutex>

namespace sphaira::net {
namespace {

// how long the gate waits for an ip after asking nifm to connect. long enough
// for a wi-fi association plus dhcp, short enough that a console with no known
// network in reach does not look hung.
constexpr u64 CONNECT_TIMEOUT_MS = 10'000;
constexpr u64 POLL_INTERVAL_NS = 250'000'000;

// nifm request kept open for as long as sphaira runs: it is what tells the os
// that we want the network up. created lazily, on the first gate that finds the
// console offline.
NifmRequest g_request{};
bool g_request_open{};
std::mutex g_mutex{};

// IsConnectedCached() is called once per entry per frame while drawing a menu,
// so the ipc behind it is rate limited.
TimeStamp g_cache_ts{};
bool g_cache_value{};
bool g_cache_valid{};

// airplane mode can only be cleared from nifm:a / nifm:s. sphaira asks for
// nifm:a on boot and falls back to nifm:u (see main.cpp), so on the fallback
// path this simply fails and the gate moves on to the request below.
void TryEnableWireless() {
    bool enabled{};
    if (R_FAILED(nifmIsWirelessCommunicationEnabled(&enabled))) {
        return;
    }

    if (enabled) {
        return;
    }

    const auto rc = nifmSetWirelessCommunicationEnabled(true);
    log_write("[NET] airplane mode was on, enabling wireless: 0x%X\n", R_VALUE(rc));
}

} // namespace

auto IsConnected() -> bool {
    u32 ip{};
    if (R_FAILED(nifmGetCurrentIpAddress(&ip))) {
        return false;
    }

    return ip != 0;
}

auto IsConnectedCached() -> bool {
    if (!g_cache_valid || g_cache_ts.GetMs() >= 1000) {
        g_cache_value = IsConnected();
        g_cache_valid = true;
        g_cache_ts.Update();
    }

    return g_cache_value;
}

auto TryConnect(u64 timeout_ms, const std::function<bool()>& cancelled) -> bool {
    if (IsConnected()) {
        return true;
    }

    std::scoped_lock lock{g_mutex};

    // another thread may have connected while we waited on the lock.
    if (IsConnected()) {
        return true;
    }

    TryEnableWireless();

    if (!g_request_open) {
        if (const auto rc = nifmCreateRequest(&g_request, true); R_FAILED(rc)) {
            log_write("[NET] nifmCreateRequest failed: 0x%X\n", R_VALUE(rc));
        } else {
            g_request_open = true;
        }
    }

    if (g_request_open) {
        // submit rather than submit-and-wait: the wait has no timeout and would
        // pin the worker thread for as long as the os keeps retrying.
        if (const auto rc = nifmRequestSubmit(&g_request); R_FAILED(rc)) {
            log_write("[NET] nifmRequestSubmit failed: 0x%X\n", R_VALUE(rc));
        }
    }

    TimeStamp ts{};
    while (ts.GetMs() < timeout_ms) {
        if (cancelled && cancelled()) {
            break;
        }

        if (IsConnected()) {
            g_cache_valid = false;
            return true;
        }

        svcSleepThread(POLL_INTERVAL_NS);
    }

    g_cache_valid = false;
    return IsConnected();
}

auto NoConnectionMessage() -> std::string {
    return "There is no internet connection.\n\n"
        "Turn on Wi-Fi (or plug in a LAN adapter) in the console's System Settings, then try again."_i18n;
}

auto IsOfflineError(Result rc) -> bool {
    if (R_SUCCEEDED(rc)) {
        return false;
    }

    if (rc == Result_NetNoConnection || R_MODULE(rc) == Module_Nifm) {
        return true;
    }

    // these all mean "a transfer did not happen". being offline explains them
    // completely, but only while we actually are offline -- the same codes come
    // up for a dead server or a bad url on a perfectly connected console.
    switch (rc) {
        case Result_CurlFailedEasyInit:
        case Result_AppFailedMusicDownload:
        case Result_AppstoreFailedZipDownload:
        case Result_GhdlFailedToDownloadAsset:
        case Result_GhdlFailedToDownloadAssetJson:
        case Result_ThemezerFailedToDownloadTheme:
        case Result_ThemezerFailedToDownloadThemeMeta:
        case Result_MainFailedToDownloadUpdate:
        case Result_DumpFailedNetworkUpload:
            return !IsConnected();
    }

    return false;
}

void ShowNoConnectionPopup(std::function<void()> on_retry) {
    if (!on_retry) {
        App::Push<ui::OptionBox>(NoConnectionMessage(), "OK"_i18n);
        return;
    }

    App::Push<ui::OptionBox>(
        NoConnectionMessage(), "Back"_i18n, "Try again"_i18n, 1,
        [on_retry = std::move(on_retry)](auto op_index) {
            if (op_index && *op_index) {
                on_retry();
            }
        }
    );
}

void RequireConnection(std::function<void()> on_connected) {
    if (!on_connected) {
        return;
    }

    if (IsConnected()) {
        on_connected();
        return;
    }

    App::Push<ui::ProgressBox>(
        0,
        "Network"_i18n,
        "Connecting..."_i18n,
        [](ui::ProgressBox* pbox) -> Result {
            pbox->NewTransferForce("Waiting for the console to connect..."_i18n);
            if (TryConnect(CONNECT_TIMEOUT_MS, [pbox](){ return pbox->ShouldExit(); })) {
                R_SUCCEED();
            }

            // a cancel is the user's answer, not a failure to explain.
            R_TRY(pbox->ShouldExitResult());
            R_THROW(Result_NetNoConnection);
        },
        [on_connected = std::move(on_connected)](Result rc) {
            if (R_SUCCEEDED(rc)) {
                on_connected();
            } else if (rc != Result_TransferCancelled) {
                ShowNoConnectionPopup([on_connected](){
                    RequireConnection(on_connected);
                });
            }
        }
    );
}

void Exit() {
    std::scoped_lock lock{g_mutex};

    if (g_request_open) {
        nifmRequestClose(&g_request);
        g_request_open = false;
    }
}

} // namespace sphaira::net
