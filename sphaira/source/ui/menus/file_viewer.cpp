#include "ui/menus/file_viewer/file_viewer_internal.hpp"
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
#include "ui/remote_input.hpp"
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
    remote_input::SetRemoteInputActive(false);
    m_file.Close();
    m_file.m_fs = nullptr;
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
    m_rotation = 0;
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

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_load_failed) {
        m_load_failed = false;
        App::PushErrorBox(m_load_result, "Failed to read file"_i18n);
        SetPop();
        return;
    }

    if (m_is_image_file) {
        const bool was_zoomed = m_viewport.IsZoomed();
        const bool rotated_90 = (m_rotation % 2 != 0);
        const int eff_w = rotated_90 ? m_image_h : m_image_w;
        const int eff_h = rotated_90 ? m_image_w : m_image_h;
        m_viewport.Update(controller, touch, eff_w, eff_h, ImageBounds(m_fullscreen), gfx::ImageFit::Contain);
        if (m_image_pick && was_zoomed != m_viewport.IsZoomed()) {
            UpdateImageAAction();
        }
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

        const bool rotated_90 = (m_rotation % 2 != 0);
        const int eff_w = rotated_90 ? m_image_h : m_image_w;
        const int eff_h = rotated_90 ? m_image_w : m_image_h;
        const auto bounds = ImageBounds(m_fullscreen);
        const auto img_rect = m_viewport.GetImageRect(eff_w, eff_h, bounds, gfx::ImageFit::Contain);

        nvgSave(vg);
        nvgIntersectScissor(vg, bounds.x, bounds.y, bounds.w, bounds.h);
        if (m_rotation == 0) {
            gfx::drawImage(vg, img_rect.x, img_rect.y, img_rect.w, img_rect.h, m_image, 5);
        } else {
            const float cx = img_rect.x + img_rect.w * 0.5f;
            const float cy = img_rect.y + img_rect.h * 0.5f;
            const float draw_w = rotated_90 ? img_rect.h : img_rect.w;
            const float draw_h = rotated_90 ? img_rect.w : img_rect.h;
            const float lx = -draw_w * 0.5f;
            const float ly = -draw_h * 0.5f;

            nvgTranslate(vg, cx, cy);
            nvgRotate(vg, nvgDegToRad(static_cast<float>(m_rotation * 90)));
            const auto paint = nvgImagePattern(vg, lx, ly, draw_w, draw_h, 0, m_image, 1.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, lx, ly, draw_w, draw_h, 5);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }
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
    if (m_pending_remote_edit) {
        m_pending_remote_edit = false;
        if (!m_load_failed) {
            EditOnDevice();
        }
    }
}
} // namespace sphaira::ui::menu::fileview
