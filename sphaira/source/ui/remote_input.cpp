#include "ui/remote_input.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "web.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"

#include <mutex>
#include <string_view>
#include <switch.h>

namespace sphaira::ui::remote_input {
namespace {

std::mutex g_mutex;
bool g_active{false};
bool g_received{false};
bool g_has_draft{false};
bool g_closing{false};
bool g_client_seen{false};
u32 g_draft_seq{0};
std::string g_received_text;
std::string g_draft_text;
Options g_current_options;

auto NewlinesToLf(std::string_view in) -> std::string {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '\r') {
            out.push_back('\n');
            if (i + 1 < in.size() && in[i + 1] == '\n') {
                i++;
            }
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

auto TextChanged(const std::string& a, const std::string& b) -> bool {
    return NewlinesToLf(a) != NewlinesToLf(b);
}

} // namespace

void SetRemoteInputActive(bool active) {
    std::lock_guard lock(g_mutex);
    g_active = active;
    if (!active) {
        g_received = false;
        g_has_draft = false;
        g_closing = false;
        g_client_seen = false;
        g_draft_seq = 0;
        g_received_text.clear();
        g_draft_text.clear();
    }
}

auto IsRemoteInputActive() -> bool {
    std::lock_guard lock(g_mutex);
    return g_active;
}

void SetReceivedText(const std::string& text) {
    std::lock_guard lock(g_mutex);
    g_received_text = text;
    g_received = true;
}

auto GetReceivedText() -> std::string {
    std::lock_guard lock(g_mutex);
    return g_received_text;
}

auto HasReceivedText() -> bool {
    std::lock_guard lock(g_mutex);
    return g_received;
}

void SetDraftText(const std::string& text) {
    std::lock_guard lock(g_mutex);
    g_draft_text = text;
    g_has_draft = true;
    g_draft_seq++;
}

auto DraftSeq() -> u32 {
    std::lock_guard lock(g_mutex);
    return g_draft_seq;
}

auto GetDraftText() -> std::string {
    std::lock_guard lock(g_mutex);
    return g_draft_text;
}

auto HasDraftText() -> bool {
    std::lock_guard lock(g_mutex);
    return g_has_draft;
}

void SetClosing(bool closing) {
    std::lock_guard lock(g_mutex);
    g_closing = closing;
}

auto IsClosing() -> bool {
    std::lock_guard lock(g_mutex);
    return g_closing;
}

void SetClientSeen() {
    std::lock_guard lock(g_mutex);
    g_client_seen = true;
}

auto ClientSeen() -> bool {
    std::lock_guard lock(g_mutex);
    return g_client_seen;
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
                : (GetCurrentOptions().editor
                    ? "Scan the QR or open the URL. Edit, then Save. Press B to cancel."_i18n
                    : "Scan the QR code with phone or open URL on PC to send text. Press B to cancel."_i18n));

            while (!pbox->ShouldExit()) {
                if (HasReceivedText()) {
                    R_SUCCEED();
                }
                svcSleepThread(150'000'000);
            }

            if (GetCurrentOptions().editor && ClientSeen()) {
                SetClosing(true);
                const auto seq0 = DraftSeq();
                for (int i = 0; i < 10; i++) {
                    if (HasReceivedText()) {
                        R_SUCCEED();
                    }
                    if (DraftSeq() != seq0) {
                        break;
                    }
                    svcSleepThread(100'000'000);
                }
            }

            R_THROW(Result_TransferCancelled);
        },
        [on_complete, was_running](Result rc) {
            const auto text = GetReceivedText();
            const auto draft = GetDraftText();
            const auto has_draft = HasDraftText();
            const auto opts = GetCurrentOptions();
            const auto original = opts.default_text;

            const auto stop_server = [was_running]() {
                SetRemoteInputActive(false);
                if (!was_running) {
                    WebShareStop();
                }
            };

            if (R_SUCCEEDED(rc)) {
                stop_server();
                if (!opts.editor && text.empty()) {
                    App::Notify("No input received"_i18n);
                    return;
                }
                App::Notify(opts.editor
                    ? "Saved from PC / phone"_i18n
                    : "Received input from phone/PC"_i18n);
                if (on_complete) {
                    on_complete(text);
                }
                return;
            }

            if (rc == Result_TransferCancelled && opts.editor && has_draft && TextChanged(original, draft)) {
                stop_server();
                App::Push<OptionBox>(
                    "Save changes?"_i18n,
                    "Don't save"_i18n, "Save"_i18n, 1,
                    [on_complete, draft](auto op_index) {
                        if (op_index && *op_index == 1) {
                            App::Notify("Saved from PC / phone"_i18n);
                            if (on_complete) {
                                on_complete(draft);
                            }
                        } else {
                            App::Notify("Input cancelled"_i18n);
                        }
                    }
                );
                return;
            }

            stop_server();
            if (rc == Result_TransferCancelled) {
                App::Notify("Input cancelled"_i18n);
            } else {
                App::Notify("No input received"_i18n);
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
