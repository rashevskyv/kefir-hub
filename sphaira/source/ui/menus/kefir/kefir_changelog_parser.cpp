#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/nvg_util.hpp"
#include "utils/utils.hpp"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::kefir {
namespace detail {

void AddChangelogSegment(std::vector<ChangelogSegment>& out, std::string text, bool bold, bool underline, ChangelogTextColour colour) {
    if (text.empty()) {
        return;
    }

    if (!out.empty()) {
        auto& last = out.back();
        if (last.bold == bold && last.underline == underline && last.colour == colour) {
            last.text += text;
            return;
        }
    }

    out.push_back({
        .text = std::move(text),
        .bold = bold,
        .underline = underline,
        .colour = colour,
    });
}

auto IsUrlStart(const std::string& text, size_t pos) -> bool {
    return text.compare(pos, 7, "http://") == 0 || text.compare(pos, 8, "https://") == 0;
}

void ParseChangelogInline(const std::string& text, std::vector<ChangelogSegment>& out, bool bold,
    bool underline, ChangelogTextColour colour) {
    std::string current;

    const auto flush = [&]() {
        AddChangelogSegment(out, std::move(current), bold, underline, colour);
        current.clear();
    };

    for (size_t i = 0; i < text.size();) {
        if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
            flush();
            bold = !bold;
            i += 2;
            continue;
        }

        if (i + 1 < text.size() && text[i] == '_' && text[i + 1] == '_') {
            flush();
            underline = !underline;
            i += 2;
            continue;
        }

        if (text.compare(i, 3, "<u>") == 0) {
            flush();
            underline = true;
            i += 3;
            continue;
        }

        if (text.compare(i, 4, "</u>") == 0) {
            flush();
            underline = false;
            i += 4;
            continue;
        }

        if (text.compare(i, 7, "{{red}}") == 0) {
            const auto end = text.find("{{/red}}", i + 7);
            if (end != std::string::npos) {
                flush();
                ParseChangelogInline(text.substr(i + 7, end - i - 7), out, bold, underline, ChangelogTextColour::Red);
                i = end + 8;
                continue;
            }
        }

        if (text.compare(i, 8, "{{blue}}") == 0) {
            const auto end = text.find("{{/blue}}", i + 8);
            if (end != std::string::npos) {
                flush();
                ParseChangelogInline(text.substr(i + 8, end - i - 8), out, bold, true, ChangelogTextColour::Blue);
                i = end + 9;
                continue;
            }
        }

        if (text[i] == '`') {
            const auto end = text.find('`', i + 1);
            if (end != std::string::npos) {
                flush();
                AddChangelogSegment(out, "`", bold, underline, colour);
                ParseChangelogInline(text.substr(i + 1, end - i - 1), out, false, underline, ChangelogTextColour::Gray);
                AddChangelogSegment(out, "`", bold, underline, colour);
                i = end + 1;
                continue;
            }
        }

        if (text[i] == '[') {
            const auto close_bracket = text.find(']', i + 1);
            if (close_bracket != std::string::npos) {
                if (close_bracket + 1 < text.size() && text[close_bracket + 1] == '(') {
                    const auto close_paren = text.find(')', close_bracket + 2);
                    if (close_paren != std::string::npos) {
                        flush();
                        ParseChangelogInline(text.substr(i + 1, close_bracket - i - 1), out, bold, true, ChangelogTextColour::Blue);
                        i = close_paren + 1;
                        continue;
                    }
                }

                flush();
                AddChangelogSegment(out, "[", bold, underline, colour);
                ParseChangelogInline(text.substr(i + 1, close_bracket - i - 1), out, bold, underline, ChangelogTextColour::Gray);
                AddChangelogSegment(out, "]", bold, underline, colour);
                i = close_bracket + 1;
                continue;
            }
        }

        if (text[i] == '\'' && (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t')) {
            const auto end = text.find('\'', i + 1);
            if (end != std::string::npos) {
                flush();
                AddChangelogSegment(out, "'", bold, underline, colour);
                ParseChangelogInline(text.substr(i + 1, end - i - 1), out, bold, underline, ChangelogTextColour::Gray);
                AddChangelogSegment(out, "'", bold, underline, colour);
                i = end + 1;
                continue;
            }
        }

        if (IsUrlStart(text, i)) {
            size_t end = i;
            while (end < text.size() && text[end] != ' ' && text[end] != '\t') {
                end++;
            }

            flush();
            AddChangelogSegment(out, text.substr(i, end - i), bold, true, ChangelogTextColour::Blue);
            i = end;
            continue;
        }

        current += text[i++];
    }

    flush();
}

auto ChangelogSegmentColour(const ChangelogSegment& segment, Theme* theme) -> NVGcolor {
    switch (segment.colour) {
        case ChangelogTextColour::Gray:
            return nvgRGBA(128, 128, 128, 255);
        case ChangelogTextColour::Red:
            return nvgRGBA(255, 80, 80, 255);
        case ChangelogTextColour::Blue:
            return nvgRGBA(100, 150, 255, 255);
        case ChangelogTextColour::Normal:
            return theme->GetColour(ThemeEntryID_TEXT);
    }

    return theme->GetColour(ThemeEntryID_TEXT);
}

auto ChangelogSpaceWidth(NVGcontext* vg, float font_size) -> float {
    float ab[4]{};
    float a_b[4]{};
    nvgFontSize(vg, font_size);
    nvgTextBounds(vg, 0.f, 0.f, "ab", nullptr, ab);
    nvgTextBounds(vg, 0.f, 0.f, "a b", nullptr, a_b);

    const auto width = (a_b[2] - a_b[0]) - (ab[2] - ab[0]);
    return width < 1.f ? font_size * 0.28f : width;
}

auto MeasureWord(NVGcontext* vg, const std::string& word, float font_size) -> float {
    float bounds[4]{};
    nvgFontSize(vg, font_size);
    nvgTextBounds(vg, 0.f, 0.f, word.c_str(), nullptr, bounds);
    return bounds[2] - bounds[0];
}

auto RenderChangelogLine(NVGcontext* vg, Theme* theme, const std::string& line, float x, float y, float width,
    float font_size, float line_height, bool render) -> float {
    std::vector<ChangelogSegment> segments;
    ParseChangelogInline(line, segments);

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    const auto space_width = ChangelogSpaceWidth(vg, font_size);
    auto current_x = x;
    auto current_y = y;
    bool line_start = true;
    bool needs_space = false;
    std::string word;
    ChangelogSegment word_segment{};

    const auto flush_word = [&]() {
        if (word.empty()) {
            return;
        }

        const auto word_width = MeasureWord(vg, word, font_size);
        const auto bold_extra = word_segment.bold ? 1.f : 0.f;
        auto leading_space = needs_space ? space_width : 0.f;

        if (!line_start && current_x + leading_space + word_width + bold_extra > x + width) {
            current_x = x;
            current_y += line_height;
            line_start = true;
            leading_space = 0.f;
        }

        current_x += leading_space;

        if (render) {
            const auto colour = ChangelogSegmentColour(word_segment, theme);
            nvgFillColor(vg, colour);
            nvgFontSize(vg, font_size);
            nvgText(vg, current_x, current_y, word.c_str(), nullptr);
            if (word_segment.bold) {
                nvgText(vg, current_x + 1.f, current_y, word.c_str(), nullptr);
            }
            if (word_segment.underline) {
                const auto underline_y = current_y + font_size + 2.f;
                nvgBeginPath(vg);
                nvgMoveTo(vg, current_x, underline_y);
                nvgLineTo(vg, current_x + word_width, underline_y);
                nvgStrokeWidth(vg, std::max(1.f, font_size / 18.f));
                nvgStrokeColor(vg, colour);
                nvgStroke(vg);
            }
        }

        current_x += word_width + bold_extra;
        line_start = false;
        needs_space = false;
        word.clear();
    };

    for (const auto& segment : segments) {
        word_segment = segment;
        for (const auto c : segment.text) {
            if (c == ' ') {
                flush_word();
                if (!line_start) {
                    needs_space = true;
                }
            } else {
                word += c;
            }
        }
        flush_word();
    }

    return (current_y - y) + line_height;
}

auto MarkdownAtxLevel(const std::string& line) -> int {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    size_t i = start;
    while (i < line.size() && line[i] == '#') {
        i++;
    }
    const auto level = static_cast<int>(i - start);
    if (level < 1 || level > 6) {
        return 0;
    }
    if (i >= line.size() || (line[i] != ' ' && line[i] != '\t')) {
        return 0;
    }
    return level;
}

auto StripMarkdownAtx(const std::string& line, int level) -> std::string {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    start += static_cast<size_t>(level);
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    size_t end = line.size();
    while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
        end--;
    }
    while (end > start && line[end - 1] == '#') {
        end--;
    }
    while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
        end--;
    }
    return line.substr(start, end - start);
}

auto RenderChangelogText(NVGcontext* vg, Theme* theme, const std::string& text, const Vec4& area, float scroll, bool render,
    float regular_font_size, float line_height_scale, float header_font_size, float preamble_font_size) -> float {
    std::istringstream stream(text);
    std::string line;
    auto y = area.y - scroll;
    auto total_height = 0.f;
    bool reached_version_entries = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto md_level = MarkdownAtxLevel(line);
        const auto is_md_header = md_level > 0;
        const auto is_ver_header = IsVersionHeaderLine(line);
        const auto is_header = is_ver_header || is_md_header;
        const auto is_blank = ::sphaira::utils::TrimAsciiWhitespace(line).empty();
        const auto is_preamble = !reached_version_entries && !is_header && !is_blank;
        float font_size = regular_font_size;
        if (is_md_header) {
            font_size = md_level <= 2 ? header_font_size : std::max(regular_font_size + 1.f, header_font_size - 2.f);
        } else if (is_ver_header) {
            font_size = header_font_size;
        } else if (is_preamble) {
            font_size = preamble_font_size;
        }
        const auto line_height = font_size * (is_header ? 1.35f : line_height_scale);

        if (is_ver_header) {
            reached_version_entries = true;
        }

        const auto display = is_md_header ? StripMarkdownAtx(line, md_level) : line;
        auto height = is_blank ? line_height * 0.55f :
            RenderChangelogLine(vg, theme, display, area.x, y, area.w, font_size, line_height, render);
        if (is_header && !is_blank) {
            height += 6.f;
        }
        y += height;
        total_height += height;
    }

    return total_height;
}

auto BuildKefirChangelogText(const std::string& raw, const std::string& current_version, const std::string& target_version, bool& should_skip) -> std::string {
    const auto target_ver = ParseKefirChangelogVersion(target_version);
    if (!target_ver) {
        should_skip = false;
        return "Could not determine target Kefir version.";
    }

    if (raw.empty()) {
        should_skip = true;
        return "Failed to download changelog.";
    }

    const auto section = ExtractChangelogSection(raw, IsUkrainianLanguage());
    std::string preamble;
    std::vector<std::pair<int, std::string>> version_blocks;

    std::istringstream stream(section);
    std::string line;
    bool in_preamble = true;
    int current_block = -1;
    std::string block_content;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto non_space = line.find_first_not_of(" \t");
        if (non_space == std::string::npos) {
            continue;
        }

        auto trimmed = line.substr(non_space);
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
            trimmed.pop_back();
        }

        if (IsFullBoldLine(trimmed)) {
            const auto content = trimmed.substr(2, trimmed.size() - 4);
            const bool is_version = !content.empty() && std::all_of(content.begin(), content.end(), [](unsigned char c) {
                return std::isdigit(c);
            });

            if (is_version) {
                if (current_block != -1 && !block_content.empty()) {
                    version_blocks.push_back({current_block, block_content});
                }

                in_preamble = false;
                current_block = std::strtol(content.c_str(), nullptr, 10);
                block_content.clear();
                continue;
            }
        }

        if (in_preamble) {
            preamble += line + "\n";
        } else if (current_block != -1) {
            block_content += line + "\n";
        }
    }

    if (current_block != -1 && !block_content.empty()) {
        version_blocks.push_back({current_block, block_content});
    }

    const auto current_ver = ParseKefirChangelogVersion(current_version);
    const auto latest_available_ver = version_blocks.empty() ? 0 : version_blocks.front().first;

    should_skip = (latest_available_ver != target_ver);

    int start_ver = 0;
    int end_ver = target_ver;

    if (current_ver == target_ver) {
        start_ver = target_ver;
        end_ver = target_ver;
    } else {
        start_ver = current_ver + 1;
    }

    std::string version_content;
    bool found_any = false;
    for (const auto& [version, content] : version_blocks) {
        if (version < start_ver || version > end_ver) {
            continue;
        }

        const auto display_content = BuildChangelogDisplayText(content, true);
        if (display_content.empty()) {
            continue;
        }

        found_any = true;
        version_content += "**" + std::to_string(version) + "**\n";
        version_content += display_content + "\n\n";
    }

    std::string result = BuildChangelogDisplayText(preamble, false);
    if (!result.empty() && found_any) {
        result += "\n\n";
    }
    result += version_content;

    return ::sphaira::utils::TrimAsciiWhitespace(result);
}

auto BuildChangelogDisplayText(const std::string& section, bool add_bullets) -> std::string {
    std::istringstream stream(section);
    std::string line;
    std::string result;
    bool first_line = true;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto non_space = line.find_first_not_of(" \t");
        if (non_space == std::string::npos) {
            continue;
        }

        auto trimmed = line.substr(non_space);

        if (trimmed.size() >= 2 && trimmed[0] == '#' && trimmed[1] == '#') {
            continue;
        }
        if (!trimmed.empty() && trimmed.find_first_not_of('_') == std::string::npos) {
            continue;
        }
        if (trimmed.size() >= 3 && trimmed.find_first_not_of('-') == std::string::npos) {
            continue;
        }

        std::string indent;
        for (size_t i = 0; i < non_space; i++) {
            indent += line[i] == '\t' ? "    " : " ";
        }

        trimmed = NormalizeChangelogMarkdown(trimmed);

        if (!first_line) {
            result += '\n';
        }
        if (add_bullets) {
            result += "\u00A0\u00A0\u00A0\u00A0\u00A0\u2022 " + indent + trimmed;
        } else {
            result += indent + trimmed;
        }
        first_line = false;
    }

    return result;
}

auto NormalizeChangelogMarkdown(const std::string& text) -> std::string {
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        if (text[i] == '*') {
            if (i + 1 < text.size() && text[i + 1] == '*') {
                result += "**";
                i += 2;
            } else {
                i++;
            }
            continue;
        }

        result += text[i++];
    }

    return result;
}

auto IsFullBoldLine(const std::string& trimmed) -> bool {
    return trimmed.size() >= 5 &&
        trimmed[0] == '*' && trimmed[1] == '*' &&
        trimmed[trimmed.size() - 1] == '*' && trimmed[trimmed.size() - 2] == '*';
}


} // namespace detail
} // namespace sphaira::ui::menu::kefir
