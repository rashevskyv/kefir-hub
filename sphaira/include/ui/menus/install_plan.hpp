#pragma once

#include <cstdint>
#include <string_view>

namespace sphaira::ui::menu::dbi {

constexpr int64_t AbsDiff(int64_t a, int64_t b) {
    return a >= b ? a - b : b - a;
}

// Which target one package should go to, given the budgets left after the
// packages before it in the queue were placed. Pure, so the packing rules can
// be checked without a console (see tests/test_install_plan.cpp).
//
// loc matches the "Install location" setting:
//   0 SD only, 1 system only, 2 system first, 3 SD first, 4 automatic.
// Returns true for microSD.
// Callers pass usable free (free - reserve); this helper does not bake reserve in.
constexpr bool PlanPickSd(long loc, int64_t size, int64_t free_sd, int64_t free_nand) {
    switch (loc) {
        case 0: return true;
        case 1: return false;
        case 2: return free_nand < size;   // fill system, spill to SD
        case 3: return free_sd >= size;    // fill SD, spill to system
        default: {                         // automatic: balance remaining usable
            const bool fits_sd = free_sd >= size;
            const bool fits_nand = free_nand >= size;
            if (fits_sd && fits_nand) {
                const auto gap_sd = AbsDiff(free_sd - size, free_nand);
                const auto gap_nand = AbsDiff(free_nand - size, free_sd);
                return gap_sd <= gap_nand; // tie → SD
            }
            if (fits_sd) return true;
            if (fits_nand) return false;
            return free_sd >= free_nand;   // neither fits: roomier (tie → SD)
        }
    }
}

// Budgets never go negative: once a target is full everything else lands on the
// other one, and the header bar turns red for whatever no longer fits.
constexpr void PlanTake(int64_t& budget, int64_t size) {
    budget = budget > size ? budget - size : 0;
}

// Evaluates whether a new candidate package fits into available storage given location policy,
// and if so, whether it should go to SD.
struct PlanCandidateResult {
    bool fits;
    bool is_sd;
};

constexpr PlanCandidateResult PlanEvaluateCandidate(long loc, int64_t size, int64_t free_sd, int64_t free_nand) {
    const bool pick_sd = PlanPickSd(loc, size, free_sd, free_nand);
    if (pick_sd) {
        if (free_sd >= size) return {true, true};
        if (loc != 0 && free_nand >= size) return {true, false}; // spill to NAND if not SD-only
        return {false, true};
    } else {
        if (free_nand >= size) return {true, false};
        if (loc != 1 && free_sd >= size) return {true, true}; // spill to SD if not NAND-only
        return {false, false};
    }
}

// Title id and version from a scene-style file name, "Game [0100ABF008968000][v458752].nsz".
// The queue analysis reads only the container's file table, never the cnmt, so this
// is what "already installed" checks against. id 0 when the name has no [16 hex].
// ponytail: names without the tag (multi-title repacks) count as not installed; read the
// cnmt in AnalyzeSource if that ever matters.
struct NameTitle {
    uint64_t id{};
    uint32_t version{};
    bool has_version{};
};

constexpr NameTitle ParseNameTitle(std::string_view name) {
    NameTitle out{};
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] != '[') continue;
        const auto close = name.find(']', i + 1);
        if (close == std::string_view::npos) break;
        const auto tag = name.substr(i + 1, close - i - 1);
        if (!out.id && tag.size() == 16) {
            uint64_t v = 0;
            bool ok = true;
            for (char c : tag) {
                const int d = hex(c);
                if (d < 0) { ok = false; break; }
                v = (v << 4) | static_cast<uint64_t>(d);
            }
            if (ok) out.id = v;
        } else if (!out.has_version && tag.size() > 1 && tag.size() <= 11 && tag[0] == 'v') {
            uint64_t v = 0;
            bool ok = true;
            for (char c : tag.substr(1)) {
                if (c < '0' || c > '9') { ok = false; break; }
                v = v * 10 + static_cast<uint64_t>(c - '0');
            }
            if (ok && v <= UINT32_MAX) {
                out.version = static_cast<uint32_t>(v);
                out.has_version = true;
            }
        }
    }
    return out;
}

// "Already installed" mode the PC sends in the SPHQ revision line ("::SPHQ_REV::|rev|<field>|0"):
// 0 = the PC leaves it to the console, 1..3 = Reinstall / Skip / Prompt. Older backends
// send 0 there. Returns -1 for "not set by the PC".
constexpr long PcSkipMode(long field) {
    return field >= 1 && field <= 3 ? field - 1 : -1;
}

} // namespace sphaira::ui::menu::dbi
