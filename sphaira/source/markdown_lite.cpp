#include "markdown_lite.hpp"
#include "markdown_html.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace sphaira::markdown {
namespace {

using Spans = std::vector<Span>;

constexpr auto npos = std::string_view::npos;

// Number of leading ASCII digits.
auto DigitCount(std::string_view s) -> std::size_t {
    std::size_t n{};
    while (n < s.size() && s[n] >= '0' && s[n] <= '9') {
        n++;
    }
    return n;
}

auto IsAlnum(char c) -> bool {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

auto IsBlank(char c) -> bool {
    return c == ' ' || c == '\t';
}

auto IsPunct(char c) -> bool {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
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

auto LeadingIndent(std::string_view s) -> int {
    int n{};
    for (const char c : s) {
        if (c == ' ') {
            n++;
        } else if (c == '\t') {
            n += 4;
        } else {
            break;
        }
    }
    return n;
}

void Emit(Spans& out, std::string_view text, bool bold, bool italic, bool code) {
    if (text.empty()) {
        return;
    }
    if (!out.empty()) {
        auto& last = out.back();
        if (last.bold == bold && last.italic == italic && last.code == code) {
            last.text.append(text);
            return;
        }
    }
    out.push_back(Span{std::string(text), bold, italic, code});
}

// Finds the ']' that closes the '[' at pos. Returns npos if none.
auto FindCloseBracket(std::string_view s, std::size_t pos) -> std::size_t {
    int depth{};
    for (std::size_t j = pos; j < s.size(); j++) {
        if (s[j] == '[') {
            depth++;
        } else if (s[j] == ']') {
            depth--;
            if (depth == 0) {
                return j;
            }
        }
    }
    return npos;
}

// Finds the ')' that closes the '(' at pos. Returns npos if none.
auto FindCloseParen(std::string_view s, std::size_t pos) -> std::size_t {
    int depth{};
    for (std::size_t j = pos; j < s.size(); j++) {
        if (s[j] == '(') {
            depth++;
        } else if (s[j] == ')') {
            depth--;
            if (depth == 0) {
                return j;
            }
        }
    }
    return npos;
}

// Finds a closing delimiter for an opener that ends before 'from'.
// delim is one or more chars. The closer must follow a non-space char.
// For '_' the closer must not be followed by a letter or digit.
auto FindCloser(std::string_view s, std::size_t from, std::string_view delim) -> std::size_t {
    const char d = delim.front();
    for (std::size_t j = from; j + delim.size() <= s.size(); j++) {
        if (s.substr(j, delim.size()) != delim || j == 0 || IsBlank(s[j - 1])) {
            continue;
        }
        const auto after = j + delim.size();
        if (d == '_' && after < s.size() && IsAlnum(s[after])) {
            continue;
        }
        return j;
    }
    return npos;
}

void ParseInline(std::string_view s, bool bold, bool italic, Spans& out) {
    std::size_t i{};
    while (i < s.size()) {
        const char c = s[i];

        if (c == '\\' && i + 1 < s.size() && IsPunct(s[i + 1])) {
            Emit(out, s.substr(i + 1, 1), bold, italic, false);
            i += 2;
            continue;
        }

        if (c == '`') {
            std::size_t run{};
            while (i + run < s.size() && s[i + run] == '`') {
                run++;
            }
            const auto delim = s.substr(i, run);
            std::size_t close = i + run;
            bool found{};
            while (close + run <= s.size()) {
                if (s.substr(close, run) == delim && (close + run == s.size() || s[close + run] != '`')) {
                    found = true;
                    break;
                }
                close++;
            }
            if (found) {
                Emit(out, s.substr(i + run, close - i - run), bold, italic, true);
                i = close + run;
            } else {
                Emit(out, delim, bold, italic, false);
                i += run;
            }
            continue;
        }

        // [text](url) becomes text. ![alt](url) is dropped.
        const bool image = c == '!' && i + 1 < s.size() && s[i + 1] == '[';
        if (c == '[' || image) {
            const auto open = image ? i + 1 : i;
            const auto close = FindCloseBracket(s, open);
            if (close != npos && close + 1 < s.size() && s[close + 1] == '(') {
                const auto end = FindCloseParen(s, close + 1);
                if (end != npos) {
                    if (!image) {
                        ParseInline(s.substr(open + 1, close - open - 1), bold, italic, out);
                    }
                    i = end + 1;
                    continue;
                }
            }
            Emit(out, s.substr(i, 1), bold, italic, false);
            i++;
            continue;
        }

        if (c == '*' || c == '_') {
            const bool word_ok = c != '_' || i == 0 || !IsAlnum(s[i - 1]);
            std::size_t run{};
            while (i + run < s.size() && s[i + run] == c) {
                run++;
            }

            // ***x*** and **x**
            if (run >= 2 && word_ok && i + run < s.size() && !IsBlank(s[i + run])) {
                if (run >= 3) {
                    const auto j = FindCloser(s, i + 3, s.substr(i, 3));
                    if (j != npos) {
                        ParseInline(s.substr(i + 3, j - i - 3), true, true, out);
                        i = j + 3;
                        continue;
                    }
                }
                const auto j = FindCloser(s, i + 2, s.substr(i, 2));
                if (j != npos) {
                    ParseInline(s.substr(i + 2, j - i - 2), true, italic, out);
                    i = j + 2;
                    continue;
                }
            }

            // *x* and _x_
            if (word_ok && i + 1 < s.size() && !IsBlank(s[i + 1]) && s[i + 1] != c) {
                const std::string_view delim1(s.data() + i, 1);
                const auto j = FindCloser(s, i + 1, delim1);
                if (j != npos) {
                    ParseInline(s.substr(i + 1, j - i - 1), bold, true, out);
                    i = j + 1;
                    continue;
                }
            }

            Emit(out, s.substr(i, run), bold, italic, false);
            i += run;
            continue;
        }

        Emit(out, s.substr(i, 1), bold, italic, false);
        i++;
    }
}

auto InlineOf(std::string_view s) -> Spans {
    Spans out;
    ParseInline(s, false, false, out);
    return out;
}

// A line of only '-', '*' or '_' (3 or more), with optional spaces.
auto IsRule(std::string_view t) -> bool {
    if (t.empty() || (t.front() != '-' && t.front() != '*' && t.front() != '_')) {
        return false;
    }
    int count{};
    for (const char c : t) {
        if (c == t.front()) {
            count++;
        } else if (!IsBlank(c)) {
            return false;
        }
    }
    return count >= 3;
}

// A table separator row, such as "|---|:--:|" or "---|---".
auto IsTableSeparator(std::string_view t) -> bool {
    bool dash{};
    bool bar{};
    for (const char c : t) {
        if (c == '-') {
            dash = true;
        } else if (c == '|') {
            bar = true;
        } else if (c != ':' && !IsBlank(c)) {
            return false;
        }
    }
    return dash && bar;
}

auto IsTableRow(std::string_view t) -> bool {
    if (t.find('|') == npos) {
        return false;
    }
    return t.front() == '|' || t.back() == '|' || t.find(" | ") != npos;
}

// Cells joined with "  |  ". A pipe after a backslash does not split a cell.
auto TableText(std::string_view t) -> std::string {
    if (t.front() == '|') {
        t.remove_prefix(1);
    }
    if (!t.empty() && t.back() == '|' && (t.size() < 2 || t[t.size() - 2] != '\\')) {
        t.remove_suffix(1);
    }

    std::string out;
    std::size_t start{};
    bool first{true};
    for (std::size_t i = 0; i <= t.size(); i++) {
        const bool split = i == t.size() || (t[i] == '|' && (i == 0 || t[i - 1] != '\\'));
        if (!split) {
            continue;
        }
        if (!first) {
            out.append("  |  ");
        }
        first = false;
        out.append(Trim(t.substr(start, i - start)));
        start = i + 1;
    }
    return out;
}

} // namespace

auto Parse(std::string_view src) -> std::vector<Block> {
    const auto text = Clean(src);

    std::vector<Block> out;
    bool gap{};
    bool in_fence{};
    char fence_char{};
    std::size_t fence_len{};

    const auto push = [&](Block&& block) {
        if (gap && !out.empty()) {
            out.push_back(Block{});
        }
        gap = false;
        out.push_back(std::move(block));
    };
    // a line that is empty after cleaning (image only, bare tags) is a gap
    const auto para = [&](Spans&& spans) {
        if (spans.empty()) {
            gap = true;
        } else {
            push(Block{Kind::Paragraph, 0, 0, std::move(spans)});
        }
    };

    std::string_view rest(text);
    while (true) {
        const auto nl = rest.find('\n');
        const auto line = (nl == npos) ? rest : rest.substr(0, nl);

        const auto t = Trim(line);
        if (in_fence) {
            std::size_t run{};
            while (run < t.size() && t[run] == fence_char) {
                run++;
            }
            if (run >= fence_len && Trim(t.substr(run)).empty()) {
                in_fence = false;
            } else {
                push(Block{Kind::Code, 0, 0, {Span{std::string(line)}}});
            }
        } else if (t.empty()) {
            gap = true;
        } else if (t.starts_with("```") || t.starts_with("~~~")) {
            in_fence = true;
            fence_char = t.front();
            fence_len = 0;
            while (fence_len < t.size() && t[fence_len] == fence_char) {
                fence_len++;
            }
        } else if (t.front() == '#') {
            std::size_t level{};
            while (level < t.size() && t[level] == '#') {
                level++;
            }
            if (level <= 6 && (level == t.size() || IsBlank(t[level]))) {
                auto content = Trim(t.substr(level));
                // a closing run of '#' ("## Title ##") is not text
                std::size_t end = content.size();
                while (end > 0 && content[end - 1] == '#') {
                    end--;
                }
                if (end == 0 || IsBlank(content[end - 1])) {
                    content = Trim(content.substr(0, end));
                }
                push(Block{Kind::Heading, static_cast<int>(level < 3 ? level : 3), 0, InlineOf(content)});
            } else {
                para(InlineOf(t));
            }
        } else if (IsRule(t)) {
            push(Block{Kind::Rule, 0, 0, {}});
        } else if (t.front() == '>') {
            auto content = t;
            while (!content.empty() && content.front() == '>') {
                content.remove_prefix(1);
                content = Trim(content);
            }
            push(Block{Kind::Quote, 0, 0, InlineOf(content)});
        } else if ((t.front() == '-' || t.front() == '*' || t.front() == '+') && (t.size() == 1 || IsBlank(t[1]))) {
            const int level = std::min(LeadingIndent(line) / 2, 4);
            push(Block{Kind::Bullet, level, 0, InlineOf(Trim(t.substr(1)))});
        } else if (const auto digits = DigitCount(t); digits > 0 && digits <= 9 && digits < t.size() &&
                   (t[digits] == '.' || t[digits] == ')') && (digits + 1 == t.size() || IsBlank(t[digits + 1]))) {
            const int number = std::stoi(std::string(t.substr(0, digits)));
            const int level = std::min(LeadingIndent(line) / 2, 4);
            push(Block{Kind::Numbered, level, number, InlineOf(Trim(t.substr(digits + 1)))});
        } else if (IsTableSeparator(t)) {
            // separator row ("|---|---|"): not shown
        } else if (IsTableRow(t)) {
            para(InlineOf(TableText(t)));
        } else {
            para(InlineOf(t));
        }

        // the text after the last '\n' is a line too, but a trailing '\n' ends the text
        rest = (nl == npos) ? std::string_view{} : rest.substr(nl + 1);
        if (rest.empty()) {
            break;
        }
    }

    return out;
}

} // namespace sphaira::markdown
