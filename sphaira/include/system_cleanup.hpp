#pragma once

#include "ui/progress_box.hpp"
#include <switch.h>
#include <functional>
#include <vector>

namespace sphaira::cleanup {

// Tools → Clean system junk; one flag per step of the DBI cleanup screen that has a reliable API.
struct Options {
    bool old_updates{true};
    bool orphans_sd{true};
    bool orphans_nand{true};
    bool placeholders_sd{true};
    bool placeholders_nand{true};
    // off by default: a ticket cannot be brought back without a backup of it.
    bool unused_tickets{false};
    bool erpt_reports{true};
    bool contents_folders{true};
    // off by default: these are a person's saves. delete_user_saves deletes them (the caller copies each
    // into the trash first); without it the step is skipped.
    bool deleted_user_saves{false};
    std::function<Result(ui::ProgressBox*, const std::vector<FsSaveDataInfo>&)> delete_user_saves;
};

struct Report {
    s64 freed_sd{};
    s64 freed_nand{};
    u32 removed{};
    // steps that did nothing because a list they depend on could not be read
    u32 skipped{};
};

Result Run(ui::ProgressBox* pbox, const Options& options, Report& out);

} // namespace sphaira::cleanup
