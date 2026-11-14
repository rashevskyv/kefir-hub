#pragma once

// Trash and action journal: nothing the Hub deletes (saves, save backups, profile packs) is gone at once.
// An item is a folder /config/kefir/trash/<id>/ with info.txt (kind, label) and files/<original path>.
// Moving a file in is a rename on the same card (no space needed); restoring is a rename back.
// libnx-free: every path is the card path ("/config/...") and `sd` is the mount prefix
// ("sdmc:" on the console, a temp folder in the host test).

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace sphaira::trash {

inline constexpr const char* SD = "sdmc:"; // the mount prefix on the console
inline constexpr const char* DIR = "/config/kefir/trash";
inline constexpr const char* JOURNAL = "/config/kefir/actions.log";
inline constexpr int MAX_AGE_DAYS = 30;

struct Item {
    std::string id;    // folder name, "YYYYMMDD-HHMMSS-N": sorts by time
    std::string kind;  // "save", "backup", "profile", "user"
    std::string label; // game or profile name
    std::time_t when{};
    std::uint64_t bytes{};
};

// a new empty item; returns its card path ("/config/kefir/trash/<id>"), or "" when it cannot be made.
auto NewItem(const std::string& sd, const std::string& kind, const std::string& label, std::time_t now) -> std::string;
// where a file at card path `path` lives inside `item`.
auto InItem(const std::string& item, const std::string& path) -> std::string;
// moves a file or folder into the item (its parent folders are made). false: it stays where it was.
auto Move(const std::string& sd, const std::string& item, const std::string& path) -> bool;
// newest first.
auto List(const std::string& sd) -> std::vector<Item>;
// moves every file back. A file whose place is taken stays in the trash. Returns files moved back.
auto Restore(const std::string& sd, const std::string& id, std::size_t* kept = nullptr) -> std::size_t;
auto Remove(const std::string& sd, const std::string& id) -> bool;
// removes items older than MAX_AGE_DAYS, then the oldest while low_space() says so. Returns items removed.
auto Purge(const std::string& sd, std::time_t now, const std::function<bool()>& low_space) -> std::size_t;

// one line to the journal: "<date time> | <what> | <label> | <copy>". The file is never cleared on start;
// above 256 KB it moves to actions.prev.log.
void Journal(const std::string& sd, const std::string& what, const std::string& label, const std::string& copy, std::time_t now);
auto FormatLine(const std::string& what, const std::string& label, const std::string& copy, std::time_t now) -> std::string;

} // namespace sphaira::trash
