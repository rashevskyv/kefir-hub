#include "markdown_html.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::markdown {
namespace {

constexpr auto npos = std::string_view::npos;

auto IsAlnum(char c) -> bool {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

auto IsBlank(char c) -> bool {
    return c == ' ' || c == '\t';
}

auto Trim(std::string_view s) -> std::string_view {
    while (!s.empty() && IsBlank(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && IsBlank(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

auto AppendUtf8(std::string& out, std::uint32_t cp) -> void {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// name is the text between '&' and ';'. Returns false if it is not known.
auto DecodeEntity(std::string_view name, std::string& out) -> bool {
    if (name == "amp") {
        out.push_back('&');
    } else if (name == "lt") {
        out.push_back('<');
    } else if (name == "gt") {
        out.push_back('>');
    } else if (name == "quot") {
        out.push_back('"');
    } else if (name == "apos") {
        out.push_back('\'');
    } else if (name == "nbsp") {
        out.push_back(' ');
    } else if (name.size() >= 2 && name[0] == '#') {
        const bool hex = name[1] == 'x' || name[1] == 'X';
        std::uint32_t cp{};
        const auto digits = name.substr(hex ? 2 : 1);
        if (digits.empty() || digits.size() > 7) {
            return false;
        }
        for (const char c : digits) {
            std::uint32_t v{};
            if (c >= '0' && c <= '9') {
                v = c - '0';
            } else if (hex && c >= 'a' && c <= 'f') {
                v = c - 'a' + 10;
            } else if (hex && c >= 'A' && c <= 'F') {
                v = c - 'A' + 10;
            } else {
                return false;
            }
            cp = cp * (hex ? 16U : 10U) + v;
        }
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        AppendUtf8(out, cp);
    } else {
        return false;
    }
    return true;
}

// Writes what an HTML tag means in Markdown. body is the text between '<' and '>'.
auto HandleTag(std::string_view body, std::string& out) -> void {
    if (body.starts_with("http://") || body.starts_with("https://")) {
        // GFM autolink <https://...> keeps its URL text.
        out.append(body);
        return;
    }

    const bool closing = !body.empty() && body.front() == '/';
    if (closing) {
        body.remove_prefix(1);
    }

    std::string name;
    for (const char c : body) {
        if (!IsAlnum(c)) {
            break;
        }
        name.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }

    if (name.empty()) {
        return; // <!DOCTYPE>, <!-- -->, stray '<!'
    }
    if (name == "br") {
        out.push_back('\n');
    } else if (name == "b" || name == "strong") {
        out.append("**");
    } else if (name == "i" || name == "em") {
        out.push_back('*');
    } else if (name == "code") {
        out.push_back('`');
    } else if (name == "li" && !closing) {
        out.append("\n- ");
    } else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
        out.push_back('\n');
        if (!closing) {
            out.append(static_cast<std::size_t>(name[1] - '0'), '#');
            out.push_back(' ');
        }
    } else if (name == "a" || name == "span" || name == "sup" || name == "sub" || name == "kbd" ||
               name == "small" || name == "font" || name == "u" || name == "s" || name == "del" ||
               name == "ins" || name == "mark" || name == "img" || name == "picture" ||
               name == "source" || name == "abbr") {
        // inline: keep the text, drop the tag
    } else {
        // block tags (p, div, details, summary, table, ul, ...) break the line
        out.push_back('\n');
    }
}

// Length of the ``` or ~~~ run that t starts with, 0 if t is not a fence.
auto FenceRun(std::string_view t) -> std::size_t {
    if (t.empty() || (t.front() != '`' && t.front() != '~')) {
        return 0;
    }
    std::size_t n{};
    while (n < t.size() && t[n] == t.front()) {
        n++;
    }
    return n >= 3 ? n : 0;
}

// A closing fence: same char, at least as long as the opener, nothing else on the line.
auto IsFenceClose(std::string_view t, char ch, std::size_t len) -> bool {
    const auto n = FenceRun(t);
    return n >= len && t[0] == ch && Trim(t.substr(n)).empty();
}

} // namespace

// Removes '\r', HTML comments and tags, and decodes entities.
// Fenced code and backtick code spans are copied as they are.
auto Clean(std::string_view raw) -> std::string {
    std::string src;
    src.reserve(raw.size());
    for (const char c : raw) {
        if (c != '\r') {
            src.push_back(c);
        }
    }

    std::string out;
    out.reserve(src.size());

    bool in_fence{};
    char fence_ch{};
    std::size_t fence_len{};

    std::size_t i{};
    while (i < src.size()) {
        const char c = src[i];
        if (i == 0 || src[i - 1] == '\n') {
            const auto eol = src.find('\n', i);
            const auto t = Trim(std::string_view(src).substr(i, (eol == npos ? src.size() : eol) - i));
            if (in_fence) {
                in_fence = !IsFenceClose(t, fence_ch, fence_len);
            } else if (FenceRun(t) > 0) {
                in_fence = true;
                fence_ch = t.front();
                fence_len = FenceRun(t);
            }
        }
        if (in_fence) {
            out.push_back(c);
            i++;
            continue;
        }
        if (c == '`') {
            std::size_t run{};
            while (i + run < src.size() && src[i + run] == '`') {
                run++;
            }
            // a closing run of the same length on this line: copy the span as it is
            std::size_t close = npos;
            for (std::size_t j = i + run; j < src.size() && src[j] != '\n' && close == npos;) {
                std::size_t m{};
                while (j + m < src.size() && src[j + m] == '`') {
                    m++;
                }
                if (src[j] != '`') {
                    j++;
                } else {
                    if (m == run) {
                        close = j;
                    }
                    j += m;
                }
            }
            const auto end = (close == npos) ? i + run : close + run;
            out.append(src, i, end - i);
            i = end;
            continue;
        }
        if (c == '<') {
            if (src.substr(i, 4) == "<!--") {
                const auto end = src.find("-->", i + 4);
                i = (end == npos) ? src.size() : end + 3;
                continue;
            }
            const auto end = src.find('>', i + 1);
            if (end != npos && end - i <= 300 && i + 1 < src.size() &&
                (IsAlnum(src[i + 1]) || src[i + 1] == '/' || src[i + 1] == '!') &&
                src.substr(i, end - i).find('\n') == npos) {
                HandleTag(src.substr(i + 1, end - i - 1), out);
                i = end + 1;
                continue;
            }
        }
        if (c == '&') {
            const auto semi = src.find(';', i);
            if (semi != npos && semi - i <= 10 && DecodeEntity(src.substr(i + 1, semi - i - 1), out)) {
                i = semi + 1;
                continue;
            }
        }
        out.push_back(c);
        i++;
    }
    return out;
}


} // namespace sphaira::markdown
