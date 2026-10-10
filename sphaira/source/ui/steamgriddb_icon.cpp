#include "ui/steamgriddb_icon.hpp"
#include "ui/steamgriddb_widgets.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "option.hpp"
#include "web.hpp"
#include "ui/progress_box.hpp"

namespace sphaira::ui::steamgriddb {
namespace {

constexpr size_t MAX_IMAGE_DOWNLOAD_SIZE = 8 * 1024 * 1024;

option::OptionString g_api_key{"steamgriddb", "api_key", ""};
// the web handoff writes the key from the server thread, the ui polls it.
Mutex g_api_key_mutex;
std::string g_api_key_cache{};
bool g_api_key_cache_loaded{}; // guarded by g_api_key_mutex
std::atomic_bool g_web_request_active{false};

} // namespace

auto NormalizeIcon(std::span<const u8> icon) -> std::vector<u8> {
    if (icon.empty() || icon.size() > MAX_IMAGE_DOWNLOAD_SIZE) {
        return {};
    }
    return ImageNormalizeIcon(icon);
}

auto GetApiKey() -> std::string {
    SCOPED_MUTEX(&g_api_key_mutex);
    if (!g_api_key_cache_loaded) {
        g_api_key_cache = g_api_key.Get();
        g_api_key_cache_loaded = true;
    }
    return g_api_key_cache;
}

void SetApiKey(const std::string& key) {
    SCOPED_MUTEX(&g_api_key_mutex);
    g_api_key_cache = key;
    g_api_key_cache_loaded = true;
}

auto IsApiKeyWebRequestActive() -> bool {
    return g_web_request_active.load();
}

void SetApiKeyWebRequestActive(bool active) {
    g_web_request_active.store(active);
}

// puts the key entry on the user's phone: the console shows a qr for a page it
// serves itself, the phone links out to steamgriddb (where Steam's own login
// happens) and posts the key back. beats typing 32 hex chars with swkbd.
void RequestApiKey(std::function<void(std::string)> on_key) {
    SetApiKeyWebRequestActive(true);
    SetApiKey("");

    // don't leave a listener up that the user never asked for: only tear the
    // server down again if this handoff is what brought it up.
    const auto was_running = WebShareIsRunning();

    WebShareResult share{};
    const auto rc = WebStartServer("/apikey", share);
    if (R_FAILED(rc)) {
        SetApiKeyWebRequestActive(false);
        App::PushErrorBox(rc, "Could not start the web server"_i18n);
        return;
    }

    App::Push<ProgressBox>(
        share.qr_image, "SteamGridDB API key"_i18n, share.ip_url.substr(share.ip_url.find("://") + 3),
        [](auto pbox) -> Result {
            pbox->NewTransferForce(App::IsApplet()
                ? "Applet Mode: keep this screen open; use the same non-guest Wi-Fi. Press B to cancel."_i18n
                : "Scan the code with a phone on the same Wi-Fi, then paste the key there. Press B to cancel."_i18n);

            while (!pbox->ShouldExit()) {
                if (!GetApiKey().empty()) {
                    R_SUCCEED();
                }
                svcSleepThread(200'000'000);
            }

            R_THROW(Result_TransferCancelled);
        },
        [on_key, was_running](Result rc) {
            SetApiKeyWebRequestActive(false);
            if (!was_running) {
                WebShareStop();
            }

            const auto key = GetApiKey();
            if (R_FAILED(rc) || key.empty()) {
                App::Notify("A SteamGridDB API key is required"_i18n);
                return;
            }

            g_api_key.Set(key);
            App::Notify("SteamGridDB key saved"_i18n);
            if (on_key) {
                on_key(key);
            }
        }
    );
}

void ShowIconPicker(const std::string& title, const IconCallback& callback) {
    if (title.empty()) {
        App::Notify("Enter an App Title before searching"_i18n);
        return;
    }

    const auto api_key = GetApiKey();
    if (!api_key.empty()) {
        StartSearch(api_key, title, callback);
        return;
    }

    RequestApiKey([title, callback](std::string key){
        StartSearch(key, title, callback);
    });
}

} // namespace sphaira::ui::steamgriddb
