#pragma once

// Which console user a backup belongs to, so a restore needs no question when the answer is clear.
// Pure logic, no libnx, so it has a host test.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {

struct ConsoleUser {
    std::uint64_t uid[2]{};
    std::string nickname{};
};

// The same user (uid) first: a backup made on this console. Then the one user with the backup's
// nickname: the same people on another console, or after the users were made again (a new uid).
// Nothing when no user, or more than one, fits.
inline auto MatchBackupUser(const std::uint64_t (&backup_uid)[2], const std::string& backup_nickname,
                            const std::vector<ConsoleUser>& users) -> std::optional<std::size_t> {
    if (backup_uid[0] || backup_uid[1]) {
        for (std::size_t i = 0; i < users.size(); i++) {
            if (users[i].uid[0] == backup_uid[0] && users[i].uid[1] == backup_uid[1]) {
                return i;
            }
        }
    }
    if (backup_nickname.empty()) {
        return std::nullopt;
    }
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < users.size(); i++) {
        if (users[i].nickname == backup_nickname) {
            if (found) {
                return std::nullopt; // two users with this name: ask
            }
            found = i;
        }
    }
    return found;
}

} // namespace sphaira::ui::menu::save
