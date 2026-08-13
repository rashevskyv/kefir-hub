#pragma once

#include "ui/widget.hpp"
#include "ui/menus/kefir_menu.hpp"
#include <string>
#include <vector>
#include <functional>



namespace sphaira::ui::menu::kefir {

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

} // namespace sphaira::ui::menu::kefir

namespace sphaira::ui {
    class ProgressBox;
}

namespace sphaira::ui::menu::kefir {
using ProgressBox = sphaira::ui::ProgressBox;


namespace detail {

void AddChangelogSegment(std::vector<ChangelogSegment>& out, std::string text, bool bold, bool underline, ChangelogTextColour colour);
auto IsUrlStart(const std::string& text, size_t pos) -> bool;
void ParseChangelogInline(const std::string& text, std::vector<ChangelogSegment>& out, bool bold = false,
    bool underline = false, ChangelogTextColour colour = ChangelogTextColour::Normal);
auto ChangelogSegmentColour(const ChangelogSegment& segment, Theme* theme) -> NVGcolor;
auto ChangelogSpaceWidth(NVGcontext* vg, float font_size) -> float;
auto MeasureWord(NVGcontext* vg, const std::string& word, float font_size) -> float;
auto RenderChangelogLine(NVGcontext* vg, Theme* theme, const std::string& line, float x, float y, float width,
    float font_size, float line_height, bool render) -> float;
auto RenderChangelogText(NVGcontext* vg, Theme* theme, const std::string& text, const Vec4& area, float scroll, bool render,
    float regular_font_size, float line_height_scale, float header_font_size, float preamble_font_size) -> float;
auto BuildKefirChangelogText(const std::string& raw, const std::string& current_version, const std::string& target_version, bool& should_skip) -> std::string;
auto BuildChangelogDisplayText(const std::string& section, bool add_bullets) -> std::string;
auto NormalizeChangelogMarkdown(const std::string& text) -> std::string;
auto IsFullBoldLine(const std::string& trimmed) -> bool;
auto ExtractChangelogSection(const std::string& raw, bool ukrainian) -> std::string;
auto ParseKefirChangelogVersion(const std::string& version) -> int;
auto IsUkrainianLanguage() -> bool;
auto DownloadAndInstallKefir(ProgressBox* pbox, const UpdaterEntry& entry) -> Result;

} // namespace detail


class KefirChangelogBox final : public Widget {
public:
    using Callback = std::function<void()>;

    KefirChangelogBox(UpdaterEntry entry, Callback callback);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    auto IsModal() const -> bool override { return true; }

private:
    static constexpr float CHANGELOG_FONT_SIZE = 20.f;
    static constexpr float CHANGELOG_HEADER_FONT_SIZE = 26.f;
    static constexpr float CHANGELOG_PREAMBLE_FONT_SIZE = 24.f;
    static constexpr float CHANGELOG_LINE_HEIGHT = 1.45f;
    static constexpr float CHANGELOG_SCROLL_STEP = 36.f;

    void LoadChangelog();
    void ScrollBy(float amount);
    void DrawChangelogText(NVGcontext* vg, Theme* theme);

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
    bool m_button_focused{false};
    Vec4 m_install_button_rect{};
};

} // namespace sphaira::ui::menu::kefir
