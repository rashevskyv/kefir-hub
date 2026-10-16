#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sphaira::system_info {

// Tools → System information: parameter → value rows, in groups the screen
// opens one at a time (Console, Atmosphère, Storage, Power, Battery, Hardware,
// Play activity). Labels are already translated.
struct Row {
    std::string label;
    std::string value;
    bool warn{};       // value drawn orange: something to act on (Tools → Battery)
    std::string help{}; // shown with A on the row; empty = no help
    std::function<void()> action{}; // A runs this instead of the help (calibration wizard steps)
    bool done{};        // value drawn green: a finished step
};

struct Group {
    std::string title;
    std::vector<Row> rows;
};

// Reads every group. Talks to a dozen services and walks the installed games,
// so it is called from a ProgressBox thread, not from the UI thread.
auto Collect() -> std::vector<Group>;

} // namespace sphaira::system_info
