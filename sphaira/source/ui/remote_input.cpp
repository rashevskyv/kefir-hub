#include "ui/remote_input.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "web.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"

#include <mutex>
#include <switch.h>

namespace sphaira::ui::remote_input {
namespace {

std::mutex g_mutex;
bool g_active{false};
std::string g_received_text;
Options g_current_options;

} // namespace

void SetRemoteInputActive(bool active) {
    std::lock_guard lock(g_mutex);
    g_active = active;
    if (!active) {
        g_received_text.clear();
    }
}

auto IsRemoteInputActive() -> bool {
    std::lock_guard lock(g_mutex);
    return g_active;
}

void SetReceivedText(const std::string& text) {
    std::lock_guard lock(g_mutex);
    g_received_text = text;
}

auto GetReceivedText() -> std::string {
    std::lock_guard lock(g_mutex);
    return g_received_text;
}

auto GetCurrentOptions() -> Options {
    std::lock_guard lock(g_mutex);
    return g_current_options;
}

void RequestRemoteText(const Options& options, OnCompleteCallback on_complete) {
    {
        std::lock_guard lock(g_mutex);
        g_current_options = options;
    }
    SetRemoteInputActive(true);

    const auto was_running = WebShareIsRunning();
    WebShareResult share{};
    const auto rc = WebStartServer("/input", share);
    if (R_FAILED(rc)) {
        SetRemoteInputActive(false);
        App::PushErrorBox(rc, "Could not start the web server"_i18n);
        return;
    }

    App::Push<ProgressBox>(
        share.qr_image, options.title, share.url,
        [](auto pbox) -> Result {
            pbox->NewTransferForce(App::IsApplet()
                ? "Applet Mode: keep this screen open; use the same Wi-Fi. Press B to cancel."_i18n
                : "Scan the QR code with phone or open URL on PC to send text. Press B to cancel."_i18n);

            while (!pbox->ShouldExit()) {
                if (!GetReceivedText().empty()) {
                    R_SUCCEED();
                }
                svcSleepThread(150'000'000);
            }

            R_THROW(Result_TransferCancelled);
        },
        [on_complete, was_running](Result rc) {
            const auto text = GetReceivedText();
            SetRemoteInputActive(false);
            if (!was_running) {
                WebShareStop();
            }

            if (rc == Result_TransferCancelled) {
                App::Notify("Input cancelled"_i18n);
                return;
            }

            if (R_FAILED(rc) || text.empty()) {
                App::Notify("No input received"_i18n);
                return;
            }

            App::Notify("Received input from phone/PC"_i18n);
            if (on_complete) {
                on_complete(text);
            }
        }
    );
}

void PromptTextInput(const Options& options, OnCompleteCallback on_complete) {
    App::Push<OptionBox>(
        options.guide + "\n\n" + "Select input method:"_i18n,
        "Manual (Keyboard)"_i18n, "From Phone / PC"_i18n, 1,
        [options, on_complete](auto op_index) {
            if (!op_index) {
                return;
            }
            if (*op_index == 0) {
                // Manual entry on console
                std::string out;
                if (R_SUCCEEDED(swkbd::ShowText(out, options.guide.c_str(), options.default_text.c_str(), options.min_length, options.max_length)) && !out.empty()) {
                    if (on_complete) {
                        on_complete(out);
                    }
                }
            } else if (*op_index == 1) {
                // Remote transfer from phone/PC
                RequestRemoteText(options, on_complete);
            }
        }
    );
}

} // namespace sphaira::ui::remote_input
