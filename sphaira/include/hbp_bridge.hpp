#pragma once

// Kefir Hub's door into the hacBrewPackLZ engine (cmake/hbp): compresses installed games with the
// NCA compression layer, which FW 12.0.0+ reads by itself. The engine throws; these functions never do.
// Each returns an empty string on success, else what went wrong, in English, as the engine says it.

#include <switch.h>
#include <functional>
#include <string>

namespace sphaira::hbp {

struct Estimate {
    s64 now{};
    // about this much after compressing, give or take margin.
    s64 after{};
    s64 margin{};
    // all of its game data is already stored compressed.
    bool already_compressed{};
    // an update is installed: it becomes part of the game.
    bool has_update{};
};

// label, done, total. Returns false to stop.
using ProgressFn = std::function<bool(const char* label, s64 done, s64 total)>;

// set when the engine was stopped through ProgressFn.
constexpr const char* Stopped = "stopped";

// where the engine keeps its journal (a replace interrupted by a power cut) and temp files.
constexpr const char* DataFolder = "/config/kefir/hbp";

// finishes or undoes a replace a power cut interrupted. Cheap when there is nothing to do.
std::string RecoverIfNeeded();

std::string EstimateGame(u64 app_id, Estimate& out, const ProgressFn& progress);

// replaces the installed game (base, update merged into it, DLC) with its compressed copy.
// The original stays until the new copy is written and checked.
std::string CompressGame(u64 app_id, s64& before, s64& after, const ProgressFn& progress);

} // namespace sphaira::hbp
