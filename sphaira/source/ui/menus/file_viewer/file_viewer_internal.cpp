#include "ui/menus/file_viewer/file_viewer_internal.hpp"
#include "path_util.hpp"
#include "ui/layout.hpp"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::fileview {

auto GetCurrentTimeMs() -> u64 {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

auto SplitLines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.emplace_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (lines.empty()) {
        lines.emplace_back();
    }
    return lines;
}

auto PathFileName(const fs::FsPath& path) -> std::string {
    const std::string_view view{path};
    const auto slash = view.find_last_of('/');
    if (slash == view.npos) {
        return std::string{view};
    }

    return std::string{view.substr(slash + 1)};
}

auto PathDirectory(const fs::FsPath& path) -> fs::FsPath {
    const std::string_view view{path};
    const auto slash = view.find_last_of('/');
    if (slash == view.npos || slash == 0) {
        return "/";
    }

    return std::string{view.substr(0, slash)};
}

auto IsJpegExtension(std::string_view ext) -> bool {
    return path::EqualsIC(ext, "jpg") || path::EqualsIC(ext, "jpeg");
}

auto IsImageExtension(std::string_view ext) -> bool {
    return IsJpegExtension(ext) || path::EqualsIC(ext, "png") || path::EqualsIC(ext, "bmp") || path::EqualsIC(ext, "gif");
}

auto ImageBounds(bool fullscreen) -> Vec4 {
    if (fullscreen) {
        return {0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT};
    }

    const auto band = layout::ContentBand();
    constexpr float pad_x = 30.f;
    constexpr float pad_y = 20.f;
    return {band.x + pad_x, band.y + pad_y, band.w - pad_x * 2.f, band.h - pad_y * 2.f};
}

std::vector<std::string> s_line_clipboard{};


} // namespace sphaira::ui::menu::fileview