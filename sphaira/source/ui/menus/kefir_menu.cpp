#include "ui/menus/kefir_menu.hpp"

#include "ui/error_box.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include "ams_su.h"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "hats_version.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

namespace sphaira::ui::menu::kefir {
namespace {

constexpr const char* NXLINKS_URL = "https://raw.githubusercontent.com/rashevskyv/nx-links/master/nx-links.json";
constexpr const char* CACHE_DIR = "/config/kefir-updater";
constexpr const char* NXLINKS_CACHE = "/config/kefir-updater/nx-links.json";
constexpr const char* AMS_ZIP = "/config/kefir-updater/atmo.zip";
constexpr const char* FIRMWARE_ZIP = "/config/kefir-updater/firmware.zip";
constexpr const char* KEFIR_PATH = "/kefir";
constexpr const char* FIRMWARE_DEST = "/firmware";
constexpr const char* KEFIR_VERSION_PATH = "/switch/kefir-updater/version";
constexpr const char* KEFIR_CHANGELOG_URL = "https://raw.githubusercontent.com/rashevskyv/kefir/master/changelog_full";
constexpr const char* COPY_FILES_TXT = "/config/kefir-updater/copy_files.txt";
constexpr const char* STAGED_COPY_FILES_TXT = "/kefir/config/kefir-updater/copy_files.txt";
constexpr const char* DOWNGRADE_FIX_SAVE = "/save/8000000000000073";
constexpr size_t UPDATE_TASK_BUFFER_SIZE = 0x100000;
constexpr s64 TILE_COLUMNS = 3;
constexpr s64 TILE_EMPTY = -1;
constexpr s64 UPDATER_LIST_PAGE_ROWS = 6;
constexpr float UPDATER_LIST_ROW_HEIGHT = 74.f;
constexpr float UPDATER_LIST_ROW_GAP = 8.f;
constexpr float UPDATER_INFO_Y_OFFSET = 11.f;
constexpr float UPDATER_INFO_ROW_GAP = 25.f;
constexpr float UPDATER_LIST_TOP_OFFSET = 1.f + 66.f;
constexpr float UPDATER_TILE_TOP_OFFSET = 1.f + 112.f;
constexpr float UPDATER_TILE_CLIP_TOP_OFFSET = UPDATER_TILE_TOP_OFFSET - 35.f;

struct FirmwareValidation {
    AmsSuUpdateInformation info{};
    AmsSuUpdateValidationInfo validation{};
};

class DowngradeHoldConfirmBox final : public Widget {
public:
    using Callback = std::function<void(bool)>;

    DowngradeHoldConfirmBox(std::string message, Callback callback)
    : m_message{std::move(message)}
    , m_callback{std::move(callback)} {
        m_pos = Vec4{230.f, 126.f, 820.f, 468.f};
        SetActions(
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                m_callback(false);
                SetPop();
            }})
        );
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);

        if (controller->GotHeld(Button::A)) {
            if (!m_holding) {
                m_holding = true;
                m_hold_start = armTicksToNs(armGetSystemTick());
            }

            const auto now = armTicksToNs(armGetSystemTick());
            m_progress = std::min(1.f, static_cast<float>(now - m_hold_start) / 3'000'000'000.f);
            if (m_progress >= 1.f) {
                m_callback(true);
                SetPop();
            }
        } else {
            m_holding = false;
            m_progress = 0.f;
        }
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        gfx::dimBackground(vg);
        gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

        constexpr float padding = 38.f;
        nvgSave(vg);
        nvgTextLineHeight(vg, 1.25f);
        gfx::drawTextBox(
            vg, m_pos.x + padding, m_pos.y + 30.f, 18.f, m_pos.w - padding * 2.f,
            theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP
        );
        nvgRestore(vg);

        const Vec4 button{m_pos.x, m_pos.y + m_pos.h - 86.f, m_pos.w, 86.f};
        gfx::drawRect(vg, button.x, button.y - 2.f, button.w, 2.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        gfx::drawRectOutline(vg, theme, 4.f, Vec4{button.x + 150.f, button.y + 10.f, button.w - 300.f, button.h - 20.f});

        const Vec4 bar{button.x + 178.f, button.y + button.h - 22.f, button.w - 356.f, 6.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * m_progress, bar.h, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 3.f);

        const auto hold_text = "Hold A for 3 seconds to continue"_i18n;
        gfx::drawText(
            vg, button.x + button.w / 2.f, button.y + 35.f, 22.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED),
            hold_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE
        );
    }

private:
    std::string m_message;
    Callback m_callback;
    bool m_holding{};
    u64 m_hold_start{};
    float m_progress{};
};

auto Trim(std::string value) -> std::string {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    size_t start{};
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }

    if (start) {
        value.erase(0, start);
    }
    return value;
}

auto ReadLineNumber(const char* path, size_t line_index) -> std::string {
    FILE* file = std::fopen(path, "r");
    if (!file) {
        return "Not Found";
    }
    ON_SCOPE_EXIT(std::fclose(file));

    char line[128]{};
    for (size_t i = 0; i <= line_index; i++) {
        if (!std::fgets(line, sizeof(line), file)) {
            return "Not Found";
        }
    }

    auto value = Trim(line);
    return value.empty() ? "Not Found" : value;
}

auto ReadFirstLine(const char* path) -> std::string {
    return ReadLineNumber(path, 0);
}

auto ReadSecondLine(const char* path) -> std::string {
    return ReadLineNumber(path, 1);
}

auto IsKnownVersion(const std::string& version) -> bool {
    if (version.empty() || version == "Not Found" || version == "Unknown") {
        return false;
    }

    return std::any_of(version.begin(), version.end(), [](unsigned char c) {
        return std::isdigit(c);
    });
}

auto FirmwareUnsupportedReason(const std::string& target, const std::string& supported) -> std::string {
    std::string message = "Firmware " + target + " is not supported by the current Kefir.";
    if (IsKnownVersion(supported)) {
        message += "\n\nCurrent Kefir supports system firmware up to " + supported + ".";
    }
    message += "\n\nUpdate Kefir first?";
    return message;
}

auto UnsupportedFirmwareLabel(const std::string& supported) -> std::string {
    if (IsKnownVersion(supported)) {
        return "Unsupported > " + supported;
    }
    return "Unsupported";
}

auto ReadCurrentKefirSupportedFirmware() -> std::string {
    const auto value = ReadSecondLine(KEFIR_VERSION_PATH);
    if (!IsKnownVersion(value)) {
        return "Not Found";
    }
    return value;
}

auto FindDigitsAfter(const std::string& value, std::string_view marker) -> std::string {
    auto pos = value.find(marker);
    if (pos == std::string::npos) {
        return {};
    }

    pos += marker.size();
    const auto start = pos;
    while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos]))) {
        pos++;
    }

    if (pos == start) {
        return {};
    }
    return value.substr(start, pos - start);
}

auto ExtractKefirVersion(const std::string& name, const std::string& url) -> std::string {
    for (const auto* value : { &url, &name }) {
        if (auto version = FindDigitsAfter(*value, "/download/"); !version.empty()) {
            return version;
        }
        if (auto version = FindDigitsAfter(*value, "kefir_"); !version.empty()) {
            return version;
        }
    }

    for (const auto* value : { &url, &name }) {
        for (size_t i = 0; i < value->size(); i++) {
            if (!std::isdigit(static_cast<unsigned char>((*value)[i]))) {
                continue;
            }

            size_t end = i;
            while (end < value->size() && std::isdigit(static_cast<unsigned char>((*value)[end]))) {
                end++;
            }

            const auto len = end - i;
            if (len >= 3 && len <= 4) {
                return value->substr(i, len);
            }
            i = end;
        }
    }

    return {};
}

auto MakeKefirLatestLabel(const UpdaterEntry& entry) -> std::string {
    if (const auto version = ExtractKefirVersion(entry.name, entry.url); !version.empty()) {
        return version;
    }
    return entry.name;
}

auto ParseKefirChangelogVersion(const std::string& version) -> int {
    std::string number;
    for (const auto c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            number += c;
        } else if (!number.empty()) {
            break;
        }
    }

    if (number.empty()) {
        return 0;
    }

    return std::strtol(number.c_str(), nullptr, 10);
}

auto IsUkrainianLanguage() -> bool {
    return App::GetLanguage() == 14;
}

auto ExtractChangelogSection(const std::string& raw, bool ukrainian) -> std::string {
    constexpr std::string_view ukr_header = "#### **UKR**";
    constexpr std::string_view eng_header = "#### **ENG**";
    constexpr std::string_view separator = "____";

    if (ukrainian) {
        const auto pos = raw.find(ukr_header);
        if (pos == std::string::npos) {
            return raw;
        }

        const auto start = pos + ukr_header.size();
        const auto sep_pos = raw.find(separator, start);
        return raw.substr(start, (sep_pos != std::string::npos ? sep_pos : raw.size()) - start);
    }

    const auto pos = raw.find(eng_header);
    if (pos == std::string::npos) {
        return raw;
    }
    return raw.substr(pos + eng_header.size());
}

auto IsFullBoldLine(const std::string& trimmed) -> bool {
    return trimmed.size() >= 5 &&
        trimmed[0] == '*' && trimmed[1] == '*' &&
        trimmed[trimmed.size() - 1] == '*' && trimmed[trimmed.size() - 2] == '*';
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

    std::string result;
    const auto display_preamble = BuildChangelogDisplayText(preamble, false);
    if (!display_preamble.empty()) {
        result += display_preamble + "\n\n";
    }

    const auto current_ver = ParseKefirChangelogVersion(current_version);
    const auto latest_available_ver = version_blocks.empty() ? 0 : version_blocks.front().first;

    should_skip = (latest_available_ver != target_ver);

    int start_ver = 0;
    int end_ver = target_ver;

    if (current_ver == target_ver) {
        start_ver = latest_available_ver;
        end_ver = latest_available_ver;
    } else {
        start_ver = current_ver + 1;
    }

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
        result += "**" + std::to_string(version) + "**\n";
        result += display_content + "\n\n";
    }

    if (!found_any) {
        result += "No changelog entries found.";
    }

    return Trim(result);
}

enum class ChangelogTextColour {
    Normal,
    Gray,
    Red,
    Blue,
};

struct ChangelogSegment {
    std::string text;
    bool bold{};
    bool underline{};
    ChangelogTextColour colour{ChangelogTextColour::Normal};
};

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

void ParseChangelogInline(const std::string& text, std::vector<ChangelogSegment>& out, bool bold = false,
    bool underline = false, ChangelogTextColour colour = ChangelogTextColour::Normal) {
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

auto TrimAsciiWhitespace(std::string value) -> std::string {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }

    size_t start{};
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        start++;
    }

    if (start) {
        value.erase(0, start);
    }
    return value;
}

auto IsVersionHeaderLine(const std::string& line) -> bool {
    const auto trimmed = TrimAsciiWhitespace(line);
    if (!IsFullBoldLine(trimmed)) {
        return false;
    }

    const auto content = trimmed.substr(2, trimmed.size() - 4);
    return !content.empty() && std::all_of(content.begin(), content.end(), [](unsigned char c) {
        return std::isdigit(c);
    });
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

        const auto is_header = IsVersionHeaderLine(line);
        const auto is_blank = TrimAsciiWhitespace(line).empty();
        const auto is_preamble = !reached_version_entries && !is_header && !is_blank;
        const auto font_size = is_header ? header_font_size : (is_preamble ? preamble_font_size : regular_font_size);
        const auto line_height = font_size * (is_header ? 1.35f : line_height_scale);

        if (is_header) {
            reached_version_entries = true;
        }

        const auto height = is_blank ? line_height * 0.55f :
            RenderChangelogLine(vg, theme, line, area.x, y, area.w, font_size, line_height, render);
        y += height;
        total_height += height;
    }

    return total_height;
}

auto TypeLabel(UpdaterEntryType type) -> const char* {
    switch (type) {
        case UpdaterEntryType::Section:
            return "";
        case UpdaterEntryType::Network:
            return "Network";
        case UpdaterEntryType::CustomLink:
            return "Other";
        case UpdaterEntryType::Kefir:
            return "Kefir";
        case UpdaterEntryType::Firmware:
            return "Firmware";
    }
    return "";
}

auto EntryDescription(const UpdaterEntry& entry) -> const char* {
    if (entry.type == UpdaterEntryType::Network) {
        return "Open GitHub releases and direct links.";
    }
    if (entry.type == UpdaterEntryType::CustomLink) {
        return "Enter a ZIP URL and extract it to the SD card.";
    }
    return entry.url.c_str();
}

auto EntryIsFolder(const UpdaterEntry& entry) -> bool {
    return entry.type == UpdaterEntryType::Network;
}

auto EntryIsDownload(const UpdaterEntry& entry) -> bool {
    return entry.type == UpdaterEntryType::Kefir ||
        entry.type == UpdaterEntryType::Firmware ||
        entry.type == UpdaterEntryType::CustomLink;
}

void DrawUpdaterEntryIcon(NVGcontext* vg, Theme* theme, const UpdaterEntry& entry, float x, float y, bool selected, bool disabled = false) {
    if (!EntryIsFolder(entry) && !EntryIsDownload(entry)) {
        return;
    }

    const auto colour = theme->GetColour(disabled ? ThemeEntryID_TEXT_INFO : (selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));

    nvgSave(vg);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 2.f);

    if (EntryIsFolder(entry)) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y + 3.f, 28.f, 19.f, 3.f);
        nvgRect(vg, x + 3.f, y, 11.f, 5.f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 14.f, y);
        nvgLineTo(vg, x + 14.f, y + 17.f);
        nvgMoveTo(vg, x + 7.f, y + 10.f);
        nvgLineTo(vg, x + 14.f, y + 17.f);
        nvgLineTo(vg, x + 21.f, y + 10.f);
        nvgMoveTo(vg, x + 5.f, y + 23.f);
        nvgLineTo(vg, x + 23.f, y + 23.f);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}

auto EntryDisplayName(const UpdaterEntry& entry) -> std::string {
    if (entry.type != UpdaterEntryType::Kefir) {
        return entry.name;
    }

    if (const auto version = ExtractKefirVersion(entry.name, entry.url); !version.empty()) {
        return "Version " + version;
    }

    auto name = entry.name;
    if (name.starts_with("Kefir")) {
        name.erase(0, std::strlen("Kefir"));
        name = Trim(name);
    }
    return name.empty() ? "Version" : "Version " + name;
}

class KefirChangelogBox final : public Widget {
public:
    using Callback = std::function<void()>;

    KefirChangelogBox(UpdaterEntry entry, Callback callback)
    : m_entry{std::move(entry)}
    , m_callback{std::move(callback)} {
        m_current_version = ReadFirstLine(KEFIR_VERSION_PATH);
        m_target_version = ExtractKefirVersion(m_entry.name, m_entry.url);

        if (const auto version = ParseKefirChangelogVersion(m_target_version)) {
            m_title = "Kefir " + std::to_string(version) + " changelog";
        } else {
            m_title = "Kefir changelog";
        }

        m_pos = Vec4{70.f, 42.f, 1140.f, 636.f};
        SetUiButtonPos({m_pos.x + m_pos.w - 30.f, m_pos.y + m_pos.h - 49.f});

        SetActions(
            std::make_pair(Button::A, Action{"Install"_i18n, [this](){
                if (m_loading || !m_unlocked || m_installing) {
                    App::PlaySoundEffect(SoundEffect_Limit);
                    return;
                }

                m_installing = true;
                m_callback();
                SetPop();
            }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                SetPop();
            }}),
            std::make_pair(Button::UP | Button::LS_UP | Button::RS_UP, Action{static_cast<u8>(ActionType::DOWN | ActionType::HELD), [this](){
                ScrollBy(-CHANGELOG_SCROLL_STEP);
            }}),
            std::make_pair(Button::DOWN | Button::LS_DOWN | Button::RS_DOWN, Action{static_cast<u8>(ActionType::DOWN | ActionType::HELD), [this](){
                ScrollBy(CHANGELOG_SCROLL_STEP);
            }}),
            std::make_pair(Button::L | Button::L2, Action{[this](){
                ScrollBy(-m_text_area.h);
            }}),
            std::make_pair(Button::R | Button::R2, Action{[this](){
                ScrollBy(m_text_area.h);
            }})
        );

        LoadChangelog();
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        gfx::dimBackground(vg);
        gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

        gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_pos.y + 28.f, 27.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED), m_title.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        const auto target = m_target_version.empty() ? "Unknown" : m_target_version;

        nvgSave(vg);
        nvgFontSize(vg, 17.f);
        nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT_INFO));

        // Draw "Current:" bold, version normal
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgText(vg, m_pos.x + 42.f, m_pos.y + 70.f, "Current:", nullptr);
        nvgText(vg, m_pos.x + 42.f + 1.f, m_pos.y + 70.f, "Current:", nullptr);

        float current_bounds[4]{};
        nvgTextBounds(vg, m_pos.x + 42.f, m_pos.y + 70.f, "Current:", nullptr, current_bounds);
        float current_width = current_bounds[2] - current_bounds[0];

        nvgText(vg, m_pos.x + 42.f + current_width + 7.f, m_pos.y + 70.f, m_current_version.c_str(), nullptr);

        // Draw "Target:" bold, version normal
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        float target_bounds[4]{};
        nvgTextBounds(vg, m_pos.x + m_pos.w - 42.f, m_pos.y + 70.f, target.c_str(), nullptr, target_bounds);
        float target_width = target_bounds[2] - target_bounds[0];

        nvgText(vg, m_pos.x + m_pos.w - 42.f, m_pos.y + 70.f, target.c_str(), nullptr);

        nvgText(vg, m_pos.x + m_pos.w - 42.f - target_width - 7.f, m_pos.y + 70.f, "Target:", nullptr);
        nvgText(vg, m_pos.x + m_pos.w - 42.f - target_width - 7.f + 1.f, m_pos.y + 70.f, "Target:", nullptr);
        nvgRestore(vg);

        gfx::drawRect(vg, m_pos.x, m_pos.y + 102.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        gfx::drawRect(vg, m_pos.x, m_pos.y + m_pos.h - 82.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

        m_text_area = Vec4{m_pos.x + 48.f, m_pos.y + 122.f, m_pos.w - 118.f, m_pos.h - 232.f};
        if (m_loading) {
            gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_text_area.y + m_text_area.h / 2.f, 24.f,
                theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading changelog...", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        } else {
            DrawChangelogText(vg, theme);
        }

        const auto footer = m_loading ? "Loading changelog..." :
            (m_unlocked ? "Ready to install." : "Scroll to the bottom to unlock Install.");
        gfx::drawText(vg, m_pos.x + 42.f, m_pos.y + m_pos.h - 52.f, 17.f,
            theme->GetColour(m_unlocked ? ThemeEntryID_TEXT_INFO : ThemeEntryID_ERROR),
            footer, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        Widget::Draw(vg, theme);
    }

private:
    static constexpr float CHANGELOG_FONT_SIZE = 20.f;
    static constexpr float CHANGELOG_HEADER_FONT_SIZE = 26.f;
    static constexpr float CHANGELOG_PREAMBLE_FONT_SIZE = 24.f;
    static constexpr float CHANGELOG_LINE_HEIGHT = 1.45f;
    static constexpr float CHANGELOG_SCROLL_STEP = 36.f;

    void LoadChangelog() {
        if (!ParseKefirChangelogVersion(m_target_version)) {
            bool dummy = false;
            m_text = BuildKefirChangelogText({}, m_current_version, m_target_version, dummy);
            m_loading = false;
            return;
        }

        const auto queued = curl::Api().ToMemoryAsync(
            curl::Url{KEFIR_CHANGELOG_URL},
            curl::StopToken{this->GetToken()},
            curl::OnComplete{[this](auto& result) {
                bool should_skip = false;
                if (!result.success || result.data.empty()) {
                    m_text = BuildKefirChangelogText({}, m_current_version, m_target_version, should_skip);
                } else {
                    const std::string raw{reinterpret_cast<const char*>(result.data.data()), result.data.size()};
                    m_text = BuildKefirChangelogText(raw, m_current_version, m_target_version, should_skip);
                }

                if (should_skip) {
                    m_callback();
                    SetPop();
                    return;
                }

                m_scroll = 0.f;
                m_max_scroll = 0.f;
                m_unlocked = false;
                m_loading = false;
            }}
        );

        if (!queued) {
            m_text = "Failed to queue changelog download.";
            m_loading = false;
        }
    }

    void ScrollBy(float amount) {
        if (m_loading || m_max_scroll <= 0.f) {
            return;
        }

        const auto old_scroll = m_scroll;
        m_scroll = std::clamp(m_scroll + amount, 0.f, m_max_scroll);
        if (old_scroll != m_scroll) {
            App::PlaySoundEffect(SoundEffect_Scroll);
        } else {
            App::PlaySoundEffect(SoundEffect_Limit);
        }
    }

    void DrawChangelogText(NVGcontext* vg, Theme* theme) {
        if (m_text.empty()) {
            m_text = "No changelog entries found.";
        }

        nvgSave(vg);
        m_text_height = RenderChangelogText(vg, theme, m_text, Vec4{0.f, 0.f, m_text_area.w, m_text_area.h}, 0.f, false,
            CHANGELOG_FONT_SIZE, CHANGELOG_LINE_HEIGHT, CHANGELOG_HEADER_FONT_SIZE, CHANGELOG_PREAMBLE_FONT_SIZE);
        m_max_scroll = std::max(0.f, m_text_height - m_text_area.h + 14.f);
        m_scroll = std::clamp(m_scroll, 0.f, m_max_scroll);

        if (m_max_scroll <= 0.5f || m_scroll >= m_max_scroll - 1.f) {
            m_unlocked = true;
        }

        nvgScissor(vg, m_text_area.x, m_text_area.y, m_text_area.w, m_text_area.h);
        RenderChangelogText(vg, theme, m_text, m_text_area, m_scroll, true,
            CHANGELOG_FONT_SIZE, CHANGELOG_LINE_HEIGHT, CHANGELOG_HEADER_FONT_SIZE, CHANGELOG_PREAMBLE_FONT_SIZE);
        nvgRestore(vg);

        const auto count = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_height / CHANGELOG_SCROLL_STEP)));
        const auto page = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_area.h / CHANGELOG_SCROLL_STEP)));
        const auto index = std::min<s64>(std::max<s64>(0, count - page), static_cast<s64>(std::ceil(m_scroll / CHANGELOG_SCROLL_STEP)));
        gfx::drawScrollbar2(vg, theme, m_text_area.x + m_text_area.w + 20.f, m_text_area.y, m_text_area.h, index, count, 1, page);
    }

    UpdaterEntry m_entry;
    Callback m_callback;
    std::string m_current_version;
    std::string m_target_version;
    std::string m_title;
    std::string m_text;
    Vec4 m_text_area{};
    float m_scroll{};
    float m_max_scroll{};
    float m_text_height{};
    bool m_loading{true};
    bool m_unlocked{};
    bool m_installing{};
};

auto IsSelectableEntry(const UpdaterEntry& entry) -> bool {
    return entry.type != UpdaterEntryType::Section;
}

auto ResolveSelectableIndex(const std::vector<UpdaterEntry>& entries, s64 index, s64 previous) -> s64 {
    if (entries.empty()) {
        return 0;
    }

    index = std::clamp<s64>(index, 0, static_cast<s64>(entries.size() - 1));
    if (IsSelectableEntry(entries[index])) {
        return index;
    }

    const auto direction = index >= previous ? 1 : -1;
    for (s64 i = index; i >= 0 && i < static_cast<s64>(entries.size()); i += direction) {
        if (IsSelectableEntry(entries[i])) {
            return i;
        }
    }

    for (s64 i = index; i >= 0 && i < static_cast<s64>(entries.size()); i -= direction) {
        if (IsSelectableEntry(entries[i])) {
            return i;
        }
    }

    return 0;
}

auto SelectableCount(const std::vector<UpdaterEntry>& entries) -> s64 {
    return std::count_if(entries.begin(), entries.end(), IsSelectableEntry);
}

auto SelectablePosition(const std::vector<UpdaterEntry>& entries, s64 index) -> s64 {
    s64 position{};
    for (s64 i = 0; i <= index && i < static_cast<s64>(entries.size()); i++) {
        if (IsSelectableEntry(entries[i])) {
            position++;
        }
    }
    return position;
}

auto TileSlots(const std::vector<UpdaterEntry>& entries) -> std::vector<s64> {
    std::vector<s64> out;
    bool has_group_entries{};

    for (s64 i = 0; i < static_cast<s64>(entries.size()); i++) {
        const auto& entry = entries[i];
        if (entry.type == UpdaterEntryType::Section) {
            if (has_group_entries) {
                while (out.size() % TILE_COLUMNS) {
                    out.emplace_back(TILE_EMPTY);
                }
            }
            has_group_entries = false;
            continue;
        }

        out.emplace_back(i);
        has_group_entries = true;
    }

    while (out.size() % TILE_COLUMNS) {
        out.emplace_back(TILE_EMPTY);
    }

    return out;
}

auto ResolveTileSlotIndex(const std::vector<s64>& slots, s64 index, s64 previous) -> s64 {
    if (slots.empty()) {
        return 0;
    }

    index = std::clamp<s64>(index, 0, static_cast<s64>(slots.size() - 1));
    if (slots[index] != TILE_EMPTY) {
        return index;
    }

    const auto direction = index >= previous ? 1 : -1;
    for (s64 i = index; i >= 0 && i < static_cast<s64>(slots.size()); i += direction) {
        if (slots[i] != TILE_EMPTY) {
            return i;
        }
    }

    for (s64 i = index; i >= 0 && i < static_cast<s64>(slots.size()); i -= direction) {
        if (slots[i] != TILE_EMPTY) {
            return i;
        }
    }

    return 0;
}

auto TileLabel(const UpdaterEntry& entry) -> std::string {
    switch (entry.type) {
        case UpdaterEntryType::Kefir:
            if (const auto version = ExtractKefirVersion(entry.name, entry.url); !version.empty()) {
                return version;
            }
            return EntryDisplayName(entry);
        case UpdaterEntryType::Firmware:
            return entry.name;
        case UpdaterEntryType::Network:
            return "Network";
        case UpdaterEntryType::CustomLink:
            return "Link";
        case UpdaterEntryType::Section:
            return {};
    }

    return {};
}

auto TileGroupLabel(UpdaterEntryType type) -> const char* {
    switch (type) {
        case UpdaterEntryType::Kefir:
            return "KEFIR";
        case UpdaterEntryType::Firmware:
            return "FIRMWARE";
        case UpdaterEntryType::Network:
        case UpdaterEntryType::CustomLink:
            return "OTHER";
        case UpdaterEntryType::Section:
            return "";
    }

    return "";
}

void AddSectionEntry(std::vector<UpdaterEntry>& out, std::string name) {
    out.push_back({
        .type = UpdaterEntryType::Section,
        .name = std::move(name),
        .url = {},
        .pack = false,
    });
}

void AddNetworkEntry(std::vector<UpdaterEntry>& out) {
    out.push_back({
        .type = UpdaterEntryType::Network,
        .name = "Network Downloads",
        .url = {},
        .pack = false,
    });
}

void AddCustomLinkEntry(std::vector<UpdaterEntry>& out) {
    out.push_back({
        .type = UpdaterEntryType::CustomLink,
        .name = "Custom Link",
        .url = {},
        .pack = false,
    });
}

void AppendEntriesOfType(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& entries, UpdaterEntryType type) {
    for (const auto& entry : entries) {
        if (entry.type == type) {
            out.push_back(entry);
        }
    }
}

void BuildSectionedEntries(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& downloads) {
    out.clear();

    AddSectionEntry(out, "KEFIR");
    AppendEntriesOfType(out, downloads, UpdaterEntryType::Kefir);

    AddSectionEntry(out, "FIRMWARE");
    AppendEntriesOfType(out, downloads, UpdaterEntryType::Firmware);

    AddSectionEntry(out, "OTHER");
    AddNetworkEntry(out);
    AddCustomLinkEntry(out);
}

auto AddJsonEntries(yyjson_val* object, UpdaterEntryType type, std::vector<UpdaterEntry>& out, std::string& latest_kefir, bool& latest_from_pack) -> bool {
    if (!object || !yyjson_is_obj(object)) {
        return false;
    }

    bool found{};
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(object, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        auto value = yyjson_obj_iter_get_val(key);
        const auto name = yyjson_get_str(key);
        const auto url = yyjson_get_str(value);
        if (!name || !url || !*url) {
            continue;
        }

        UpdaterEntry entry{
            .type = type,
            .name = name,
            .url = url,
            .pack = std::string_view{name}.find("[PACK]") != std::string_view::npos,
        };

        if (entry.type == UpdaterEntryType::Kefir && (latest_kefir.empty() || (entry.pack && !latest_from_pack))) {
            latest_kefir = MakeKefirLatestLabel(entry);
            latest_from_pack = entry.pack;
        }

        out.push_back(std::move(entry));
        found = true;
    }

    return found;
}

auto ParseUpdaterLinks(const fs::FsPath& path, std::vector<UpdaterEntry>& out, std::string& latest_kefir) -> bool {
    out.clear();
    latest_kefir.clear();

    auto doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto root = yyjson_doc_get_root(doc);
    auto cfws = root ? yyjson_obj_get(root, "cfws") : nullptr;
    auto atmosphere = cfws ? yyjson_obj_get(cfws, "Atmosphere") : nullptr;
    auto firmwares = root ? yyjson_obj_get(root, "firmwares") : nullptr;

    bool latest_from_pack{};
    bool found{};
    found |= AddJsonEntries(atmosphere, UpdaterEntryType::Kefir, out, latest_kefir, latest_from_pack);
    found |= AddJsonEntries(firmwares, UpdaterEntryType::Firmware, out, latest_kefir, latest_from_pack);
    return found;
}

auto CopyIfExists(ProgressBox* pbox, fs::FsNativeSd& fs, const fs::FsPath& src, const fs::FsPath& dst) -> Result {
    if (!fs.FileExists(src)) {
        R_SUCCEED();
    }

    R_TRY(fs.CreateDirectoryRecursivelyWithPath(dst));
    pbox->NewTransfer("Copying " + dst.toString());
    return pbox->CopyFile(&fs, src, dst, true);
}

auto CopyListedFiles(ProgressBox* pbox, fs::FsNativeSd& fs, const char* list_path) -> Result {
    FILE* file = std::fopen(list_path, "r");
    if (!file) {
        R_SUCCEED();
    }
    ON_SCOPE_EXIT(std::fclose(file));

    char line[FS_MAX_PATH * 2]{};
    while (std::fgets(line, sizeof(line), file)) {
        std::string entry{line};
        while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) {
            entry.pop_back();
        }
        if (entry.empty()) {
            continue;
        }

        const auto sep = entry.find('|');
        if (sep == std::string::npos) {
            continue;
        }

        fs::FsPath src{entry.substr(0, sep)};
        fs::FsPath dst{entry.substr(sep + 1)};
        if (!fs.FileExists(src) && src.s[0] == '/') {
            src = std::string{KEFIR_PATH} + src.s;
        }
        R_TRY(CopyIfExists(pbox, fs, src, dst));
    }

    R_SUCCEED();
}

auto DownloadAndInstallKefir(ProgressBox* pbox, const UpdaterEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.FileExists(AMS_ZIP)) {
        fs.DeleteFile(AMS_ZIP);
    }

    pbox->NewTransfer("Downloading " + entry.name);
    const auto result = curl::Api().ToFile(
        curl::Url{entry.url},
        curl::Path{AMS_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, 0x1);

    if (fs.DirExists(KEFIR_PATH)) {
        R_TRY(fs.DeleteDirectoryRecursively(KEFIR_PATH));
    }
    R_TRY(fs.CreateDirectoryRecursively(KEFIR_PATH));

    pbox->NewTransfer("Extracting to /kefir...");
    R_TRY(thread::TransferUnzipAll(pbox, AMS_ZIP, &fs, KEFIR_PATH));
    R_TRY(fs.Commit());

    R_TRY(CopyIfExists(pbox, fs, "/kefir/bootloader/hekate_ipl.ini", "/bootloader/hekate_ipl.ini"));
    R_TRY(CopyIfExists(pbox, fs, "/kefir/config/kefir-updater/kefir_updater.ini", "/bootloader/ini/!kefir_updater.ini"));
    R_TRY(CopyIfExists(pbox, fs, "/kefir/bootloader/res/ku.bmp", "/bootloader/res/ku.bmp"));
    R_TRY(CopyListedFiles(pbox, fs, COPY_FILES_TXT));
    R_TRY(CopyListedFiles(pbox, fs, STAGED_COPY_FILES_TXT));

    if (fs.FileExists(AMS_ZIP)) {
        fs.DeleteFile(AMS_ZIP);
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

auto FormatFirmwareVersion(u32 version) -> std::string {
    return std::to_string((version >> 26) & 0x1f) + "." +
           std::to_string((version >> 20) & 0x1f) + "." +
           std::to_string((version >> 16) & 0xf);
}

auto BuildFirmwareServicePath(const fs::FsPath& path) -> std::string {
    std::string service_path = path.s;
    if (service_path.empty()) {
        service_path = FIRMWARE_DEST;
    }
    if (service_path.back() != '/') {
        service_path += '/';
    }
    return service_path;
}

auto ValidateFirmware(FirmwareValidation* out, const fs::FsPath& path) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    R_TRY(amssuGetUpdateInformation(&out->info, service_path.c_str()));
    R_TRY(amssuValidateUpdate(&out->validation, service_path.c_str()));
    return out->validation.result;
}

void CleanupFirmwareFiles(ProgressBox* pbox, const fs::FsPath& path);

auto ApplyDowngradeFix(ProgressBox* pbox) -> Result {
    pbox->NewTransfer("Applying downgrade fix...");

    fs::FsNativeBis system(FsBisPartitionId_System, "");
    R_TRY(system.GetFsOpenResult());

    if (!system.FileExists(DOWNGRADE_FIX_SAVE)) {
        R_SUCCEED();
    }

    R_TRY(system.DeleteFile(DOWNGRADE_FIX_SAVE));
    R_TRY(system.Commit());
    R_SUCCEED();
}

auto InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path, bool apply_downgrade_fix) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    pbox->NewTransfer("Setting up system update...");
    R_TRY(amssuSetupUpdate(nullptr, UPDATE_TASK_BUFFER_SIZE, service_path.c_str(), use_exfat));

    AsyncResult prepare{};
    R_TRY(amssuRequestPrepareUpdate(&prepare));
    ON_SCOPE_EXIT(asyncResultClose(&prepare));
    pbox->NewTransfer("Preparing system update...");

    while (true) {
        rc = asyncResultWait(&prepare, 0);
        if (R_FAILED(rc) && rc != 0xea01) {
            return rc;
        }
        if (R_SUCCEEDED(rc)) {
            R_TRY(asyncResultGet(&prepare));
        }

        bool prepared = false;
        R_TRY(amssuHasPreparedUpdate(&prepared));
        if (prepared) {
            break;
        }

        NsSystemUpdateProgress progress{};
        R_TRY(amssuGetPrepareUpdateProgress(&progress));
        pbox->UpdateTransfer(progress.current_size, progress.total_size);
        svcSleepThread(50'000'000);
    }

    pbox->NewTransfer("Applying system update...");
    R_TRY(amssuApplyPreparedUpdate());

    if (apply_downgrade_fix) {
        R_TRY(ApplyDowngradeFix(pbox));
    }

    CleanupFirmwareFiles(pbox, path);

    R_SUCCEED();
}

auto GetFirmwareTargetName() -> std::string {
    bool emummc = false;
    Result rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 value{};
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65007, &value))) {
            emummc = value != 0;
        }
        splExit();
    }
    return emummc ? "emuMMC" : "sysMMC";
}

auto ParseVersion(const std::string& version) -> std::vector<int> {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) {
            continue;
        }

        char* end = nullptr;
        const auto part = std::strtol(segment.c_str(), &end, 10);
        if (end == segment.c_str()) {
            break;
        }
        parts.push_back(static_cast<int>(part));
    }

    return parts;
}

auto IsVersionLower(const std::string& target, const std::string& current) -> bool {
    const auto target_parts = ParseVersion(target);
    const auto current_parts = ParseVersion(current);
    const auto max_len = std::max(target_parts.size(), current_parts.size());

    for (size_t i = 0; i < max_len; i++) {
        const auto t = i < target_parts.size() ? target_parts[i] : 0;
        const auto c = i < current_parts.size() ? current_parts[i] : 0;
        if (t < c) {
            return true;
        }
        if (t > c) {
            return false;
        }
    }

    return false;
}

auto DownloadAndExtractFirmware(ProgressBox* pbox, const UpdaterEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }

    pbox->NewTransfer("Downloading " + entry.name);
    const auto result = curl::Api().ToFile(
        curl::Url{entry.url},
        curl::Path{FIRMWARE_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, 0x1);

    if (fs.DirExists(FIRMWARE_DEST)) {
        R_TRY(fs.DeleteDirectoryRecursively(FIRMWARE_DEST));
    }
    R_TRY(fs.CreateDirectoryRecursively(FIRMWARE_DEST));

    pbox->NewTransfer("Extracting to /firmware...");
    R_TRY(thread::TransferUnzipAll(pbox, FIRMWARE_ZIP, &fs, FIRMWARE_DEST));
    R_TRY(fs.Commit());

    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

void CleanupFirmwareFiles(ProgressBox* pbox, const fs::FsPath& path) {
    fs::FsNativeSd fs;
    if (R_FAILED(fs.GetFsOpenResult())) {
        return;
    }

    fs::FsPath firmware_path = path;
    if (firmware_path.s[0] == '\0') {
        firmware_path = FIRMWARE_DEST;
    }
    pbox->NewTransfer("Removing firmware files...");

    if (fs.DirExists(firmware_path)) {
        fs.DeleteDirectoryRecursively(firmware_path);
    }
    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }
    fs.Commit();
}

} // namespace

Menu::Menu() : MenuBase{"Updater", MenuFlag_None} {
    RefreshSystemInfo();

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (!m_entries.empty() && !m_loading) {
                OpenSelected();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            RefreshSystemInfo();
            FetchLinks();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    OnLayoutChange();
}

Menu::~Menu() = default;

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_entries.empty()) {
        return;
    }

    if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles) {
        if (controller->GotDown(Button::RIGHT)) {
            MoveTileSelection(1);
            return;
        } else if (controller->GotDown(Button::LEFT)) {
            MoveTileSelection(-1);
            return;
        } else if (controller->GotDown(Button::DOWN)) {
            MoveTileSelection(TILE_COLUMNS);
            return;
        } else if (controller->GotDown(Button::UP)) {
            MoveTileSelection(-TILE_COLUMNS);
            return;
        } else if (controller->GotDown(Button::R2)) {
            MoveTileSelection(TILE_COLUMNS * 2);
            return;
        } else if (controller->GotDown(Button::L2)) {
            MoveTileSelection(-TILE_COLUMNS * 2);
            return;
        }

        m_list->OnUpdate(controller, touch, m_tile_index, m_tile_entries.size(), [this](bool touch, auto i) {
            const auto tile_index = ResolveTileSlotIndex(m_tile_entries, i, m_tile_index);
            if (touch && m_tile_index == tile_index) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_tile_index = tile_index;
                if (tile_index >= 0 && tile_index < static_cast<s64>(m_tile_entries.size()) && m_tile_entries[tile_index] != TILE_EMPTY) {
                    SetIndex(m_tile_entries[tile_index]);
                }
            }
        }, this);
    } else {
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto tiles = static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles;
    const auto info_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto info_y = GetY() + UPDATER_INFO_Y_OFFSET;
    nvgSave(vg);
    nvgFontSize(vg, 17.f);
    nvgFillColor(vg, info_colour);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    // Current Kefir
    nvgText(vg, 80.f, info_y, "Current Kefir:", nullptr);
    nvgText(vg, 81.f, info_y, "Current Kefir:", nullptr);
    float ck_bounds[4]{};
    nvgTextBounds(vg, 80.f, info_y, "Current Kefir:", nullptr, ck_bounds);
    nvgText(vg, ck_bounds[2] + 7.f, info_y, m_current_kefir.c_str(), nullptr);

    // Latest Kefir
    nvgText(vg, 650.f, info_y, "Latest Kefir:", nullptr);
    nvgText(vg, 651.f, info_y, "Latest Kefir:", nullptr);
    float lk_bounds[4]{};
    nvgTextBounds(vg, 650.f, info_y, "Latest Kefir:", nullptr, lk_bounds);
    nvgText(vg, lk_bounds[2] + 7.f, info_y, m_latest_kefir.c_str(), nullptr);

    // Current Firmware
    nvgText(vg, 80.f, info_y + UPDATER_INFO_ROW_GAP, "Current Firmware:", nullptr);
    nvgText(vg, 81.f, info_y + UPDATER_INFO_ROW_GAP, "Current Firmware:", nullptr);
    float cf_bounds[4]{};
    nvgTextBounds(vg, 80.f, info_y + UPDATER_INFO_ROW_GAP, "Current Firmware:", nullptr, cf_bounds);
    nvgText(vg, cf_bounds[2] + 7.f, info_y + UPDATER_INFO_ROW_GAP, m_current_firmware.c_str(), nullptr);

    // Console
    nvgText(vg, 650.f, info_y + UPDATER_INFO_ROW_GAP, "Console:", nullptr);
    nvgText(vg, 651.f, info_y + UPDATER_INFO_ROW_GAP, "Console:", nullptr);
    float c_bounds[4]{};
    nvgTextBounds(vg, 650.f, info_y + UPDATER_INFO_ROW_GAP, "Console:", nullptr, c_bounds);
    nvgText(vg, c_bounds[2] + 7.f, info_y + UPDATER_INFO_ROW_GAP, m_console_revision.c_str(), nullptr);

    nvgRestore(vg);

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading updater links...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, 80.f, GetY() + 71.f, 17.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
    }

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No updater entries found");
        return;
    }

    if (tiles) {
        DrawTiles(vg, theme);
    } else {
        DrawList(vg, theme);
    }
}

void Menu::DrawList(NVGcontext* vg, Theme* theme) {
    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& entry = m_entries[i];
        if (entry.type == UpdaterEntryType::Section) {
            const auto top_pad = 32.f;
            const Vec4 band{v.x, v.y + top_pad, v.w, 26.f};
            gfx::drawRect(vg, band.x, band.y, band.w, band.h, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 4.f);
            gfx::drawRect(vg, v.x + 15.f, band.y + 7.f, 4.f, band.h - 14.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 2.f);
            gfx::drawTextArgs(vg, v.x + 30.f, band.y + band.h / 2.f, 16.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                "%s", entry.name.c_str());
            return;
        }

        const auto selected = m_index == i;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto name_id = unsupported ? ThemeEntryID_TEXT_INFO : (downgrade ? ThemeEntryID_ERROR : text_id);

        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_entries.size() - 1) {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        std::string name = EntryDisplayName(entry);
        if (unsupported) {
            name += " [UNSUPPORTED]";
        }

        const auto text_x = v.x + 55.f;
        DrawUpdaterEntryIcon(vg, theme, entry, v.x + 15.f, v.y + 24.f, selected, unsupported);

        gfx::drawTextBox(vg, text_x, v.y + 11.f, 23.f, v.w - 230.f,
            theme->GetColour(name_id), name.c_str());

        if (entry.type != UpdaterEntryType::Kefir) {
            const auto label = unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE" : TypeLabel(entry.type));
            const auto label_colour = (entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO);
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 17.f, 15.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, label_colour,
                "%s", label.c_str());
        }

        gfx::drawTextBox(vg, text_x, v.y + 44.f, 16.f, v.w - 70.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), EntryDescription(entry));
    });
}

void Menu::DrawTiles(NVGcontext* vg, Theme* theme) {
    m_list->Draw(vg, theme, m_tile_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto tile_i) {
        const auto entry_index = m_tile_entries[tile_i];
        if (entry_index == TILE_EMPTY) {
            return;
        }

        const auto& entry = m_entries[entry_index];
        const auto selected = m_index == entry_index;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);

        s64 previous_entry_index = TILE_EMPTY;
        for (s64 i = tile_i - 1; i >= 0; i--) {
            if (m_tile_entries[i] != TILE_EMPTY) {
                previous_entry_index = m_tile_entries[i];
                break;
            }
        }

        const bool show_group = previous_entry_index == TILE_EMPTY ||
            TileGroupLabel(entry.type) != std::string_view{TileGroupLabel(m_entries[previous_entry_index].type)};

        if (show_group) {
            gfx::drawTextArgs(vg, v.x, v.y - 33.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", TileGroupLabel(entry.type));
        }

        const auto tile = v;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, tile);
        } else {
            gfx::drawRect(vg, tile, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 16.f);
        }

        // Draw icon container frame (subtle background)
        gfx::drawRect(vg, tile.x + 20.f, tile.y + 20.f, 115.f, 115.f, nvgRGBA(0, 0, 0, 25), 8.f);

        // Center and scale the vector icon inside the 115x115 container
        // Original icon size: 28x23. With 2.5x scale: 70x57.5.
        const float ix = tile.x + 20.f + (115.f - 70.f) / 2.f;
        const float iy = tile.y + 20.f + (115.f - 57.5f) / 2.f;

        nvgSave(vg);
        nvgTranslate(vg, ix, iy);
        nvgScale(vg, 2.5f, 2.5f);
        DrawUpdaterEntryIcon(vg, theme, entry, 0.f, 0.f, selected, unsupported);
        nvgRestore(vg);

        // Draw texts on the right side of the card
        const auto name_colour = unsupported ? theme->GetColour(ThemeEntryID_TEXT_INFO) : downgrade ? theme->GetColour(ThemeEntryID_ERROR) :
            theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);

        std::string name = EntryDisplayName(entry);
        if (unsupported) {
            name += " [UNSUPPORTED]";
        }

        const float text_x = tile.x + 148.f;
        const float text_clip_w = tile.w - 20.f - 148.f;

        // 1. Title/Name
        gfx::drawTextBox(vg, text_x, tile.y + 24.f, 18.f, text_clip_w, name_colour, name.c_str());

        // 2. Type/Status
        const auto type_label = unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE" : TypeLabel(entry.type));
        const auto type_colour = (entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawTextBox(vg, text_x, tile.y + 68.f, 14.f, text_clip_w, type_colour, type_label.c_str());

        // 3. Description
        const char* description = EntryDescription(entry);
        if (entry.type == UpdaterEntryType::Kefir || entry.type == UpdaterEntryType::Firmware) {
            description = "";
        }
        gfx::drawTextBox(vg, text_x, tile.y + 92.f, 14.f, text_clip_w, theme->GetColour(ThemeEntryID_TEXT_INFO), description);
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    RefreshSystemInfo();

    if (!m_loaded && !m_loading) {
        FetchLinks();
    }
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Updater Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items view_items{
        "List"_i18n,
        "Grid"_i18n,
    };

    options->Add<SidebarEntryArray>("Layout"_i18n, view_items, [this](s64& index_out) {
        m_view_mode.Set(index_out);
        OnLayoutChange();
    }, m_view_mode.Get());
}

void Menu::OnLayoutChange() {
    auto make_content_pos = [this](float top) {
        auto pos = m_pos;
        pos.y = top;
        pos.h = std::max(0.f, GetY() + GetH() - top);
        return pos;
    };

    Vec4 content_pos{};
    if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles) {
        constexpr float x = 75.f;
        constexpr float tile_w = 370.f;
        constexpr float tile_h = 155.f;
        constexpr float x_gap = 10.f;
        constexpr float y_gap = 65.f;
        content_pos = make_content_pos(GetY() + UPDATER_TILE_CLIP_TOP_OFFSET);
        const Vec4 v{x, GetY() + UPDATER_TILE_TOP_OFFSET, tile_w, tile_h};
        m_list = std::make_unique<List>(3, 2 * 3, content_pos, v, Vec2{x_gap, y_gap});
    } else {
        content_pos = make_content_pos(GetY() + UPDATER_LIST_TOP_OFFSET);
        const Vec4 v{75.f, GetY() + UPDATER_LIST_TOP_OFFSET, 1220.f - 150.f, UPDATER_LIST_ROW_HEIGHT};
        m_list = std::make_unique<List>(1, UPDATER_LIST_PAGE_ROWS, content_pos, v, Vec2{0.f, UPDATER_LIST_ROW_GAP});
    }
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetScrollBarPos(m_pos.x + m_pos.w, content_pos.y, content_pos.h);

    m_tile_entries = TileSlots(m_entries);
    const auto it = std::ranges::find(m_tile_entries, m_index);
    m_tile_index = it == m_tile_entries.end() ? 0 : std::distance(m_tile_entries.begin(), it);
    EnsureTileVisible();
}

void Menu::EnsureTileVisible() {
    if (!m_list || static_cast<UpdaterViewMode>(m_view_mode.Get()) != UpdaterViewMode::Tiles || m_tile_entries.empty()) {
        return;
    }

    constexpr s64 visible_rows = 2;
    const auto row = m_tile_index / TILE_COLUMNS;
    const auto first_visible_row = static_cast<s64>(m_list->GetYoff() / m_list->GetMaxY());

    if (row < first_visible_row) {
        m_list->SetYoff(static_cast<float>(row) * m_list->GetMaxY());
    } else if (row >= first_visible_row + visible_rows) {
        m_list->SetYoff(static_cast<float>(row - visible_rows + 1) * m_list->GetMaxY());
    }
}

bool Menu::MoveTileSelection(s64 step) {
    if (m_tile_entries.empty()) {
        return false;
    }

    const auto old_index = m_tile_index;
    const auto target = std::clamp<s64>(m_tile_index + step, 0, static_cast<s64>(m_tile_entries.size() - 1));
    const auto next_index = ResolveTileSlotIndex(m_tile_entries, target, m_tile_index);
    if (next_index == old_index || m_tile_entries[next_index] == TILE_EMPTY) {
        return false;
    }

    m_tile_index = next_index;
    SetIndex(m_tile_entries[m_tile_index]);
    App::PlaySoundEffect(SoundEffect_Focus);
    return true;
}

void Menu::FetchLinks() {
    m_loading = true;
    m_error_message.clear();
    m_entries.clear();
    m_tile_entries.clear();
    m_latest_kefir = "Unknown";
    BuildSectionedEntries(m_entries, {});
    m_tile_entries = TileSlots(m_entries);
    SetIndex(0);

    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_DIR);

    curl::Api().ToFileAsync(
        curl::Url{NXLINKS_URL},
        curl::Path{NXLINKS_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            std::vector<UpdaterEntry> entries;
            std::string latest_kefir;
            if (!result.success || !ParseUpdaterLinks(result.path, entries, latest_kefir)) {
                m_error_message = "Failed to load updater lists.";
                SetIndex(0);
                return false;
            }

            BuildSectionedEntries(m_entries, entries);
            m_tile_entries = TileSlots(m_entries);
            m_latest_kefir = latest_kefir.empty() ? "Unknown" : latest_kefir;

            if (entries.empty()) {
                m_error_message = "No Kefir or firmware downloads found.";
            }

            SetIndex(0);
            return true;
        }}
    );
}

void Menu::SetIndex(s64 index) {
    m_index = ResolveSelectableIndex(m_entries, index, m_index);
    if (m_list) {
        if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::List) {
            const auto max = UPDATER_LIST_ROW_HEIGHT + UPDATER_LIST_ROW_GAP;
            const auto start = static_cast<s64>(m_list->GetYoff() / max);
            if (m_index < start) {
                m_list->SetYoff(m_index * max);
            } else if (m_index >= start + UPDATER_LIST_PAGE_ROWS) {
                m_list->SetYoff((m_index - UPDATER_LIST_PAGE_ROWS + 1) * max);
            }
        }
    }

    if (m_index <= 1) {
        m_list->SetYoff(0);
    }

    const auto it = std::ranges::find(m_tile_entries, m_index);
    if (it != m_tile_entries.end()) {
        m_tile_index = std::distance(m_tile_entries.begin(), it);
        EnsureTileVisible();
    }

    UpdateSubheading();
}

void Menu::OpenSelected() {
    if (m_entries.empty() || m_index >= static_cast<s64>(m_entries.size())) {
        return;
    }

    const auto entry = m_entries[m_index];
    switch (entry.type) {
        case UpdaterEntryType::Network:
            App::Push<ui::menu::gh::Menu>(MenuFlag_None);
            break;
        case UpdaterEntryType::CustomLink:
            ui::menu::gh::DownloadDirectLink();
            break;
        case UpdaterEntryType::Kefir:
            InstallKefir(entry);
            break;
        case UpdaterEntryType::Firmware:
            DownloadFirmware(entry);
            break;
        case UpdaterEntryType::Section:
            break;
    }
}

void Menu::InstallKefir(const UpdaterEntry& entry, std::function<void()> on_success) {
    App::Push<KefirChangelogBox>(entry,
        [this, entry, on_success = std::move(on_success)]() mutable {
            App::Push<ProgressBox>(0, "Installing"_i18n, entry.name,
                [entry](auto pbox) -> Result {
                    return DownloadAndInstallKefir(pbox, entry);
                },
                [this, entry, on_success = std::move(on_success)](Result rc) mutable {
                    if (R_FAILED(rc)) {
                        App::Push<ErrorBox>(rc, "Failed to install " + entry.name);
                        return;
                    }

                    RefreshSystemInfo();
                    if (on_success) {
                        on_success();
                        return;
                    }

                    App::Push<OptionBox>(
                        "Kefir package installed.\n\nReboot now?",
                        "Later"_i18n, "Reboot"_i18n, 1,
                        [](auto op_index) {
                            if (op_index && *op_index == 1) {
                                utils::requestForcedReboot();
                            }
                        });
                });
        });
}

void Menu::DownloadFirmware(const UpdaterEntry& entry, bool skip_support_check) {
    if (!skip_support_check && !IsFirmwareSupported(entry.name)) {
        PromptKefirThenFirmware(entry);
        return;
    }

    const auto downgrade = IsDowngrade(entry.name);

    std::string message = "Download firmware " + entry.name + "?\n\n";
    message += "It will be staged at ";
    message += FIRMWARE_ZIP;
    message += " and extracted to /firmware.";

    if (downgrade) {
        message = "Firmware downgrade warning\n\n";
        message += "Current: " + m_current_firmware + "\n";
        message += "Target: " + entry.name + "\n\n";
        message += "Downgrading system firmware may prevent the console from booting until a factory reset is performed.\n\n";
        message += "Fix path: hekate > Payloads > TegraExplorer > DowngradeFix.te\n";
        message += "Guide: https://bit.ly/fw_downgrade\n\n";
        message += "Continue?";
    }

    App::Push<OptionBox>(message, "Cancel"_i18n, "Download"_i18n, 1,
        [this, entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Downloading"_i18n, entry.name,
                [entry](auto pbox) -> Result {
                    return DownloadAndExtractFirmware(pbox, entry);
                },
                [this, entry](Result rc) {
                    if (R_FAILED(rc)) {
                        App::Push<ErrorBox>(rc, "Failed to download " + entry.name);
                        return;
                    }

                    PromptInstallFirmware(entry.name);
                });
        });
}

void Menu::PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, display_name,
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Validating firmware contents...");
            return ValidateFirmware(validation.get(), path);
        },
        [this, display_name, path, validation](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware validation failed");
                return;
            }

            const auto version = FormatFirmwareVersion(validation->info.version);
            const bool use_exfat = validation->info.exfat_supported &&
                                   R_SUCCEEDED(validation->validation.exfat_result);
            std::string message = "Install firmware " + version + " on " + GetFirmwareTargetName() + "?\n\n";
            message += use_exfat ? "FAT32 + exFAT support\n" : "FAT32 support only\n";
            message += "Do not power off the console during installation.";

            App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
                [this, display_name, path, version](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }

                    if (!IsDowngrade(version)) {
                        InstallFirmware(display_name, path);
                        return;
                    }

                    std::string warning = "Firmware downgrade warning\n\n";
                    warning += "Current: " + m_current_firmware + "\n";
                    warning += "Target: " + version + "\n\n";
                    warning += "Downgrading firmware can cause boot problems. Make sure you know what you are doing and have a NAND or emuMMC backup.\n\n";
                    warning += "If you continue, Sphaira will install the firmware and automatically apply the downgrade fix after installation.\n\n";
                    warning += "By continuing, you accept full responsibility.";

                    App::Push<DowngradeHoldConfirmBox>(warning,
                        [this, display_name, path](bool accepted) {
                            if (accepted) {
                                InstallFirmware(display_name, path, true);
                            }
                        });
                });
        });
}

void Menu::InstallFirmware(const std::string& display_name, const fs::FsPath& path, bool apply_downgrade_fix) {
    App::Push<ProgressBox>(0, "Updating Firmware"_i18n, display_name,
        [path, apply_downgrade_fix](auto pbox) -> Result {
            FirmwareValidation validation{};
            R_TRY(ValidateFirmware(&validation, path));
            const bool use_exfat = validation.info.exfat_supported &&
                                   R_SUCCEEDED(validation.validation.exfat_result);
            return InstallValidatedFirmware(pbox, use_exfat, path, apply_downgrade_fix);
        },
        [apply_downgrade_fix](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware update failed");
                return;
            }

            std::string message = "Firmware update applied successfully.";
            if (apply_downgrade_fix) {
                message += "\n\nDowngrade fix applied.";
            }
            message += "\n\nReboot now?";

            App::Push<OptionBox>(
                message,
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        utils::requestForcedReboot();
                    }
                });
        }, false);
}

void Menu::UpdateSubheading() {
    const auto count = SelectableCount(m_entries);
    if (m_entries.empty() || !count) {
        this->SetSubHeading("0 / 0");
        return;
    }

    const auto& entry = m_entries[m_index];
    this->SetSubHeading(std::to_string(SelectablePosition(m_entries, m_index)) + " / " + std::to_string(count) + " - " + TypeLabel(entry.type));
}

void Menu::RefreshSystemInfo() {
    m_current_kefir = ReadFirstLine(KEFIR_VERSION_PATH);
    m_supported_firmware = ReadCurrentKefirSupportedFirmware();
    m_current_firmware = hats::getSystemFirmware();
    m_console_revision = hats::isErista() ? "Erista (v1)" : "Mariko (v2)";
    if (m_latest_kefir.empty()) {
        m_latest_kefir = "Unknown";
    }
}

bool Menu::IsDowngrade(const std::string& target_version) const {
    return IsVersionLower(target_version, m_current_firmware);
}

bool Menu::IsFirmwareSupported(const std::string& target_version) const {
    if (!IsKnownVersion(m_supported_firmware)) {
        return true;
    }

    return !IsVersionLower(m_supported_firmware, target_version);
}

bool Menu::FindKefirUpdate(UpdaterEntry& out) const {
    for (const auto& entry : m_entries) {
        if (entry.type == UpdaterEntryType::Kefir && entry.pack) {
            out = entry;
            return true;
        }
    }

    for (const auto& entry : m_entries) {
        if (entry.type == UpdaterEntryType::Kefir) {
            out = entry;
            return true;
        }
    }

    return false;
}

void Menu::PromptKefirThenFirmware(const UpdaterEntry& firmware_entry) {
    UpdaterEntry kefir_entry;
    if (!FindKefirUpdate(kefir_entry)) {
        App::Push<ErrorBox>(0x1, "Firmware " + firmware_entry.name + " is not supported by the current Kefir, and no Kefir update entry was found.");
        return;
    }

    App::Push<OptionBox>(
        FirmwareUnsupportedReason(firmware_entry.name, m_supported_firmware),
        "Cancel"_i18n, "Update Kefir"_i18n, 1,
        [this, kefir_entry, firmware_entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            InstallKefir(kefir_entry, [this, firmware_entry]() {
                DownloadFirmware(firmware_entry, true);
            });
        });
}

} // namespace sphaira::ui::menu::kefir
