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

    if (m_mode == TextMode::Hex || m_file_size > EDIT_MAX_SIZE) {
        m_is_streamed = true;
        m_mode = m_mode == TextMode::Hex ? TextMode::Hex : TextMode::View;
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
        SetAction(Button::START, Action{"Options"_i18n, [this](){
            DisplayTextOptions();
        }});
    }
}

void Menu::SetupEditActions() {
    RemoveAction(Button::R2);
    RemoveAction(Button::L);
    RemoveAction(Button::R);
    RemoveAction(Button::SELECT);
    RemoveAction(Button::R3);
    RemoveAction(Button::Y);
    RemoveAction(Button::L2);

    if (m_adjusting_range) {
        SetAction(Button::A, Action{"Done"_i18n, [this](){
            FinishAdjustRange();
        }});
        SetAction(Button::B, Action{"Cancel"_i18n, [this](){
            CancelAdjustRange();
        }});
        SetAction(Button::L, Action{ActionType::UP, "Top bound"_i18n, "\uE0E4+\uE0EB/\uE0EC / \uE101", [](){}});
        SetAction(Button::R, Action{ActionType::UP, "Bottom bound"_i18n, "\uE0E5+\uE0EB/\uE0EC / \uE102", [](){}});
    } else if (m_selecting_range) {
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
        SetAction(Button::Y, Action{"Toggle"_i18n, [this](){
            if (!TryToggleLine(m_line_index)) {
                App::Notify("Not a boolean value"_i18n);
            }
        }});
    }

    SetAction(Button::X, Action{"Actions"_i18n, [this](){
        ShowLineActions();
    }});
    SetAction(Button::START, Action{"Options"_i18n, [this](){
        DisplayTextOptions();
    }});
    if (!m_adjusting_range) {
        SetAction(Button::L2, Action{"Cursor / Scroll"_i18n, "\uE101 / \uE102", [](){}});
    }
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
    m_adjusting_range = false;

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
    m_adjusting_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(true);
    }

    SetupViewActions();
    UpdateTextSubHeading();
}

void Menu::ShowLineActions() {
    struct ActionEntry {
        std::string title;
        std::optional<ActionIcon> icon;
        std::function<void()> callback;
    };
    std::vector<ActionEntry> actions;

    if (m_line_index >= 0 && m_line_index < static_cast<s64>(m_lines.size())
        && text_helper::ToggleIniBoolean(m_lines[m_line_index]).toggled) {
        actions.push_back({"Toggle"_i18n, ActionIcon::Toggle, [this](){ TryToggleLine(m_line_index); }});
    }
    actions.push_back({"Edit line"_i18n, ActionIcon::Edit, [this](){ EditLine(); }});

    if (m_has_range) {
        if (!m_adjusting_range) {
            actions.push_back({"Expand range"_i18n, ActionIcon::Range, [this](){ StartAdjustRange(); }});
        }
        actions.push_back({"Clear selection"_i18n, ActionIcon::Delete, [this](){ ClearRangeSelection(); UpdateTextSubHeading(); }});
    } else {
        actions.push_back({"Select range"_i18n, ActionIcon::Range, [this](){ StartRangeSelection(); }});
    }

    actions.push_back({"Copy"_i18n, ActionIcon::Copy, [this](){ CopySelection(); }});
    actions.push_back({"Cut"_i18n, ActionIcon::Cut, [this](){ CutSelection(); }});
    actions.push_back({"Paste below"_i18n, ActionIcon::Paste, [this](){ PasteBelow(); }});
    actions.push_back({"Paste from PC / phone"_i18n, ActionIcon::Paste, [this](){ PasteFromDevice(); }});
    actions.push_back({"Delete"_i18n, ActionIcon::Delete, [this](){ DeleteLine(); }});
    actions.push_back({"Insert line below"_i18n, ActionIcon::Insert, [this](){ InsertLine(); }});
    actions.push_back({"Join with next line"_i18n, ActionIcon::Join, [this](){ JoinLine(); }});

    if (text_helper::IsIniFile(m_path)) {
        actions.push_back({"Comment"_i18n, ActionIcon::Comment, [this](){ CommentSelection(); }});
        actions.push_back({"Uncomment"_i18n, ActionIcon::Comment, [this](){ UncommentSelection(); }});
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

void Menu::EditOnDevice() {
    if (!m_writable || m_is_streamed || m_file_size > EDIT_MAX_SIZE) {
        return;
    }

    remote_input::Options opts{};
    opts.title = GetDisplayName();
    opts.guide = "Edit on your computer or phone. Save to Switch keeps the session open."_i18n;
    opts.default_text = BuildText();
    opts.placeholder = opts.title;
    opts.multiline = true;
    opts.editor = true;
    opts.min_length = 0;
    opts.max_length = static_cast<int>(EDIT_MAX_SIZE);

    remote_input::RequestRemoteText(opts, [this](const std::string& text) {
        ApplyRemoteText(text);
    });
}

void Menu::ApplyRemoteText(const std::string& text) {
    if (!m_writable || m_is_streamed) {
        return;
    }
    if (!m_editable) {
        SwitchToEditMode();
    }

    auto next = SplitLines(text);
    if (next == m_lines) {
        if (m_text_dirty) {
            SaveText();
        }
        return;
    }

    PushUndo();
    m_lines = std::move(next);

    m_line_index = 0;
    ClearRangeSelection();
    RecreateList();
    SaveText();
    UpdateTextSubHeading();
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

    if (m_adjusting_range) {
        UpdateAdjustRange(controller);
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
                    if (TryToggleLine(index)) {
                        return;
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


} // namespace sphaira::ui::menu::fileview
