#pragma once

#include <string>
#include <functional>

namespace sphaira::ui::remote_input {

struct Options {
    std::string title{"Remote Input"};
    std::string guide{"Enter text or paste a URL"};
    std::string default_text{""};
    std::string placeholder{"https://... or enter text"};
    bool multiline{false};
    bool editor{false};
    int min_length{1};
    int max_length{4096};
};

using OnCompleteCallback = std::function<void(const std::string& text)>;

// Shows a prompt allowing the user to either type on console (swkbd) or send from phone/PC (QR/Web).
void PromptTextInput(const Options& options, OnCompleteCallback on_complete);

// Directly opens the QR code / web server transfer screen.
void RequestRemoteText(const Options& options, OnCompleteCallback on_complete);

// Internal web handlers
void SetRemoteInputActive(bool active);
auto IsRemoteInputActive() -> bool;
void SetReceivedText(const std::string& text);
auto GetReceivedText() -> std::string;
auto HasReceivedText() -> bool;
void SetDraftText(const std::string& text);
auto GetDraftText() -> std::string;
auto HasDraftText() -> bool;
void SetClosing(bool closing);
auto IsClosing() -> bool;
void SetClientSeen();
auto ClientSeen() -> bool;
auto GetCurrentOptions() -> Options;

} // namespace sphaira::ui::remote_input
