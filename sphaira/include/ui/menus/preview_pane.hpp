#pragma once

#include "ui/widget.hpp"
#include "utils/thread.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace sphaira::ui::menu::tools {

// what the pane shows for the row under the cursor: a picture fetched from `url` and a few lines of text
struct PreviewInfo {
    std::string key;   // names the cached picture under /config/kefir/cache/thumbs/; empty: no picture
    std::string url;   // where the picture is, or empty when `resolve` finds out
    std::function<std::string(std::stop_token)> resolve; // worker thread: the url of the picture, empty when there is none
    std::function<void(std::stop_token)> after; // worker thread, after the picture (cached or fetched): game facts
    std::string title;
    std::vector<std::string> lines;
};

// The right side of a file list. The text shows at once; the picture is fetched only when the cursor
// has stayed on a row for DWELL_MS, so scrolling through a list asks for nothing.
class PreviewPane {
public:
    static constexpr int DWELL_MS = 500;

    PreviewPane() = default;
    ~PreviewPane();

    void Show(PreviewInfo info); // the cursor moved to a row
    void Clear();                // the cursor is on a row with nothing to show
    void Update();               // every frame: starts the fetch once the cursor has dwelt
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& area);

    // only a pane that holds a picture can take the focus or open it full screen
    auto HasPicture() const -> bool { return m_texture && m_w > 0 && m_h > 0; }
    void SetFocused(bool focused) { m_focused = focused && HasPicture(); if (!m_focused) m_full = false; }
    auto Focused() const -> bool { return m_focused; }
    void SetFull(bool full) { m_full = full && m_focused; }
    auto Full() const -> bool { return m_full; }

private:
    void Cancel();
    void DropTexture();

    PreviewInfo m_info{};
    bool m_has{};
    bool m_started{};
    bool m_failed{};
    bool m_focused{};
    bool m_full{};
    int m_texture{};
    int m_w{}, m_h{};
    std::chrono::steady_clock::time_point m_since{};
    std::stop_source m_stop{};
    std::unique_ptr<utils::Async> m_loader{};
};

} // namespace sphaira::ui::menu::tools
