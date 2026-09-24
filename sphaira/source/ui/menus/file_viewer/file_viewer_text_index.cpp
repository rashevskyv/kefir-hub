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
auto Menu::TryToggleLine(s64 index) -> bool {
    if (!m_editable || index < 0 || index >= static_cast<s64>(m_lines.size())) {
        return false;
    }
    const auto toggle = text_helper::ToggleIniBoolean(m_lines[index]);
    if (!toggle.toggled) {
        return false;
    }
    PushUndo();
    m_lines[index] = toggle.new_line;
    ClearRangeSelection();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
    App::PlaySoundEffect(SoundEffect_Focus);
    return true;
}

void Menu::EditLine() {
    if (!m_editable) return;
    if (m_line_index < 0 || m_line_index >= static_cast<s64>(m_lines.size())) {
        return;
    }
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
    m_adjusting_range = false;
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
    m_adjusting_range = false;
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
    m_adjusting_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(true);
    }
    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::ClearRangeSelection() {
    m_selecting_range = false;
    m_has_range = false;
    m_adjusting_range = false;
    if (m_text_list) {
        m_text_list->SetWrap(true);
    }
    SetupEditActions();
}

void Menu::StartAdjustRange() {
    if (!m_editable || !m_has_range) {
        return;
    }
    m_adjusting_range = true;
    m_adjust_orig_start = m_range_start;
    m_adjust_orig_end = m_range_end;
    if (m_text_list) {
        m_text_list->SetWrap(false);
    }
    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::FinishAdjustRange() {
    if (!m_adjusting_range) {
        return;
    }
    m_adjusting_range = false;
    SetupEditActions();
    App::PlaySoundEffect(SoundEffect_Focus);
    UpdateTextSubHeading();
}

void Menu::CancelAdjustRange() {
    if (!m_adjusting_range) {
        return;
    }
    m_range_start = m_adjust_orig_start;
    m_range_end = m_adjust_orig_end;
    m_adjusting_range = false;
    m_line_index = std::clamp<s64>(m_range_start, 0, m_lines.empty() ? 0 : static_cast<s64>(m_lines.size()) - 1);
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::NudgeRangeTop(s64 delta) {
    if (m_lines.empty()) {
        return;
    }
    const s64 next = std::clamp<s64>(m_range_start + delta, 0, m_range_end);
    if (next == m_range_start) {
        return;
    }
    m_range_start = next;
    m_line_index = m_range_start;
    if (m_text_list) {
        m_text_list->EnsureVisible(m_range_start, m_lines.size());
    }
    m_line_scroll.Reset();
    App::PlaySoundEffect(SoundEffect_Focus);
    UpdateTextSubHeading();
}

void Menu::NudgeRangeBottom(s64 delta) {
    if (m_lines.empty()) {
        return;
    }
    const s64 last = static_cast<s64>(m_lines.size()) - 1;
    const s64 next = std::clamp<s64>(m_range_end + delta, m_range_start, last);
    if (next == m_range_end) {
        return;
    }
    m_range_end = next;
    m_line_index = m_range_end;
    if (m_text_list) {
        m_text_list->EnsureVisible(m_range_end, m_lines.size());
    }
    m_line_scroll.Reset();
    App::PlaySoundEffect(SoundEffect_Focus);
    UpdateTextSubHeading();
}

void Menu::UpdateAdjustRange(Controller* controller) {
    const bool l_mod = controller->GotDown(Button::L) || controller->GotHeld(Button::L);
    const bool r_mod = controller->GotDown(Button::R) || controller->GotHeld(Button::R);
    const bool pad_up = controller->GotDown(Button::DPAD_UP | Button::UP) &&
                        !controller->GotDown(Button::LS_UP | Button::RS_UP);
    const bool pad_down = controller->GotDown(Button::DPAD_DOWN | Button::DOWN) &&
                          !controller->GotDown(Button::LS_DOWN | Button::RS_DOWN);

    if (controller->GotDown(Button::LS_UP) || (l_mod && pad_up)) {
        NudgeRangeTop(-1);
    } else if (controller->GotDown(Button::LS_DOWN) || (l_mod && pad_down)) {
        NudgeRangeTop(1);
    }

    if (controller->GotDown(Button::RS_UP) || (r_mod && pad_up)) {
        NudgeRangeBottom(-1);
    } else if (controller->GotDown(Button::RS_DOWN) || (r_mod && pad_down)) {
        NudgeRangeBottom(1);
    }
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

void Menu::InsertSnippet(const std::string& text, s64 insert_at) {
    if (!m_editable) {
        return;
    }

    auto lines = SplitLines(text);
    if (lines.size() == 1 && lines[0].empty()) {
        return;
    }

    insert_at = std::clamp<s64>(insert_at, 0, static_cast<s64>(m_lines.size()));
    PushUndo();
    m_lines.insert(m_lines.begin() + insert_at, lines.begin(), lines.end());
    m_line_index = std::clamp<s64>(insert_at + static_cast<s64>(lines.size()) - 1, 0, m_lines.size() - 1);
    ClearRangeSelection();
    RecreateList();
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::PasteFromDevice() {
    if (!m_editable) {
        return;
    }

    const auto [start, end] = GetTargetRange();
    const s64 insert_at = std::clamp<s64>(end + 1, 0, static_cast<s64>(m_lines.size()));

    remote_input::Options opts{};
    opts.title = "Paste text"_i18n;
    opts.guide = "Paste or type the text, then Send."_i18n;
    opts.placeholder = "Paste text here"_i18n;
    opts.multiline = true;
    opts.min_length = 1;
    opts.max_length = 256 * 1024;

    remote_input::RequestRemoteText(opts, [this, insert_at](const std::string& text) {
        InsertSnippet(text, insert_at);
    });
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

void Menu::DisplayTextOptions() {
    auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (m_writable && !m_is_streamed && m_file_size <= EDIT_MAX_SIZE) {
        options->Add<SidebarEntryCallback>("Edit on PC / phone"_i18n, [this](){
            EditOnDevice();
        }, "Open this file in a browser. Save to Switch writes the file; Save and Close also ends the session."_i18n)->SetIcon(ActionIcon::Edit);
    }

    if (!m_editable) {
        return;
    }

    options->Add<SidebarEntryCallback>("Save"_i18n, [this](){
        SaveText();
    }, "Save changes to the file."_i18n)->SetIcon(ActionIcon::Save);

    options->Add<SidebarEntryCallback>("Undo"_i18n, [this](){
        Undo();
    }, "Step back through the last 32 edits."_i18n)->SetIcon(ActionIcon::Undo);

    options->Add<SidebarEntryCallback>("Redo"_i18n, [this](){
        Redo();
    }, "Step forward again after an undo."_i18n)->SetIcon(ActionIcon::Redo);

    options->Add<SidebarEntryCallback>("Go to line"_i18n, [this](){
        GoToLine();
    }, "Jump straight to a line number."_i18n)->SetIcon(ActionIcon::GoTo);
}


} // namespace sphaira::ui::menu::fileview
