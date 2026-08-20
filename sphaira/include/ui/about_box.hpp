#pragma once

#include "ui/widget.hpp"
#include <string>

namespace sphaira::ui {

class AboutBox final : public Widget {
public:
    AboutBox();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    auto IsModal() const -> bool override { return true; }

private:
    static constexpr float FONT_SIZE = 19.f;
    static constexpr float HEADER_FONT_SIZE = 24.f;
    static constexpr float LINE_HEIGHT = 1.45f;
    static constexpr float SCROLL_STEP = 36.f;

    void LoadChangelog();
    void FetchLatestChangelog();
    void ScrollBy(float amount);
    void DrawChangelogText(NVGcontext* vg, Theme* theme);

    std::string m_title{"About Kefir Hub"};
    std::string m_version_str;
    std::string m_text;
    Vec4 m_text_area{};
    float m_scroll{0.f};
    float m_max_scroll{0.f};
    float m_text_height{0.f};
    bool m_loading{true};
    bool m_touch_dragging{false};
    float m_touch_last_y{0.f};
};

} // namespace sphaira::ui
