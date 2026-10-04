#pragma once

#include "ui/progress_box.hpp"
#include <switch.h>

namespace sphaira::cleanup {

// Tools → Clean system junk; one flag per step of the DBI cleanup screen that has a reliable API.
struct Options {
    bool old_updates{true};
    bool orphans_sd{true};
    bool orphans_nand{true};
    bool placeholders_sd{true};
    bool placeholders_nand{true};
    bool unused_tickets{true};
    bool erpt_reports{true};
    bool contents_folders{true};
    // off by default: these are a person's saves, gone for good once deleted.
    bool deleted_user_saves{false};
};

struct Report {
    s64 freed_sd{};
    s64 freed_nand{};
    u32 removed{};
};

Result Run(ui::ProgressBox* pbox, const Options& options, Report& out);

} // namespace sphaira::cleanup
