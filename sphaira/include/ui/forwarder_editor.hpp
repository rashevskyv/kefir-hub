#pragma once

#include "owo.hpp"

#include <functional>
#include <string>
#include <vector>

namespace sphaira::ui::forwarder {

struct Values {
    std::string title;
    std::string author;
    std::string version;
    std::vector<u8> icon;
    ForwarderOptions options{};
    bool include_platform{false};
    bool icon_changed{false}; // the user picked a new icon
};

// return true to close the editor, false to keep it open (validation failed).
using CreateCallback = std::function<bool(const Values& values)>;

// a row that opens its own dialog and applies at once (not on submit).
struct Link {
    std::string label;
    std::function<std::string()> value;
    std::function<void()> open;
    std::string hint{};
};

struct Config {
    Values values;
    std::string icon_source{};
    std::string steam_query{};
    std::string screen_title{};
    std::string title_label{};
    std::string submit_label{};
    bool show_author{};
    bool show_version{};
    bool show_platform_title{};
    bool show_forwarder_options{true};
    std::vector<Link> links{}; // shown after the other rows, before the submit row
    CreateCallback on_create;
};

void Show(Config config);

} // namespace sphaira::ui::forwarder
