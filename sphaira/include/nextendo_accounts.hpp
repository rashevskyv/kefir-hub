#pragma once

// Which user profiles are linked to Nextendo: the Hub remembers the profiles it signed in (the console does not
// say which service an account link belongs to), and the account screen marks them. Pure rules; host test
// tests/test_nextendo_accounts.cpp.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::nextendo {

enum class AccountMark {
    NotLinked,     // no link at all: Sign in is possible
    Nextendo,      // linked, and the Hub signed it in to Nextendo
    OtherLink,     // linked, but not through the Hub's Nextendo sign-in (donor, Linkalho, Nintendo): unlink first
    NextendoUnread, // the Hub signed it in, the link status cannot be read now (Nextendo off)
    Unknown,       // link status cannot be read and the Hub has no record
};

// recorded = the profile id is in the list kept by the Hub
inline auto MarkFor(bool linked_known, bool linked, bool recorded) -> AccountMark {
    if (!linked_known) {
        return recorded ? AccountMark::NextendoUnread : AccountMark::Unknown;
    }
    if (!linked) {
        return AccountMark::NotLinked;
    }
    return recorded ? AccountMark::Nextendo : AccountMark::OtherLink;
}

// Sign in is offered only for a profile with no link; an unreadable status is let through (the console screen decides).
inline auto CanSignIn(AccountMark m) -> bool {
    return m == AccountMark::NotLinked || m == AccountMark::Unknown;
}

// The record is a comma separated list of 32-digit profile ids.
inline auto ParseRecord(std::string_view s) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&] {
        if (cur.size() == 32 && std::all_of(cur.begin(), cur.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); })) {
            if (std::find(out.begin(), out.end(), cur) == out.end()) {
                out.push_back(cur);
            }
        }
        cur.clear();
    };
    for (char c : s) {
        if (c == ',') {
            flush();
        } else {
            cur.push_back(c >= 'a' && c <= 'f' ? static_cast<char>(c - 32) : c);
        }
    }
    flush();
    return out;
}

inline auto FormatRecord(const std::vector<std::string>& ids) -> std::string {
    std::string out;
    for (const auto& id : ids) {
        out += out.empty() ? id : "," + id;
    }
    return out;
}

inline auto IsRecorded(const std::vector<std::string>& ids, const std::string& id) -> bool {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// A profile that is readable and not linked any more (unlinked in System Settings or in the Hub) leaves the record.
inline auto Forget(std::vector<std::string> ids, const std::string& id) -> std::vector<std::string> {
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    return ids;
}

inline auto Remember(std::vector<std::string> ids, const std::string& id) -> std::vector<std::string> {
    if (!IsRecorded(ids, id)) {
        ids.push_back(id);
    }
    return ids;
}

} // namespace sphaira::nextendo
