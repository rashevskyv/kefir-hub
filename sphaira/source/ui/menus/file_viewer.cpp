#include "ui/menus/file_viewer.hpp"
#include "text_helper.hpp"
#include "path_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "minizip_helper.hpp"
#include "swkbd.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/theme_creator.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "web.hpp"

#include <minizip/zip.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <utility>

namespace sphaira::ui::menu::fileview {
namespace {

auto GetCurrentTimeMs() -> u64 {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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

static std::vector<std::string> s_line_clipboard{};

} // namespace

Menu::Menu(const fs::FsPath& path)
: MenuBase{path, MenuFlag_None}
, m_path{path}
, m_mode{TextMode::View}
, m_writable{false} {
    m_fs = &m_sd_fs;
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    LoadCurrentFile();
}

Menu::Menu(fs::Fs* fs, const fs::FsPath& path, TextMode mode, bool writable)
: MenuBase{path, MenuFlag_None}
, m_path{path}
, m_mode{mode}
, m_writable{writable} {
    m_fs = fs ? fs : &m_sd_fs;
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        if (m_mode == TextMode::Edit) {
            PromptTextExit();
        } else {
            SetPop();
        }
    }});

    LoadCurrentFile();
}

Menu::Menu(const fs::FsPath& path, std::vector<fs::FsPath> image_paths, s64 image_index, std::vector<std::string> image_titles)
: MenuBase{path, MenuFlag_None}
, m_path{path}
, m_image_paths{std::move(image_paths)}
, m_image_titles{std::move(image_titles)}
, m_image_index{image_index}
, m_mode{TextMode::View}
, m_writable{false} {
    m_fs = &m_sd_fs;
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    if (m_image_paths.empty()) {
        m_image_paths.emplace_back(path);
        m_image_titles.clear();
        m_image_index = 0;
    } else {
        const auto count = static_cast<s64>(m_image_paths.size());
        m_image_index = std::clamp(m_image_index, static_cast<s64>(0), count - 1);
        m_path = m_image_paths[m_image_index];
    }
    m_image_selected.resize(m_image_paths.size());

    LoadCurrentFile();
}

Menu::~Menu() {
    FreeImage();
}

void Menu::LoadCurrentFile() {
    FreeImage();
    m_scroll_text.reset();
    m_text_list.reset();
    m_file.Close();
    m_file_size = 0;
    m_file_offset = 0;
    m_load_result = 0;
    m_load_failed = false;
    m_is_streamed = false;
    m_page_cache.clear();
    m_page_offsets = {0};
    m_page_start_lines = {1};
    m_current_page = 0;
    m_stream_start_line = 1;
    m_zl_modifier_used = false;
    m_touch_was_pinch = false;
    m_is_image_file = IsImageExtension(path::Extension(m_path));

    if (!m_fs) {
        m_fs = &m_sd_fs;
    }

    if (m_is_image_file && m_image_paths.empty()) {
        m_image_paths.emplace_back(m_path);
        m_image_index = 0;
    }
    if (m_image_selected.size() != m_image_paths.size()) {
        m_image_selected.resize(m_image_paths.size());
    }

    SetTitle(GetDisplayName());
    SetSubHeading("");

    RemoveAction(Button::A);
    RemoveAction(Button::X);
    RemoveAction(Button::Y);
    RemoveAction(Button::L2);
    RemoveAction(Button::R2);
    RemoveAction(Button::L);
    RemoveAction(Button::R);
    RemoveAction(Button::LEFT);
    RemoveAction(Button::RIGHT);
    RemoveAction(Button::START);
    RemoveAction(Button::SELECT);
    RemoveAction(Button::R3);

    if (m_is_image_file) {
        SetShowStorage(false);
        LoadImageFile();
    } else {
        SetShowStorage(true);
        LoadTextFile();
    }
}

void Menu::RecreateList() {
    const float item_h = std::round(m_font_size * 1.6f);
    constexpr float pad_y = 2.f;
    m_viewport_rows = std::max<s64>(1, static_cast<s64>(530.f / (item_h + pad_y)));
    m_buffer_rows = m_viewport_rows * 2;

    const Vec4 list_pos{40.f, 100.f, 1200.f, 530.f};
    const Vec4 item_pos{50.f, 105.f, 1180.f, item_h};
    m_text_list = std::make_unique<List>(1, m_viewport_rows, list_pos, item_pos, Vec2{0.f, pad_y});
    if (m_has_range || m_selecting_range) {
        m_text_list->SetWrap(false);
    }
}

void Menu::LoadPage(s64 page_idx) {
    if (page_idx < 0) {
        page_idx = 0;
    }

    auto read_func = [this](int64_t off, char* buf, int64_t sz) -> int64_t {
        u64 bytes_read = 0;
        Result rc = m_file.Read(off, buf, sz, 0, &bytes_read);
        if (R_FAILED(rc)) {
            m_load_result = rc;
            m_load_failed = true;
            return 0;
        }
        if (bytes_read == 0 && sz > 0) {
            m_load_result = FsError_InvalidSize;
            m_load_failed = true;
            return 0;
        }
        return static_cast<int64_t>(bytes_read);
    };

    while (static_cast<s64>(m_page_offsets.size()) <= page_idx) {
        const s64 last_idx = static_cast<s64>(m_page_offsets.size()) - 1;
        const s64 last_offset = m_page_offsets[last_idx];
        const s64 last_line = m_page_start_lines[last_idx];
        const auto p = text_helper::ReadPage(read_func, m_file_size, last_offset, last_line, m_buffer_rows, m_viewport_rows);
        if (p.is_error || p.logical_end_offset <= last_offset) {
            if (!m_load_failed) {
                m_load_result = FsError_InvalidSize;
                m_load_failed = true;
            }
            break;
        }
        if (p.is_eof && p.lines.size() <= static_cast<size_t>(m_viewport_rows)) {
            page_idx = std::min<s64>(page_idx, last_idx);
            break;
        }
        m_page_offsets.push_back(p.logical_end_offset);
        m_page_start_lines.push_back(p.logical_end_line);
        if (p.is_eof) {
            page_idx = std::min<s64>(page_idx, static_cast<s64>(m_page_offsets.size()) - 1);
            break;
        }
    }

    if (m_load_failed) {
        return;
    }

    page_idx = std::clamp<s64>(page_idx, 0, static_cast<s64>(m_page_offsets.size()) - 1);
    m_current_page = page_idx;

    if (!m_page_cache.contains(page_idx)) {
        auto p = text_helper::ReadPage(read_func, m_file_size, m_page_offsets[page_idx], m_page_start_lines[page_idx], m_buffer_rows, m_viewport_rows);
        if (p.is_error) {
            if (!m_load_failed) {
                m_load_result = FsError_InvalidSize;
                m_load_failed = true;
            }
            return;
        }
        p.page_index = page_idx;
        m_page_cache[page_idx] = std::move(p);

        while (m_page_cache.size() > 4) {
            auto furthest = m_page_cache.begin();
            s64 max_dist = -1;
            for (auto it = m_page_cache.begin(); it != m_page_cache.end(); ++it) {
                const s64 dist = std::abs(it->first - m_current_page);
                if (dist > max_dist) {
                    max_dist = dist;
                    furthest = it;
                }
            }
            m_page_cache.erase(furthest);
        }
    }

    const auto& cur_page = m_page_cache[page_idx];
    m_lines = cur_page.lines;
    m_stream_start_line = cur_page.start_line;
}

void Menu::PreloadPages() {
    if (!m_is_streamed || m_load_failed) {
        return;
    }

    auto read_func = [this](int64_t off, char* buf, int64_t sz) -> int64_t {
        u64 bytes_read = 0;
        Result rc = m_file.Read(off, buf, sz, 0, &bytes_read);
        if (R_FAILED(rc)) {
            m_load_result = rc;
            m_load_failed = true;
            return 0;
        }
        if (bytes_read == 0 && sz > 0) {
            m_load_result = FsError_InvalidSize;
            m_load_failed = true;
            return 0;
        }
        return static_cast<int64_t>(bytes_read);
    };

    for (s64 step = 1; step <= 2; step++) {
        if (m_load_failed) {
            break;
        }

        const s64 next_page = m_current_page + step;
        if (m_page_cache.contains(next_page)) {
            continue;
        }

        const s64 prev_page = next_page - 1;
        if (m_page_cache.contains(prev_page) && m_page_cache[prev_page].is_eof) {
            break;
        }

        while (static_cast<s64>(m_page_offsets.size()) <= next_page) {
            const s64 last_idx = static_cast<s64>(m_page_offsets.size()) - 1;
            const s64 last_offset = m_page_offsets[last_idx];
            const s64 last_line = m_page_start_lines[last_idx];
            const auto p = text_helper::ReadPage(read_func, m_file_size, last_offset, last_line, m_buffer_rows, m_viewport_rows);
            if (p.is_error || p.logical_end_offset <= last_offset) {
                if (!m_load_failed) {
                    m_load_result = FsError_InvalidSize;
                    m_load_failed = true;
                }
                break;
            }
            if (p.is_eof && p.lines.size() <= static_cast<size_t>(m_viewport_rows)) {
                break;
            }
            m_page_offsets.push_back(p.logical_end_offset);
            m_page_start_lines.push_back(p.logical_end_line);
            if (p.is_eof) {
                break;
            }
        }

        if (m_load_failed) {
            break;
        }

        if (next_page < static_cast<s64>(m_page_offsets.size())) {
            auto p = text_helper::ReadPage(read_func, m_file_size, m_page_offsets[next_page], m_page_start_lines[next_page], m_buffer_rows, m_viewport_rows);
            if (p.is_error) {
                if (!m_load_failed) {
                    m_load_result = FsError_InvalidSize;
                    m_load_failed = true;
                }
                break;
            }
            p.page_index = next_page;
            m_page_cache[next_page] = std::move(p);
            while (m_page_cache.size() > 4) {
                auto furthest = m_page_cache.begin();
                s64 max_dist = -1;
                for (auto it = m_page_cache.begin(); it != m_page_cache.end(); ++it) {
                    const s64 dist = std::abs(it->first - m_current_page);
                    if (dist > max_dist) {
                        max_dist = dist;
                        furthest = it;
                    }
                }
                m_page_cache.erase(furthest);
            }
        }
    }
}

void Menu::PageUp(s64 count) {
    if (m_is_streamed) {
        if (m_current_page > 0) {
            LoadPage(std::max<s64>(0, m_current_page - count));
            if (m_text_list) {
                m_text_list->SetYoff(0.f);
            }
            m_line_index = 0;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        } else if (m_text_list && m_text_list->GetYoff() > 0.f) {
            m_text_list->SetYoff(0.f);
            m_line_index = 0;
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    } else {
        if (m_text_list) {
            const float step = m_text_list->GetMaxY();
            const float page_shift = static_cast<float>(count * m_viewport_rows) * step;
            const float next_y = std::max(0.f, m_text_list->GetYoff() - page_shift);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::PageDown(s64 count) {
    if (m_is_streamed) {
        if (!m_page_cache[m_current_page].is_eof) {
            LoadPage(m_current_page + count);
            if (m_text_list) {
                m_text_list->SetYoff(0.f);
            }
            m_line_index = 0;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    } else {
        if (m_text_list) {
            const s64 total = static_cast<s64>(m_lines.size());
            const float step = m_text_list->GetMaxY();
            const float y_max = (total > m_viewport_rows) ? static_cast<float>(total - m_viewport_rows) * step : 0.f;
            const float page_shift = static_cast<float>(count * m_viewport_rows) * step;
            const float next_y = std::min(y_max, m_text_list->GetYoff() + page_shift);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::LineUp() {
    const float step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    if (m_is_streamed) {
        const s64 cur_row = (step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / step)) : 0;
        if (cur_row > 0) {
            const float next_y = std::max(0.f, (cur_row - 1) * step);
            if (m_text_list) {
                m_text_list->SetYoff(next_y);
            }
            m_line_index = cur_row - 1;
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        } else if (m_current_page > 0) {
            LoadPage(m_current_page - 1);
            const s64 target_row = std::max<s64>(0, m_viewport_rows - 1);
            if (m_text_list) {
                m_text_list->SetYoff(target_row * step);
            }
            m_line_index = target_row;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    } else {
        if (m_text_list) {
            const s64 count = static_cast<s64>(m_lines.size());
            const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * step : 0.f;
            const float next_y = std::clamp(m_text_list->GetYoff() - step, 0.f, y_max);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::LineDown() {
    const float step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    if (m_is_streamed) {
        const s64 cur_row = (step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / step)) : 0;
        const s64 count = static_cast<s64>(m_lines.size());
        const bool at_eof = m_page_cache[m_current_page].is_eof;

        if (cur_row + 1 < m_viewport_rows) {
            if (cur_row + 1 + m_viewport_rows <= count || (at_eof && cur_row + 1 < count)) {
                const float next_y = (cur_row + 1) * step;
                if (m_text_list) {
                    m_text_list->SetYoff(next_y);
                }
                m_line_index = cur_row + 1;
                App::PlaySoundEffect(SoundEffect_Scroll);
                UpdateTextSubHeading();
            }
        } else if (!at_eof) {
            LoadPage(m_current_page + 1);
            if (m_text_list) {
                m_text_list->SetYoff(0.f);
            }
            m_line_index = 0;
            PreloadPages();
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        } else {
            const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * step : 0.f;
            if (m_text_list->GetYoff() + step <= y_max + 1.f) {
                const float next_y = std::min(m_text_list->GetYoff() + step, y_max);
                m_text_list->SetYoff(next_y);
                m_line_index = static_cast<s64>(std::round(next_y / step));
                App::PlaySoundEffect(SoundEffect_Scroll);
                UpdateTextSubHeading();
            }
        }
    } else {
        if (m_text_list) {
            const s64 count = static_cast<s64>(m_lines.size());
            const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * step : 0.f;
            const float next_y = std::clamp(m_text_list->GetYoff() + step, 0.f, y_max);
            m_text_list->SetYoff(next_y);
            m_line_index = static_cast<s64>(std::round(next_y / step));
            App::PlaySoundEffect(SoundEffect_Scroll);
            UpdateTextSubHeading();
        }
    }
}

void Menu::ZoomText(float delta) {
    const float new_size = std::clamp(m_font_size + delta, 12.f, 32.f);
    if (std::abs(new_size - m_font_size) < 0.1f) {
        return;
    }

    const float old_step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    const s64 old_visible_row = (old_step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / old_step)) : 0;

    m_font_size = new_size;
    RecreateList();

    if (m_is_streamed) {
        s64 current_offset = 0;
        if (m_current_page < static_cast<s64>(m_page_offsets.size())) {
            current_offset = m_page_offsets[m_current_page];
        }
        s64 current_line = 1;
        if (m_current_page < static_cast<s64>(m_page_start_lines.size())) {
            current_line = m_page_start_lines[m_current_page];
        }
        m_page_offsets = {current_offset};
        m_page_start_lines = {current_line};
        m_page_cache.clear();
        m_current_page = 0;
        LoadPage(0);
        if (m_text_list) {
            m_text_list->SetYoff(0.f);
        }
        m_line_index = 0;
        PreloadPages();
    } else {
        const float new_step = m_text_list ? m_text_list->GetMaxY() : 30.f;
        const s64 count = static_cast<s64>(m_lines.size());
        const float y_max = (count > m_viewport_rows) ? static_cast<float>(count - m_viewport_rows) * new_step : 0.f;
        const float next_y = std::clamp(static_cast<float>(old_visible_row) * new_step, 0.f, y_max);
        if (m_text_list) {
            m_text_list->SetYoff(next_y);
        }
        m_line_index = static_cast<s64>(std::round(next_y / new_step));
    }

    UpdateTextSubHeading();
}

void Menu::LoadTextFile() {
    m_lines.clear();
    m_undo.clear();
    m_redo.clear();
    m_saved_text.clear();
    m_text_dirty = false;
    m_line_index = 0;
    m_load_failed = false;
    m_load_result = 0;
    m_is_streamed = false;
    m_font_size = 18.f;
    m_zl_modifier_used = false;
    m_touch_was_pinch = false;

    if (!m_fs) {
        m_fs = &m_sd_fs;
    }

    Result rc = m_fs->OpenFile(m_path, FsOpenMode_Read, &m_file);
    if (R_FAILED(rc)) {
        m_load_result = rc;
        m_load_failed = true;
        return;
    }

    rc = m_file.GetSize(&m_file_size);
    if (R_FAILED(rc)) {
        m_file.Close();
        m_load_result = rc;
        m_load_failed = true;
        return;
    }

    if (m_file_size > EDIT_MAX_SIZE) {
        m_is_streamed = true;
        m_mode = TextMode::View;
        m_editable = false;
        m_current_page = 0;
        m_page_offsets = {0};
        m_page_start_lines = {1};
        m_page_cache.clear();

        RecreateList();
        LoadPage(0);
        PreloadPages();
        SetupViewActions();
        UpdateTextSubHeading();
        return;
    }

    const s64 read_size = m_file_size;
    std::string buf;
    buf.resize(read_size);

    u64 bytes_read = 0;
    if (read_size > 0) {
        rc = m_file.Read(0, buf.data(), read_size, 0, &bytes_read);
        if (R_FAILED(rc)) {
            m_file.Close();
            m_load_result = rc;
            m_load_failed = true;
            return;
        }

        if (bytes_read != static_cast<u64>(read_size)) {
            m_file.Close();
            m_load_result = FsError_InvalidSize;
            m_load_failed = true;
            return;
        }
    }
    m_file.Close();

    buf.resize(bytes_read);

    if (m_mode == TextMode::Edit && !m_writable) {
        m_mode = TextMode::View;
    }
    m_editable = (m_mode == TextMode::Edit);

    std::string_view view{buf};
    m_line_break = (view.find("\r\n") != std::string_view::npos) ? "\r\n" : "\n";

    size_t start = 0;
    while (start <= view.size()) {
        const auto end = view.find('\n', start);
        auto line = view.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        m_lines.emplace_back(line);

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    if (m_lines.empty()) {
        m_lines.emplace_back();
    }

    m_saved_text = BuildText();
    m_text_dirty = false;
    m_line_index = 0;

    RecreateList();

    if (m_editable) {
        SetupEditActions();
    } else {
        SetupViewActions();
    }

    UpdateTextSubHeading();
}

void Menu::SetupViewActions() {
    RemoveAction(Button::A);
    RemoveAction(Button::X);
    RemoveAction(Button::Y);
    RemoveAction(Button::START);
    RemoveAction(Button::L2);
    RemoveAction(Button::R2);
    RemoveAction(Button::L);
    RemoveAction(Button::R);
    RemoveAction(Button::SELECT);
    RemoveAction(Button::R3);

    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    SetAction(Button::L, Action{ActionType::UP, "Page"_i18n, "\uE0E4 / \uE0E5", [](){}});
    SetAction(Button::L2, Action{ActionType::UP, "10 Pages"_i18n, "\uE0E6 / \uE0E7", [](){}});
    SetAction(Button::SELECT, Action{ActionType::UP, "Zoom"_i18n, "\uE0E4 + \uE102", [](){}});
    SetAction(Button::R3, Action{ActionType::UP, "Scroll"_i18n, "\uE101 / \uE102", [](){}});

    if (m_writable && !m_is_streamed && m_file_size <= EDIT_MAX_SIZE) {
        SetAction(Button::A, Action{"Edit"_i18n, [this](){
            SwitchToEditMode();
        }});
    }
}

void Menu::SetupEditActions() {
    RemoveAction(Button::R2);
    RemoveAction(Button::L);
    RemoveAction(Button::R);
    RemoveAction(Button::SELECT);
    RemoveAction(Button::R3);

    if (m_selecting_range) {
        SetAction(Button::A, Action{"Finish selection"_i18n, [this](){
            FinishRangeSelection();
        }});
        SetAction(Button::B, Action{"Cancel"_i18n, [this](){
            CancelRangeSelection();
        }});
    } else {
        SetAction(Button::A, Action{"Edit line"_i18n, [this](){
            EditLine();
        }});
        SetAction(Button::B, Action{"Back"_i18n, [this](){
            SwitchToViewMode();
        }});
    }

    SetAction(Button::X, Action{"Actions"_i18n, [this](){
        ShowLineActions();
    }});
    SetAction(Button::START, Action{"Options"_i18n, [this](){
        DisplayTextOptions();
    }});
    SetAction(Button::L2, Action{"Cursor / Scroll"_i18n, "\uE101 / \uE102", [](){}});
}

void Menu::SwitchToEditMode() {
    if (!m_writable || m_is_streamed || m_file_size > EDIT_MAX_SIZE) {
        return;
    }

    m_mode = TextMode::Edit;
    m_editable = true;
    m_held_down_at_bottom = false;
    m_held_up_at_top = false;
    m_selecting_range = false;
    m_has_range = false;

    if (m_text_list) {
        m_text_list->SetWrap(true);
        const float item_h = m_text_list->GetMaxY();
        const s64 first_visible = (item_h > 0.f) ? static_cast<s64>(m_text_list->GetYoff() / item_h) : 0;
        const s64 total = static_cast<s64>(m_lines.size());
        m_line_index = std::clamp<s64>(first_visible, 0, total > 0 ? total - 1 : 0);
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }

    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::SwitchToViewMode() {
    m_mode = TextMode::View;
    m_editable = false;
    m_held_down_at_bottom = false;
    m_held_up_at_top = false;
    m_selecting_range = false;
    m_has_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(true);
    }

    SetupViewActions();
    UpdateTextSubHeading();
}

void Menu::UpdateTextSubHeading() {
    const float step = m_text_list ? m_text_list->GetMaxY() : 30.f;
    const s64 cur_row = (step > 0.f && m_text_list) ? static_cast<s64>(std::round(m_text_list->GetYoff() / step)) : 0;

    if (m_is_streamed) {
        auto heading = "Page "_i18n + std::to_string(m_current_page + 1);
        heading += "  |  " + "Line "_i18n + std::to_string(m_stream_start_line + cur_row);
        if (m_file_size > 0 && m_current_page < static_cast<s64>(m_page_offsets.size())) {
            const s64 offset = m_page_offsets[m_current_page];
            const s64 pct = std::clamp<s64>((offset * 100) / m_file_size, 0, 100);
            heading += "  |  " + std::to_string(pct) + "%";
        }
        if (!m_writable) {
            heading += "  (" + "Read-only"_i18n + ")";
        } else {
            heading += "  (" + "View"_i18n + ")";
        }
        SetSubHeading(heading);
        return;
    }

    const s64 display_line = m_editable ? (m_line_index + 1) : (cur_row + 1);
    auto heading = std::to_string(display_line) + " / " + std::to_string(m_lines.size());
    if (m_mode == TextMode::View) {
        if (!m_writable) {
            heading += "  (" + "Read-only"_i18n + ")";
        } else {
            heading += "  (" + "View"_i18n + ")";
        }
    }
    if (m_text_dirty) {
        heading += "  *";
    }
    SetSubHeading(heading);
}

auto Menu::BuildText() const -> std::string {
    std::string out;
    for (size_t i = 0; i < m_lines.size(); i++) {
        out += m_lines[i];
        if (i + 1 < m_lines.size()) {
            out += m_line_break;
        }
    }
    return out;
}

void Menu::PushUndo() {
    constexpr size_t MAX_UNDO = 32;

    m_undo.emplace_back(m_lines);
    if (m_undo.size() > MAX_UNDO) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
}

void Menu::Undo() {
    if (m_undo.empty()) {
        App::Notify("Nothing to undo"_i18n);
        return;
    }

    m_redo.emplace_back(m_lines);
    m_lines = m_undo.back();
    m_undo.pop_back();
    m_text_dirty = (BuildText() != m_saved_text);
    m_line_index = std::min<s64>(m_line_index, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    UpdateTextSubHeading();
}
void Menu::Redo() {
    if (m_redo.empty()) {
        App::Notify("Nothing to redo"_i18n);
        return;
    }

    m_undo.emplace_back(m_lines);
    m_lines = m_redo.back();
    m_redo.pop_back();
    m_text_dirty = (BuildText() != m_saved_text);
    m_line_index = std::min<s64>(m_line_index, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    UpdateTextSubHeading();
}

void Menu::EditLine() {
    if (!m_editable) return;
    std::string out;
    if (R_FAILED(swkbd::ShowText(out, "Edit line"_i18n.c_str(), m_lines[m_line_index].c_str(), 0, 1024))) {
        return;
    }

    if (out == m_lines[m_line_index]) {
        return;
    }

    PushUndo();
    m_lines[m_line_index] = out;
    ClearRangeSelection();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::InsertLine() {
    if (!m_editable) return;
    PushUndo();
    m_lines.insert(m_lines.begin() + m_line_index + 1, "");
    m_line_index++;
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::DeleteLine() {
    if (!m_editable) return;
    const auto [start, end] = GetTargetRange();
    PushUndo();
    m_lines.erase(m_lines.begin() + start, m_lines.begin() + end + 1);
    if (m_lines.empty()) {
        m_lines.emplace_back();
    }
    m_line_index = std::clamp<s64>(start, 0, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::JoinLine() {
    if (!m_editable) return;
    if (m_line_index + 1 >= static_cast<s64>(m_lines.size())) {
        App::Notify("No line below to join"_i18n);
        return;
    }

    PushUndo();
    m_lines[m_line_index] += m_lines[m_line_index + 1];
    m_lines.erase(m_lines.begin() + m_line_index + 1);
    ClearRangeSelection();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::GoToLine() {
    s64 out = m_line_index + 1;
    if (R_FAILED(swkbd::ShowNumPad(out, "Go to line"_i18n.c_str(), std::to_string(out).c_str(), 1, 9))) {
        return;
    }

    const s64 clamped = std::clamp<s64>(out, 1, m_lines.size());
    m_line_index = clamped - 1;
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    UpdateTextSubHeading();
}

void Menu::StartRangeSelection() {
    if (!m_editable) return;
    m_range_anchor = m_line_index;
    m_selecting_range = true;
    m_has_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(false);
    }
    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::FinishRangeSelection() {
    if (!m_selecting_range) return;
    m_range_start = std::min(m_range_anchor, m_line_index);
    m_range_end = std::max(m_range_anchor, m_line_index);
    m_has_range = true;
    m_selecting_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(false);
    }
    SetupEditActions();
    App::PlaySoundEffect(SoundEffect_Focus);
    UpdateTextSubHeading();
}

void Menu::CancelRangeSelection() {
    if (!m_selecting_range) return;
    m_selecting_range = false;
    m_has_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(true);
    }
    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::ClearRangeSelection() {
    m_selecting_range = false;
    m_has_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(true);
    }
    SetupEditActions();
}

auto Menu::HasSelection() const -> bool {
    return m_editable && (m_has_range || m_selecting_range);
}

auto Menu::GetTargetRange() const -> std::pair<s64, s64> {
    if (m_lines.empty()) {
        return {0, 0};
    }
    if (m_has_range) {
        const s64 start = std::clamp<s64>(m_range_start, 0, m_lines.size() - 1);
        const s64 end = std::clamp<s64>(m_range_end, start, m_lines.size() - 1);
        return {start, end};
    }
    if (m_selecting_range) {
        const s64 start = std::clamp<s64>(std::min(m_range_anchor, m_line_index), 0, m_lines.size() - 1);
        const s64 end = std::clamp<s64>(std::max(m_range_anchor, m_line_index), start, m_lines.size() - 1);
        return {start, end};
    }
    const s64 cur = std::clamp<s64>(m_line_index, 0, m_lines.size() - 1);
    return {cur, cur};
}

void Menu::CopySelection() {
    if (!m_editable) return;
    const auto [start, end] = GetTargetRange();
    s_line_clipboard.clear();
    for (s64 i = start; i <= end && i < static_cast<s64>(m_lines.size()); i++) {
        s_line_clipboard.push_back(m_lines[i]);
    }
    App::PlaySoundEffect(SoundEffect_Focus);
}

void Menu::CutSelection() {
    if (!m_editable) return;
    const auto [start, end] = GetTargetRange();
    s_line_clipboard.clear();
    for (s64 i = start; i <= end && i < static_cast<s64>(m_lines.size()); i++) {
        s_line_clipboard.push_back(m_lines[i]);
    }
    PushUndo();
    m_lines.erase(m_lines.begin() + start, m_lines.begin() + end + 1);
    if (m_lines.empty()) {
        m_lines.emplace_back();
    }
    m_line_index = std::clamp<s64>(start, 0, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::PasteBelow() {
    if (!m_editable) return;
    if (s_line_clipboard.empty()) {
        App::Notify("Clipboard is empty"_i18n);
        return;
    }
    PushUndo();
    const auto [start, end] = GetTargetRange();
    const s64 insert_pos = std::clamp<s64>(end + 1, 0, m_lines.size());
    m_lines.insert(m_lines.begin() + insert_pos, s_line_clipboard.begin(), s_line_clipboard.end());
    m_line_index = std::clamp<s64>(insert_pos + static_cast<s64>(s_line_clipboard.size()) - 1, 0, m_lines.size() - 1);
    ClearRangeSelection();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::CommentSelection() {
    if (!m_editable) return;
    const auto [start, end] = GetTargetRange();
    bool changed = false;
    for (s64 i = start; i <= end && i < static_cast<s64>(m_lines.size()); i++) {
        auto commented = text_helper::CommentIniLine(m_lines[i]);
        if (commented != m_lines[i]) {
            if (!changed) {
                PushUndo();
                changed = true;
            }
            m_lines[i] = std::move(commented);
        }
    }
    if (changed) {
        ClearRangeSelection();
        m_text_dirty = (BuildText() != m_saved_text);
        m_line_scroll.Reset();
        UpdateTextSubHeading();
    }
}

void Menu::UncommentSelection() {
    if (!m_editable) return;
    const auto [start, end] = GetTargetRange();
    bool changed = false;
    for (s64 i = start; i <= end && i < static_cast<s64>(m_lines.size()); i++) {
        auto uncommented = text_helper::UncommentIniLine(m_lines[i]);
        if (uncommented != m_lines[i]) {
            if (!changed) {
                PushUndo();
                changed = true;
            }
            m_lines[i] = std::move(uncommented);
        }
    }
    if (changed) {
        ClearRangeSelection();
        m_text_dirty = (BuildText() != m_saved_text);
        m_line_scroll.Reset();
        UpdateTextSubHeading();
    }
}

auto Menu::SaveText() -> bool {
    if (!m_editable || m_is_streamed || !m_fs) {
        return false;
    }

    const auto text = BuildText();
    const std::vector<u8> data{text.begin(), text.end()};

    fs::FsPath tmp_path{};
    fs::FsPath bak_path{};

    if (std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.editor", m_path.s) >= static_cast<int>(sizeof(tmp_path)) ||
        std::snprintf(bak_path, sizeof(bak_path), "%s.bak.editor", m_path.s) >= static_cast<int>(sizeof(bak_path))) {
        App::PushErrorBox(FsError_TooLongPath, "Path too long for temporary save files"_i18n);
        return false;
    }

    if (m_fs->FileExists(m_path)) {
        if (m_fs->FileExists(tmp_path)) {
            m_fs->DeleteFile(tmp_path);
        }
        if (m_fs->FileExists(bak_path)) {
            m_fs->DeleteFile(bak_path);
        }
    }

    Result primary_rc = m_fs->write_entire_file(tmp_path, data);
    if (R_FAILED(primary_rc)) {
        log_write("[SaveText] write_entire_file failed for %s: 0x%x\n", tmp_path.s, primary_rc);
        if (m_fs->FileExists(tmp_path)) {
            m_fs->DeleteFile(tmp_path);
        }
        App::PushErrorBox(primary_rc, "Failed to write temporary file"_i18n);
        return false;
    }

    bool renamed_orig = false;
    if (m_fs->FileExists(m_path)) {
        primary_rc = m_fs->RenameFile(m_path, bak_path);
        if (R_FAILED(primary_rc)) {
            log_write("[SaveText] Rename original -> backup failed for %s: 0x%x\n", m_path.s, primary_rc);
            if (m_fs->FileExists(tmp_path)) {
                m_fs->DeleteFile(tmp_path);
            }
            App::PushErrorBox(primary_rc, "Failed to create backup file"_i18n);
            return false;
        }
        renamed_orig = true;
    }

    primary_rc = m_fs->RenameFile(tmp_path, m_path);
    if (R_FAILED(primary_rc)) {
        log_write("[SaveText] Rename tmp -> original failed for %s: 0x%x\n", m_path.s, primary_rc);

        if (renamed_orig) {
            Result rollback_rc = m_fs->RenameFile(bak_path, m_path);
            if (R_FAILED(rollback_rc)) {
                log_write("[SaveText] CRITICAL: Rollback backup -> original failed for %s: 0x%x. Preserving %s and %s\n",
                          m_path.s, rollback_rc, tmp_path.s, bak_path.s);
                App::PushErrorBox(rollback_rc, "Failed to restore original file from backup during save recovery. Preserved temporary and backup files."_i18n);
                return false;
            }
        }

        if (m_fs->FileExists(tmp_path)) {
            m_fs->DeleteFile(tmp_path);
        }
        App::PushErrorBox(primary_rc, "Failed to update original file"_i18n);
        return false;
    }

    if (!m_fs->FileExists(m_path)) {
        log_write("[SaveText] Original file missing after rename: %s\n", m_path.s);
        App::PushErrorBox(FsError_FileNotFound, "Saved file is missing after update"_i18n);
        return false;
    }

    if (renamed_orig && m_fs->FileExists(bak_path)) {
        Result del_rc = m_fs->DeleteFile(bak_path);
        if (R_FAILED(del_rc)) {
            log_write("[SaveText] Warning: failed to remove backup file %s: 0x%x\n", bak_path.s, del_rc);
        }
    }

    m_saved_text = text;
    m_file_size = static_cast<s64>(data.size());
    m_undo.clear();
    m_redo.clear();
    m_text_dirty = false;
    App::Notify("Saved"_i18n);
    UpdateTextSubHeading();
    return true;
}

void Menu::PromptTextExit() {
    if (!m_editable || !m_text_dirty) {
        SetPop();
        return;
    }

    PopupList::Items items;
    items.emplace_back("Save"_i18n);
    items.emplace_back("Discard"_i18n);
    items.emplace_back("Cancel"_i18n);

    App::Push<PopupList>("Unsaved changes"_i18n, items, [this](auto op_index){
        if (!op_index || *op_index == 2) {
            return;
        }

        if (*op_index == 0) {
            if (SaveText()) {
                SetPop();
            }
        } else if (*op_index == 1) {
            SetPop();
        }
    });
}

void Menu::ShowLineActions() {
    struct ActionEntry {
        std::string title;
        std::optional<ActionIcon> icon;
        std::function<void()> callback;
    };
    std::vector<ActionEntry> actions;

    actions.push_back({"Edit line"_i18n, ActionIcon::Edit, [this](){ EditLine(); }});

    if (m_has_range) {
        actions.push_back({"Clear selection"_i18n, std::nullopt, [this](){ ClearRangeSelection(); UpdateTextSubHeading(); }});
    } else {
        actions.push_back({"Select range"_i18n, std::nullopt, [this](){ StartRangeSelection(); }});
    }

    actions.push_back({"Copy"_i18n, ActionIcon::Copy, [this](){ CopySelection(); }});
    actions.push_back({"Cut"_i18n, ActionIcon::Cut, [this](){ CutSelection(); }});
    actions.push_back({"Paste below"_i18n, ActionIcon::Paste, [this](){ PasteBelow(); }});
    actions.push_back({"Delete"_i18n, ActionIcon::Delete, [this](){ DeleteLine(); }});
    actions.push_back({"Insert line below"_i18n, ActionIcon::Insert, [this](){ InsertLine(); }});
    actions.push_back({"Join with next line"_i18n, ActionIcon::Join, [this](){ JoinLine(); }});

    if (text_helper::IsIniFile(m_path)) {
        actions.push_back({"Comment"_i18n, std::nullopt, [this](){ CommentSelection(); }});
        actions.push_back({"Uncomment"_i18n, std::nullopt, [this](){ UncommentSelection(); }});
    }

    actions.push_back({"Undo"_i18n, ActionIcon::Undo, [this](){ Undo(); }});
    actions.push_back({"Redo"_i18n, ActionIcon::Redo, [this](){ Redo(); }});

    PopupList::Items items;
    std::vector<std::optional<ActionIcon>> icons;
    items.reserve(actions.size());
    icons.reserve(actions.size());
    for (const auto& a : actions) {
        items.push_back(a.title);
        icons.push_back(a.icon);
    }

    std::string title;
    if (m_has_range) {
        title = "Lines "_i18n + std::to_string(m_range_start + 1) + " - " + std::to_string(m_range_end + 1);
    } else if (m_selecting_range) {
        const auto [start, end] = GetTargetRange();
        title = "Lines "_i18n + std::to_string(start + 1) + " - " + std::to_string(end + 1);
    } else {
        title = "Line "_i18n + std::to_string(m_line_index + 1);
    }

    auto popup = std::make_unique<PopupList>(title, items, [actions = std::move(actions)](auto op_index){
        if (!op_index || *op_index >= actions.size()) {
            return;
        }
        actions[*op_index].callback();
    });
    popup->SetIcons(std::move(icons));
    App::Push(std::move(popup));
}

void Menu::DisplayTextOptions() {
    auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>("Save"_i18n, [this](){
        SaveText();
    }, "Save changes to the file."_i18n);

    options->Add<SidebarEntryCallback>("Undo"_i18n, [this](){
        Undo();
    }, "Step back through the last 32 edits."_i18n);

    options->Add<SidebarEntryCallback>("Redo"_i18n, [this](){
        Redo();
    }, "Step forward again after an undo."_i18n);

    options->Add<SidebarEntryCallback>("Go to line"_i18n, [this](){
        GoToLine();
    }, "Jump straight to a line number."_i18n);
}

void Menu::UpdateText(Controller* controller, TouchInfo* touch) {
    if (!m_text_list) {
        return;
    }

    if (!m_editable) {
        if (touch->is_touching && touch->is_tap) {
            m_touch_was_pinch = false;
        }

        if (touch->is_pinch) {
            m_touch_was_pinch = true;
            if (std::abs(touch->pinch_delta) > 1.5f) {
                ZoomText(touch->pinch_delta > 0.f ? 0.5f : -0.5f);
            }
        } else if (m_is_streamed) {
            if (touch->is_end) {
                if (!m_touch_was_pinch) {
                    const s32 dy = static_cast<s32>(touch->cur.y) - static_cast<s32>(touch->initial.y);
                    constexpr s32 SWIPE_THRESHOLD = 40;
                    if (dy < -SWIPE_THRESHOLD) {
                        PageDown(1);
                    } else if (dy > SWIPE_THRESHOLD) {
                        PageUp(1);
                    }
                }
                m_touch_was_pinch = false;
            }
        } else {
            m_text_list->OnUpdateTouchOnly(touch, m_lines.size());
        }

        const auto zl_held = controller->GotHeld(Button::L2);
        if (zl_held) {
            const auto zoom_in = controller->GotDown(Button::UP | Button::DPAD_UP | Button::LS_UP | Button::RS_UP);
            const auto zoom_out = controller->GotDown(Button::DOWN | Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN);
            if (zoom_in) {
                m_zl_modifier_used = true;
                ZoomText(1.f);
            } else if (zoom_out) {
                m_zl_modifier_used = true;
                ZoomText(-1.f);
            }
        }

        if (controller->GotUp(Button::L)) {
            PageUp(1);
        }

        if (controller->GotUp(Button::R)) {
            PageDown(1);
        }

        if (controller->GotUp(Button::L2)) {
            if (!m_zl_modifier_used) {
                PageUp(10);
            }
            m_zl_modifier_used = false;
        }

        if (controller->GotUp(Button::R2)) {
            PageDown(10);
        }

        if (!zl_held) {
            const bool up_pressed = controller->GotDown(Button::UP | Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                                    controller->GotHeld(Button::UP | Button::DPAD_UP | Button::LS_UP | Button::RS_UP);

            const bool down_pressed = controller->GotDown(Button::DOWN | Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                                      controller->GotHeld(Button::DOWN | Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN);

            if (up_pressed) {
                LineUp();
            } else if (down_pressed) {
                LineDown();
            }
        }
        return;
    }

    const s64 count = static_cast<s64>(m_lines.size());
    const s64 page = m_text_list->GetPage();
    const float step = m_text_list->GetMaxY();
    const float y_max = (count > page) ? static_cast<float>(count - page) * step : 0.f;

    if (controller->GotDown(Button::RS_UP) || controller->GotHeld(Button::RS_UP)) {
        const float next_y = std::clamp(m_text_list->GetYoff() - step, 0.f, y_max);
        m_text_list->SetYoff(next_y);
    } else if (controller->GotDown(Button::RS_DOWN) || controller->GotHeld(Button::RS_DOWN)) {
        const float next_y = std::clamp(m_text_list->GetYoff() + step, 0.f, y_max);
        m_text_list->SetYoff(next_y);
    }

    const u64 down_mask = static_cast<u64>(Button::DOWN) | static_cast<u64>(Button::DPAD_DOWN) | static_cast<u64>(Button::LS_DOWN);
    const u64 up_mask = static_cast<u64>(Button::UP) | static_cast<u64>(Button::DPAD_UP) | static_cast<u64>(Button::LS_UP);

    if (controller->GotUp(Button::DOWN) || !controller->GotHeld(Button::DOWN)) {
        m_held_down_at_bottom = false;
    }
    if (controller->GotUp(Button::UP) || !controller->GotHeld(Button::UP)) {
        m_held_up_at_top = false;
    }

    Controller local_ctrl = *controller;
    const u64 rs_mask = static_cast<u64>(Button::RS_UP) | static_cast<u64>(Button::RS_DOWN) | static_cast<u64>(Button::RS_LEFT) | static_cast<u64>(Button::RS_RIGHT);
    local_ctrl.m_kdown &= ~rs_mask;
    local_ctrl.m_kheld &= ~rs_mask;
    local_ctrl.m_kup &= ~rs_mask;

    if (count > 0) {
        if (m_has_range) {
            if (m_range_end >= count - 1) {
                local_ctrl.m_kdown &= ~down_mask;
                local_ctrl.m_kheld &= ~down_mask;
            }
            if (m_range_start <= 0) {
                local_ctrl.m_kdown &= ~up_mask;
                local_ctrl.m_kheld &= ~up_mask;
            }
        } else if (m_selecting_range) {
            if (m_line_index >= count - 1) {
                local_ctrl.m_kdown &= ~down_mask;
                local_ctrl.m_kheld &= ~down_mask;
            }
            if (m_line_index <= 0) {
                local_ctrl.m_kdown &= ~up_mask;
                local_ctrl.m_kheld &= ~up_mask;
            }
        } else {
            const bool at_bottom = (m_line_index == count - 1);
            const bool at_top = (m_line_index == 0);
            if (at_bottom && m_held_down_at_bottom) {
                local_ctrl.m_kdown &= ~down_mask;
            }
            if (at_top && m_held_up_at_top) {
                local_ctrl.m_kdown &= ~up_mask;
            }
        }
    }

    m_text_list->OnUpdate(&local_ctrl, touch, m_line_index, m_lines.size(), [this, count, controller](bool touched, s64 index){
        if (touched) {
            m_held_down_at_bottom = false;
            m_held_up_at_top = false;
            if (index != m_line_index) {
                m_line_index = index;
                m_last_tapped_row = index;
                m_last_tap_time = GetCurrentTimeMs();
                m_line_scroll.Reset();
                UpdateTextSubHeading();
                App::PlaySoundEffect(SoundEffect_Focus);
            } else {
                const auto now = GetCurrentTimeMs();
                if (m_last_tapped_row == index && (now - m_last_tap_time) <= 500) {
                    m_last_tap_time = 0;
                    if (m_selecting_range) {
                        FinishRangeSelection();
                        return;
                    }
                    if (text_helper::IsIniFile(m_path)) {
                        const auto toggle = text_helper::ToggleIniBoolean(m_lines[index]);
                        if (toggle.toggled) {
                            PushUndo();
                            m_lines[index] = toggle.new_line;
                            ClearRangeSelection();
                            m_text_dirty = (BuildText() != m_saved_text);
                            UpdateTextSubHeading();
                            App::PlaySoundEffect(SoundEffect_Focus);
                            return;
                        }
                    }
                    EditLine();
                } else {
                    m_last_tapped_row = index;
                    m_last_tap_time = now;
                }
            }
        } else {
            const s64 delta = index - m_line_index;
            if (m_has_range) {
                if (m_range_start + delta >= 0 && m_range_end + delta < count) {
                    m_range_start += delta;
                    m_range_end += delta;
                    m_line_index = index;
                    if (m_text_list) {
                        m_text_list->EnsureVisible(delta > 0 ? m_range_end : m_range_start, count);
                    }
                    App::PlaySoundEffect(SoundEffect_Focus);
                    m_line_scroll.Reset();
                    UpdateTextSubHeading();
                } else {
                    if (m_text_list) {
                        m_text_list->EnsureVisible(m_line_index, count);
                    }
                }
            } else {
                m_line_index = index;
                const bool at_bottom = (m_line_index == count - 1);
                const bool at_top = (m_line_index == 0);
                if (at_bottom && controller->GotHeld(Button::DOWN)) {
                    m_held_down_at_bottom = true;
                }
                if (at_top && controller->GotHeld(Button::UP)) {
                    m_held_up_at_top = true;
                }
                App::PlaySoundEffect(SoundEffect_Focus);
                m_line_scroll.Reset();
                UpdateTextSubHeading();
            }
        }
    });
}

void Menu::DrawText(NVGcontext* vg, Theme* theme) {
    if (!m_text_list) {
        return;
    }

    const auto gutter = m_is_streamed
        ? std::to_string(m_stream_start_line + static_cast<s64>(m_lines.size()))
        : std::to_string(m_lines.size());
    float bounds[4];
    nvgFontSize(vg, m_font_size);
    gfx::textBounds(vg, 0, 0, bounds, gutter.c_str());
    const float gutter_w = bounds[2] - bounds[0] + 16.f;
    const bool is_ini = text_helper::IsIniFile(m_path);
    const bool has_sel = HasSelection();
    const auto [sel_start, sel_end] = GetTargetRange();

    m_text_list->Draw(vg, theme, m_lines.size(), [this, gutter_w, is_ini, has_sel, sel_start = sel_start, sel_end = sel_end](auto* vg, auto* theme, const Vec4& pos, s64 index){
        const auto focused = (m_line_index == index);
        const auto in_range = has_sel && (index >= sel_start && index <= sel_end);

        if (in_range) {
            auto tint = theme->GetColour(ThemeEntryID_FOCUS);
            tint.a *= 0.35f;
            gfx::drawRect(vg, pos, tint, 5.f);
        }

        if (focused && m_editable) {
            gfx::drawRectOutline(vg, theme, 4.f, pos);
        }

        const s64 display_line_num = m_is_streamed ? (m_stream_start_line + index) : (index + 1);
        const float gutter_font_size = std::max(10.f, m_font_size - 2.f);
        gfx::drawTextArgs(vg, pos.x + gutter_w - 8.f, pos.y + pos.h / 2.f, gutter_font_size,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%ld", static_cast<long>(display_line_num));

        const auto colour = theme->GetColour((focused && m_editable) ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        const auto text_x = pos.x + gutter_w;
        const auto text_w = pos.w - gutter_w - 10.f;

        if (focused && m_editable && !is_ini) {
            m_line_scroll.Draw(vg, true, text_x, pos.y + pos.h / 2.f, text_w, m_font_size,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, m_lines[index]);
        } else if (is_ini) {
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, pos.y, text_w, pos.h);

            const auto info = text_helper::ParseIniLine(m_lines[index]);
            if (info.type == text_helper::IniLineType::Comment) {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", m_lines[index].c_str());
            } else if (info.type == text_helper::IniLineType::Section) {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", m_lines[index].c_str());
            } else if (info.type == text_helper::IniLineType::KeyValue) {
                const std::string key_str(info.key);
                const std::string eq_str(info.eq);
                const std::string val_str(info.val);
                float key_bounds[4];
                nvgFontSize(vg, m_font_size);
                gfx::textBounds(vg, text_x, pos.y + pos.h / 2.f, key_bounds, key_str.c_str());
                const float key_w = key_bounds[2] - key_bounds[0];

                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), "%s", key_str.c_str());

                float eq_bounds[4];
                gfx::textBounds(vg, text_x + key_w, pos.y + pos.h / 2.f, eq_bounds, eq_str.c_str());
                const float eq_w = eq_bounds[2] - eq_bounds[0];

                gfx::drawTextArgs(vg, text_x + key_w, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", eq_str.c_str());

                gfx::drawTextArgs(vg, text_x + key_w + eq_w, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", val_str.c_str());
            } else {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", m_lines[index].c_str());
            }
            nvgRestore(vg);
        } else {
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, pos.y, text_w, pos.h);
            gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, m_font_size,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", m_lines[index].c_str());
            nvgRestore(vg);
        }
    });
}

void Menu::LoadImageFile() {
    SetAction(Button::A, Action{"Fit Image"_i18n, [this](){
        ResetImageView();
    }});
    SetAction(Button::X, Action{"Select"_i18n, [this](){
        ToggleCurrentSelection();
    }});
    SetAction(Button::Y, Action{"Invert Selection"_i18n, [this](){
        InvertSelection();
    }});
    SetAction(Button::START, Action{"Options"_i18n, [this](){
        DisplayImageOptions();
    }});
    SetAction(Button::L2, Action{"Zoom Up / Down"_i18n, "\uE0E6 \uE0EB/\uE0EC", [](){
    }});
    UpdateFullscreenAction();

    if (m_image_paths.size() > 1) {
        SetAction(Button::LEFT, Action{"Prev / Next Image"_i18n, "\uE0ED / \uE0EE", [this](){
            NextImage(-1);
        }});
        SetAction(Button::RIGHT, Action{"", [this](){
            NextImage(1);
        }});
    }

    const auto ext = path::Extension(m_path);
    const auto data = ImageLoadFromFile(m_path, IsJpegExtension(ext) ? ImageFlag_JPEG : ImageFlag_None);
    if (!data.data.empty()) {
        m_image_w = data.w;
        m_image_h = data.h;
        m_image = nvgCreateImageRGBA(App::GetVg(), data.w, data.h, 0, data.data.data());
    }

    ResetImageView();
}

void Menu::FreeImage() {
    if (m_image) {
        nvgDeleteImage(App::GetVg(), m_image);
        m_image = 0;
    }

    m_image_w = 0;
    m_image_h = 0;
}

void Menu::ResetImageView() {
    m_viewport.Reset();
    UpdateImageSubHeading();
}

void Menu::NextImage(s64 direction) {
    if (m_image_paths.empty()) {
        return;
    }

    const auto app = App::GetApp();
    if (app && (app->m_controller.GotHeld(Button::L2) || app->m_controller.GotDown(Button::L2))) {
        return;
    }
    if (m_viewport.IsZoomed()) {
        return;
    }

    const auto count = static_cast<s64>(m_image_paths.size());
    m_image_index = (m_image_index + direction + count) % count;
    m_path = m_image_paths[m_image_index];
    LoadCurrentFile();
}

void Menu::UpdateImageSubHeading() {
    if (!m_is_image_file || m_image_paths.empty()) {
        SetSubHeading("");
        return;
    }

    char buf[128]{};
    const auto selected = GetSelectedCount();
    if (selected) {
        std::snprintf(buf, sizeof(buf), "%zd / %zu  |  %zu selected", m_image_index + 1, m_image_paths.size(), selected);
    } else if (m_image_paths.size() > 1) {
        std::snprintf(buf, sizeof(buf), "%zd / %zu", m_image_index + 1, m_image_paths.size());
    }

    SetSubHeading(buf);
}

void Menu::ToggleFullscreen() {
    m_fullscreen = !m_fullscreen;
    ResetImageView();
    UpdateFullscreenAction();
}

void Menu::UpdateFullscreenAction() {
    SetAction(Button::R2, Action{m_fullscreen ? "Exit Full Screen"_i18n : "Full Screen"_i18n, [this](){
        ToggleFullscreen();
    }});
}

void Menu::ToggleCurrentSelection() {
    if (m_image_index < 0 || static_cast<size_t>(m_image_index) >= m_image_selected.size()) {
        return;
    }

    m_image_selected[m_image_index] = !m_image_selected[m_image_index];
    UpdateImageSubHeading();
}

void Menu::InvertSelection() {
    for (size_t i = 0; i < m_image_selected.size(); i++) {
        m_image_selected[i] = !m_image_selected[i];
    }

    UpdateImageSubHeading();
}

void Menu::DisplayImageOptions() {
    auto options = std::make_unique<Sidebar>("Image Options"_i18n, Sidebar::Side::RIGHT);

    options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
        App::PopToMenu();
        DeleteImages();
    }, "Permanently delete the selected image(s) from the SD card."_i18n);

    options->Add<SidebarEntryCallback>("Compress to zip"_i18n, [this](){
        App::PopToMenu();
        ZipImages("");
    }, "Compress the selected image(s) into a zip archive."_i18n);

    options->Add<SidebarEntryCallback>("Create Switch Theme"_i18n, [this](){
        App::PopToMenu();
        CreateSwitchTheme();
    }, "Use the selected image to create a custom Switch theme."_i18n);

    App::Push(std::move(options));
}

void Menu::DeleteImages() {
    const auto indices = GetTargetIndices();
    if (indices.empty()) {
        return;
    }

    const auto message = indices.size() == 1 ? "Delete selected image?"_i18n : "Delete selected images?"_i18n;
    App::Push<OptionBox>(message, "No"_i18n, "Yes"_i18n, 0, [this, indices](auto op_index){
        if (!op_index || !*op_index) {
            return;
        }

        App::Push<ProgressBox>(0, "Deleting"_i18n, "", [this, indices](auto pbox) -> Result {
            fs::FsNativeSd fs;
            for (const auto index : indices) {
                if (index < 0 || static_cast<size_t>(index) >= m_image_paths.size()) {
                    continue;
                }

                const auto path = m_image_paths[index];
                pbox->SetTitle(PathFileName(path));
                R_TRY(fs.DeleteFile(path));
            }

            R_SUCCEED();
        }, [this, indices](Result rc){
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to delete image"_i18n);
                return;
            }

            RemoveDeletedImages(indices);
            filebrowser::SignalChange();
            App::Notify("Delete success!"_i18n);

            if (m_image_paths.empty()) {
                SetPop();
                return;
            }

            LoadCurrentFile();
        });
    });
}

void Menu::ZipImages(fs::FsPath zip_out) {
    const auto targets = GetTargetPaths();
    if (targets.empty()) {
        return;
    }

    if (zip_out.empty()) {
        const auto parent = PathDirectory(targets.front());
        fs::FsPath file_path;

        if (targets.size() == 1) {
            auto name = PathFileName(targets.front());
            if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
                name.resize(dot);
            }
            std::snprintf(file_path, sizeof(file_path), "%s.zip", name.c_str());
            zip_out = fs::AppendPath(parent, file_path);
        } else {
            for (u64 i = 0; ; i++) {
                if (i) {
                    std::snprintf(file_path, sizeof(file_path), "Images (%zu).zip", i);
                } else {
                    std::snprintf(file_path, sizeof(file_path), "Images.zip");
                }

                zip_out = fs::AppendPath(parent, file_path);
                if (!m_fs->FileExists(zip_out)) {
                    break;
                }
            }
        }
    } else if (!std::string_view(zip_out).ends_with(".zip")) {
        zip_out += ".zip";
    }

    App::Push<ProgressBox>(0, "Compressing "_i18n, "", [zip_out, targets](auto pbox) -> Result {
        const auto t = std::time(nullptr);
        const auto tm = std::localtime(&t);
        fs::FsNativeSd fs;

        zip_fileinfo zip_info{};
        zip_info.tmz_date.tm_sec = tm->tm_sec;
        zip_info.tmz_date.tm_min = tm->tm_min;
        zip_info.tmz_date.tm_hour = tm->tm_hour;
        zip_info.tmz_date.tm_mday = tm->tm_mday;
        zip_info.tmz_date.tm_mon = tm->tm_mon;
        zip_info.tmz_date.tm_year = tm->tm_year;

        zlib_filefunc64_def file_func;
        mz::FileFuncStdio(&file_func);

        auto zfile = zipOpen2_64(zip_out, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        ON_SCOPE_EXIT(zipClose(zfile, "sphaira v" APP_VERSION_HASH));

        for (const auto& path : targets) {
            const auto name = PathFileName(path);
            pbox->SetTitle(name);
            pbox->NewTransfer(name);

            if (ZIP_OK != zipOpenNewFileInZip(zfile, name.c_str(), &zip_info, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION)) {
                R_THROW(Result_ZipOpenNewFileInZip);
            }
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));

            R_TRY(thread::TransferZip(pbox, zfile, &fs, path, nullptr, thread::Mode::SingleThreadedIfSmaller));
        }

        R_SUCCEED();
    }, [](Result rc){
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Compress failed!"_i18n);
        } else {
            filebrowser::SignalChange();
            App::Notify("Compress success!"_i18n);
        }
    });
}

void Menu::CreateSwitchTheme() {
    const auto targets = GetTargetPaths();
    if (targets.size() != 1) {
        App::Notify("Select one image for theme creation"_i18n);
        return;
    }

    App::Push<theme_creator::Menu>(targets.front());
}

void Menu::RemoveDeletedImages(const std::vector<s64>& indices) {
    auto sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    const auto original_index = m_image_index;
    auto next_index = m_image_index;
    bool deleted_current{};

    for (const auto index : sorted) {
        if (index == original_index) {
            deleted_current = true;
        } else if (index < original_index) {
            next_index--;
        }
    }

    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        const auto index = *it;
        if (index < 0 || static_cast<size_t>(index) >= m_image_paths.size()) {
            continue;
        }

        m_image_paths.erase(m_image_paths.begin() + index);
        if (static_cast<size_t>(index) < m_image_selected.size()) {
            m_image_selected.erase(m_image_selected.begin() + index);
        }
        if (static_cast<size_t>(index) < m_image_titles.size()) {
            m_image_titles.erase(m_image_titles.begin() + index);
        }
    }

    if (m_image_paths.empty()) {
        m_path.clear();
        m_image_index = 0;
        return;
    }

    if (deleted_current) {
        next_index = std::min<s64>(next_index, m_image_paths.size() - 1);
    }

    m_image_index = std::clamp<s64>(next_index, 0, m_image_paths.size() - 1);
    m_path = m_image_paths[m_image_index];
}

auto Menu::GetDisplayName() const -> std::string {
    if (m_is_image_file && m_image_index >= 0 && static_cast<size_t>(m_image_index) < m_image_titles.size() && !m_image_titles[m_image_index].empty()) {
        return m_image_titles[m_image_index];
    }

    return PathFileName(m_path);
}

auto Menu::GetSelectedCount() const -> size_t {
    return std::count(m_image_selected.begin(), m_image_selected.end(), true);
}

auto Menu::GetTargetIndices() const -> std::vector<s64> {
    std::vector<s64> out;

    if (GetSelectedCount()) {
        for (s64 i = 0; static_cast<size_t>(i) < m_image_selected.size(); i++) {
            if (m_image_selected[i]) {
                out.push_back(i);
            }
        }
    } else if (m_image_index >= 0 && static_cast<size_t>(m_image_index) < m_image_paths.size()) {
        out.push_back(m_image_index);
    }

    return out;
}

auto Menu::GetTargetPaths() const -> std::vector<fs::FsPath> {
    std::vector<fs::FsPath> out;
    for (const auto index : GetTargetIndices()) {
        if (index >= 0 && static_cast<size_t>(index) < m_image_paths.size()) {
            out.emplace_back(m_image_paths[index]);
        }
    }

    return out;
}

auto Menu::CurrentImageSelected() const -> bool {
    return m_image_index >= 0 && static_cast<size_t>(m_image_index) < m_image_selected.size() && m_image_selected[m_image_index];
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_load_failed) {
        m_load_failed = false;
        App::PushErrorBox(m_load_result, "Failed to read file"_i18n);
        SetPop();
        return;
    }

    if (m_is_image_file) {
        m_viewport.Update(controller, touch, m_image_w, m_image_h, ImageBounds(m_fullscreen), gfx::ImageFit::Contain);
    } else if (m_scroll_text) {
        m_scroll_text->Update(controller, touch);
    } else {
        UpdateText(controller, touch);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    if (m_is_image_file) {
        DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);

        if (!m_image || !m_image_w || !m_image_h) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to load image"_i18n.c_str());
            return;
        }

        const auto bounds = ImageBounds(m_fullscreen);
        const auto img_rect = m_viewport.GetImageRect(m_image_w, m_image_h, bounds, gfx::ImageFit::Contain);

        nvgSave(vg);
        nvgIntersectScissor(vg, bounds.x, bounds.y, bounds.w, bounds.h);
        gfx::drawImage(vg, img_rect.x, img_rect.y, img_rect.w, img_rect.h, m_image, 5);
        nvgRestore(vg);

        if (CurrentImageSelected()) {
            const Vec4 marker{bounds.x + 14.f, bounds.y + 14.f, 44.f, 44.f};
            gfx::drawRect(vg, marker, theme->GetColour(ThemeEntryID_POPUP), 5);
            gfx::drawText(vg, marker.x + marker.w / 2.f, marker.y + marker.h / 2.f - 2.f, 28.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "\uE14B", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }

        if (const auto selected = GetSelectedCount()) {
            const Vec4 badge{bounds.x + bounds.w - 184.f, bounds.y + 14.f, 170.f, 44.f};
            gfx::drawRect(vg, badge, theme->GetColour(ThemeEntryID_POPUP), 5);
            gfx::drawTextArgs(vg, badge.x + badge.w / 2.f, badge.y + badge.h / 2.f, 18.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%zu selected", selected);
        }
        return;
    }

    MenuBase::Draw(vg, theme);

    if (m_scroll_text) {
        m_scroll_text->Draw(vg, theme);
    } else {
        DrawText(vg, theme);
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

} // namespace sphaira::ui::menu::fileview
