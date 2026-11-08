// lclassics.titlesdb edited at TEXT level: every byte outside the touched member stays as it was.
// Handles both forms: {"titles":{"<code>":{...},...}} and the NES v0 list {"titles":[{...},...]},
// compact or pretty printed.
#pragma once
#include "common.hpp"
#include "json.hpp"

#include <optional>
#include <string_view>

namespace nso::titlesdb {

struct Entry {
    std::string code;
    size_t begin = 0, end = 0;  // dict: key start .. value end. list: element span.
};

struct Db {
    bool ok = false;
    bool is_list = false;
    size_t open = 0;  // index of '{' or '[' of "titles"
    size_t close = 0; // index of the closing bracket
    std::vector<Entry> entries;
};

namespace detail {
constexpr size_t kNpos = std::string_view::npos;

inline size_t Ws(std::string_view t, size_t i) {
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) i++;
    return i;
}
// index after the closing quote, kNpos on error
inline size_t Str(std::string_view t, size_t i) {
    if (i >= t.size() || t[i] != '"') return kNpos;
    for (i++; i < t.size(); i++) {
        if (t[i] == 92) i++;
        else if (t[i] == '"') return i + 1;
    }
    return kNpos;
}
// index after the value starting at i
inline size_t Value(std::string_view t, size_t i) {
    if (i >= t.size()) return kNpos;
    if (t[i] == '"') return Str(t, i);
    if (t[i] == '{' || t[i] == '[') {
        int depth = 0;
        for (; i < t.size(); i++) {
            const char c = t[i];
            if (c == '"') { i = Str(t, i); if (i == kNpos) return kNpos; i--; }
            else if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') { if (--depth == 0) return i + 1; }
        }
        return kNpos;
    }
    const size_t s = i;
    while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ']' && t[i] != ' ' && t[i] != '\t' &&
           t[i] != '\r' && t[i] != '\n') i++;
    return i == s ? kNpos : i;
}
// value of the top-level "code" string of the object t[b,e)
inline std::string CodeField(std::string_view t, size_t b, size_t e) {
    size_t i = b;
    if (i >= e || t[i] != '{') return {};
    for (i++; i < e;) {
        i = Ws(t, i);
        if (i >= e || t[i] == '}') break;
        if (t[i] == ',') { i++; continue; }
        const size_t ke = Str(t, i);
        if (ke == kNpos) return {};
        const std::string_view key = t.substr(i + 1, ke - i - 2);
        i = Ws(t, ke);
        if (i >= e || t[i] != ':') return {};
        i = Ws(t, i + 1);
        const size_t ve = Value(t, i);
        if (ve == kNpos) return {};
        if (key == "code" && t[i] == '"') return std::string(t.substr(i + 1, ve - i - 2));
        i = ve;
    }
    return {};
}
}  // namespace detail

inline Db Scan(std::string_view t) {
    using namespace detail;
    Db db;
    size_t i = 0;
    if (t.substr(0, 3) == "\xEF\xBB\xBF") i = 3;
    i = Ws(t, i);
    if (i >= t.size() || t[i] != '{') return db;
    for (i++;;) {
        i = Ws(t, i);
        if (i >= t.size()) return db;
        if (t[i] == '}') return db;  // no "titles"
        if (t[i] == ',') { i++; continue; }
        const size_t ke = Str(t, i);
        if (ke == kNpos) return db;
        const std::string_view key = t.substr(i + 1, ke - i - 2);
        i = Ws(t, ke);
        if (i >= t.size() || t[i] != ':') return db;
        i = Ws(t, i + 1);
        if (key != "titles") { i = Value(t, i); if (i == kNpos) return db; continue; }
        if (i >= t.size() || (t[i] != '{' && t[i] != '[')) return db;
        db.is_list = t[i] == '[';
        db.open = i;
        const char endc = db.is_list ? ']' : '}';
        for (i++;;) {
            i = Ws(t, i);
            if (i >= t.size()) return db;
            if (t[i] == endc) { db.close = i; db.ok = true; return db; }
            if (t[i] == ',') { i++; continue; }
            Entry e;
            e.begin = i;
            if (db.is_list) {
                e.end = Value(t, i);
                if (e.end == kNpos) return db;
                e.code = CodeField(t, e.begin, e.end);
            } else {
                const size_t ce = Str(t, i);
                if (ce == kNpos) return db;
                e.code = std::string(t.substr(i + 1, ce - i - 2));
                i = Ws(t, ce);
                if (i >= t.size() || t[i] != ':') return db;
                i = Ws(t, i + 1);
                e.end = Value(t, i);
                if (e.end == kNpos) return db;
            }
            i = e.end;
            db.entries.push_back(std::move(e));
        }
    }
}

inline std::optional<std::vector<std::string>> ListCodes(std::string_view t) {
    const Db db = Scan(t);
    if (!db.ok) return std::nullopt;
    std::vector<std::string> v;
    for (auto& e : db.entries) v.push_back(e.code);
    return v;
}

inline bool Has(const Db& db, std::string_view code) {
    for (auto& e : db.entries) if (e.code == code) return true;
    return false;
}

// Removes the entry; false when the db is not parseable or the code is absent.
inline bool Remove(std::string& t, std::string_view code) {
    const Db db = Scan(t);
    if (!db.ok) return false;
    for (size_t i = 0; i < db.entries.size(); i++) {
        if (db.entries[i].code != code) continue;
        const auto& e = db.entries[i];
        if (i > 0) t.erase(db.entries[i - 1].end, e.end - db.entries[i - 1].end);  // ",<ws><entry>"
        else if (db.entries.size() > 1) t.erase(e.begin, db.entries[1].begin - e.begin);
        else t.erase(e.begin, e.end - e.begin);
        return true;
    }
    return false;
}

// Adds (or replaces) an entry. `entry_json` is the entry object text, e.g. from json::Obj::Text().
inline bool Add(std::string& t, std::string_view code, std::string_view entry_json) {
    Remove(t, code);
    const Db db = Scan(t);
    if (!db.ok) return false;
    std::string piece = db.entries.empty() ? "" : ",";
    if (!db.is_list) piece += json::Quote(code) + ":";
    piece += entry_json;
    t.insert(db.entries.empty() ? db.open + 1 : db.entries.back().end, piece);
    return true;
}

// The entry object text (for tests / duplicate detection), empty when absent.
inline std::string_view EntryText(std::string_view t, const Db& db, std::string_view code) {
    for (auto& e : db.entries) {
        if (e.code != code) continue;
        if (db.is_list) return t.substr(e.begin, e.end - e.begin);
        size_t i = detail::Ws(t, detail::Str(t, e.begin));
        i = detail::Ws(t, i + 1);
        return t.substr(i, e.end - i);
    }
    return {};
}

}  // namespace nso::titlesdb
