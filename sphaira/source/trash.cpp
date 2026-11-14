#include "trash.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sphaira::trash {
namespace {

constexpr long JOURNAL_MAX = 256 * 1024;

auto Exists(const std::string& p) -> bool {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

auto IsDir(const std::string& p) -> bool {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// makes every folder of `p` (a full path with the mount prefix), not the last part.
void MakeParents(const std::string& sd, const std::string& p) {
    for (auto pos = p.find('/', sd.size() + 1); pos != std::string::npos; pos = p.find('/', pos + 1)) {
        mkdir(p.substr(0, pos).c_str(), 0777);
    }
}

auto Children(const std::string& dir) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (auto d = opendir(dir.c_str())) {
        while (auto e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) {
                out.emplace_back(e->d_name);
            }
        }
        closedir(d);
    }
    return out;
}

auto RemoveAll(const std::string& p) -> bool {
    if (IsDir(p)) {
        for (const auto& c : Children(p)) {
            RemoveAll(p + "/" + c);
        }
        return rmdir(p.c_str()) == 0;
    }
    return std::remove(p.c_str()) == 0;
}

auto SizeOf(const std::string& p) -> std::uint64_t {
    struct stat st{};
    if (stat(p.c_str(), &st) != 0) {
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) {
        return st.st_size;
    }
    std::uint64_t n = 0;
    for (const auto& c : Children(p)) {
        n += SizeOf(p + "/" + c);
    }
    return n;
}

// "/dumps/x", "//dumps/x" and "dumps/x" are one place: "/dumps/x".
auto Clean(const std::string& path) -> std::string {
    const auto i = path.find_first_not_of('/');
    return "/" + (i == std::string::npos ? std::string{} : path.substr(i));
}

auto OneLine(std::string s) -> std::string {
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return s;
}

// moves the files under `from` (full path) back to `to` (full path), file by file.
void RestoreTree(const std::string& sd, const std::string& from, const std::string& to, std::size_t& moved, std::size_t& kept) {
    if (IsDir(from)) {
        for (const auto& c : Children(from)) {
            RestoreTree(sd, from + "/" + c, to + "/" + c, moved, kept);
        }
        rmdir(from.c_str()); // only when it is empty now
        return;
    }
    if (Exists(to)) {
        kept++;
        return;
    }
    MakeParents(sd, to);
    if (std::rename(from.c_str(), to.c_str()) == 0) {
        moved++;
    } else {
        kept++;
    }
}

auto ReadInfo(const std::string& path, Item& it) -> bool {
    auto f = std::fopen(path.c_str(), "r");
    if (!f) {
        return false;
    }
    char line[512];
    std::string lines[3];
    for (auto& l : lines) {
        if (!std::fgets(line, sizeof(line), f)) {
            break;
        }
        l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) {
            l.pop_back();
        }
    }
    std::fclose(f);
    it.kind = lines[0];
    it.label = lines[1];
    it.when = static_cast<std::time_t>(std::strtoll(lines[2].c_str(), nullptr, 10));
    return !it.kind.empty();
}

} // namespace

auto NewItem(const std::string& sd, const std::string& kind, const std::string& label, std::time_t now) -> std::string {
    char stamp[32];
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    MakeParents(sd, sd + DIR + "/x");
    for (int n = 1; n < 1000; n++) {
        const auto item = std::string{DIR} + "/" + stamp + "-" + std::to_string(n);
        if (mkdir((sd + item).c_str(), 0777) != 0) {
            if (errno == EEXIST) {
                continue;
            }
            return {};
        }
        auto f = std::fopen((sd + item + "/info.txt").c_str(), "w");
        if (!f) {
            rmdir((sd + item).c_str());
            return {};
        }
        std::fprintf(f, "%s\n%s\n%lld\n", OneLine(kind).c_str(), OneLine(label).c_str(), static_cast<long long>(now));
        std::fclose(f);
        return item;
    }
    return {};
}

auto InItem(const std::string& item, const std::string& path) -> std::string {
    return item + "/files" + Clean(path);
}

auto Move(const std::string& sd, const std::string& item, const std::string& path) -> bool {
    if (item.empty() || Clean(path) == "/") {
        return false;
    }
    const auto dst = sd + InItem(item, path);
    MakeParents(sd, dst);
    return std::rename((sd + Clean(path)).c_str(), dst.c_str()) == 0;
}

auto List(const std::string& sd) -> std::vector<Item> {
    std::vector<Item> out;
    for (const auto& id : Children(sd + DIR)) {
        Item it;
        it.id = id;
        const auto dir = sd + DIR + "/" + id;
        if (!IsDir(dir) || !ReadInfo(dir + "/info.txt", it)) {
            continue;
        }
        it.bytes = SizeOf(dir + "/files");
        out.push_back(std::move(it));
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.id > b.id; });
    return out;
}

auto Restore(const std::string& sd, const std::string& id, std::size_t* kept) -> std::size_t {
    std::size_t moved = 0, left = 0;
    const auto dir = sd + DIR + "/" + id;
    if (!id.empty() && id.find('/') == std::string::npos && IsDir(dir + "/files")) {
        for (const auto& c : Children(dir + "/files")) {
            RestoreTree(sd, dir + "/files/" + c, sd + "/" + c, moved, left);
        }
        if (left == 0) {
            RemoveAll(dir);
        }
    }
    if (kept) {
        *kept = left;
    }
    return moved;
}

auto Remove(const std::string& sd, const std::string& id) -> bool {
    if (id.empty() || id.find('/') != std::string::npos || id == "." || id == "..") {
        return false;
    }
    return RemoveAll(sd + DIR + "/" + id);
}

auto Purge(const std::string& sd, std::time_t now, const std::function<bool()>& low_space) -> std::size_t {
    auto items = List(sd);
    std::size_t removed = 0;
    // oldest first
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const bool old = now - it->when > static_cast<std::time_t>(MAX_AGE_DAYS) * 24 * 60 * 60;
        if (!old && !(low_space && low_space())) {
            continue;
        }
        if (Remove(sd, it->id)) {
            Journal(sd, old ? "trash: removed (30 days)" : "trash: removed (low space)", it->label, std::string{DIR} + "/" + it->id, now);
            removed++;
        }
    }
    return removed;
}

auto FormatLine(const std::string& what, const std::string& label, const std::string& copy, std::time_t now) -> std::string {
    char stamp[32];
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string{stamp} + " | " + OneLine(what) + " | " + OneLine(label) + " | " + (copy.empty() ? "no copy" : OneLine(copy)) + "\n";
}

void Journal(const std::string& sd, const std::string& what, const std::string& label, const std::string& copy, std::time_t now) {
    const auto path = sd + JOURNAL;
    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && st.st_size > JOURNAL_MAX) {
        const auto prev = sd + "/config/kefir/actions.prev.log";
        std::remove(prev.c_str());
        std::rename(path.c_str(), prev.c_str());
    }
    MakeParents(sd, path);
    if (auto f = std::fopen(path.c_str(), "a")) {
        const auto line = FormatLine(what, label, copy, now);
        std::fwrite(line.data(), 1, line.size(), f);
        std::fclose(f);
    }
}

} // namespace sphaira::trash
