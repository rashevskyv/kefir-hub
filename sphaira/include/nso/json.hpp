// Tiny JSON text writer that matches Python json.dumps(ensure_ascii=False, separators=(',',':')).
#pragma once
#include "common.hpp"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace nso::json {

// Length of the valid UTF-8 sequence at s[i], 0 when invalid.
inline size_t Utf8Len(std::string_view s, size_t i) {
    const u8 c = u8(s[i]);
    if (c < 0x80) return 1;
    size_t n; u32 min;
    if ((c & 0xE0) == 0xC0) { n = 2; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 3; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 4; min = 0x10000; }
    else return 0;
    if (i + n > s.size()) return 0;
    u32 cp = c & (0xFFu >> (n + 1));
    for (size_t k = 1; k < n; k++) {
        const u8 d = u8(s[i + k]);
        if ((d & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (d & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return n;
}

// "..." with escapes; invalid UTF-8 bytes become '?'. Non-ASCII stays raw UTF-8.
inline std::string Quote(std::string_view s) {
    constexpr char BS = 92;  // backslash
    std::string o = "\"";
    for (size_t i = 0; i < s.size();) {
        const u8 c = u8(s[i]);
        if (c >= 0x80) {
            const size_t n = Utf8Len(s, i);
            if (n == 0) { o += '?'; i++; }
            else { o.append(s.substr(i, n)); i += n; }
            continue;
        }
        i++;
        char e = 0;
        switch (c) {
            case '"': e = '"'; break;
            case BS: e = BS; break;
            case '\n': e = 'n'; break;
            case '\r': e = 'r'; break;
            case '\t': e = 't'; break;
            case '\b': e = 'b'; break;
            case '\f': e = 'f'; break;
            default: break;
        }
        if (e) {
            o += BS; o += e;
        } else if (c < 0x20) {
            static const char* hex = "0123456789abcdef";
            o += BS; o += "u00"; o += hex[c >> 4]; o += hex[c & 15];
        } else {
            o += char(c);
        }
    }
    return o + "\"";
}

// Ordered object. Raw() takes ready JSON text (numbers like 1.5 / 3.0 keep their form).
class Obj {
public:
    Obj& Raw(std::string k, std::string v) { f_.emplace_back(std::move(k), std::move(v)); return *this; }
    Obj& Str(std::string k, std::string_view v) { return Raw(std::move(k), Quote(v)); }
    Obj& Int(std::string k, long long v) { return Raw(std::move(k), std::to_string(v)); }
    Obj& Bool(std::string k, bool v) { return Raw(std::move(k), v ? "true" : "false"); }
    Obj& Ints(std::string k, std::initializer_list<int> v) {
        std::string s = "[";
        for (int x : v) { if (s.size() > 1) s += ','; s += std::to_string(x); }
        return Raw(std::move(k), s + "]");
    }
    Obj& Sort() {  // ASCII order, like Python sorted(dict)
        std::stable_sort(f_.begin(), f_.end(), [](auto& a, auto& b) { return a.first < b.first; });
        return *this;
    }
    std::string Text() const {
        std::string o = "{";
        for (size_t i = 0; i < f_.size(); i++) {
            if (i) o += ',';
            o += Quote(f_[i].first) + ":" + f_[i].second;
        }
        return o + "}";
    }
private:
    std::vector<std::pair<std::string, std::string>> f_;
};

}  // namespace nso::json
