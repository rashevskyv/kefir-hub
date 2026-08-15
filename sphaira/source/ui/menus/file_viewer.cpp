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
    RemoveAction(Button::LEFT);
    RemoveAction(Button::RIGHT);
    RemoveAction(Button::START);

    if (m_is_image_file) {
        LoadImageFile();
    } else {
        LoadTextFile();
    }
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
    m_is_truncated_preview = false;

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

    const s64 read_size = std::min<s64>(m_file_size, EDIT_MAX_SIZE);
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
    m_is_truncated_preview = (m_file_size > EDIT_MAX_SIZE);

    if (m_mode == TextMode::Edit && (!m_writable || m_is_truncated_preview)) {
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

    const Vec4 list_pos{40.f, 100.f, 1200.f, 530.f};
    const Vec4 item_pos{50.f, 105.f, 1180.f, 30.f};
    m_text_list = std::make_unique<List>(1, 17, list_pos, item_pos, Vec2{0.f, 2.f});

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
    RemoveAction(Button::START);
    RemoveAction(Button::L2);

    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    SetAction(Button::R2, Action{"Scroll"_i18n, "\uE102", [](){}});

    if (m_writable && !m_is_truncated_preview && m_file_size <= EDIT_MAX_SIZE) {
        SetAction(Button::A, Action{"Edit"_i18n, [this](){
            SwitchToEditMode();
        }});
    }
}

void Menu::SetupEditActions() {
    RemoveAction(Button::R2);

    SetAction(Button::A, Action{"Edit line"_i18n, [this](){
        EditLine();
    }});
    SetAction(Button::X, Action{"Actions"_i18n, [this](){
        ShowLineActions();
    }});
    SetAction(Button::START, Action{"Options"_i18n, [this](){
        DisplayTextOptions();
    }});
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        PromptTextExit();
    }});
    SetAction(Button::L2, Action{"Cursor / Scroll"_i18n, "\uE101 / \uE102", [](){}});
}

void Menu::SwitchToEditMode() {
    if (!m_writable || m_is_truncated_preview || m_file_size > EDIT_MAX_SIZE) {
        return;
    }

    m_mode = TextMode::Edit;
    m_editable = true;

    if (m_text_list) {
        const float item_h = m_text_list->GetMaxY();
        const s64 first_visible = (item_h > 0.f) ? static_cast<s64>(m_text_list->GetYoff() / item_h) : 0;
        const s64 total = static_cast<s64>(m_lines.size());
        m_line_index = std::clamp<s64>(first_visible, 0, total > 0 ? total - 1 : 0);
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }

    SetupEditActions();
    UpdateTextSubHeading();
}

void Menu::UpdateTextSubHeading() {
    auto heading = std::to_string(m_line_index + 1) + " / " + std::to_string(m_lines.size());
    if (m_is_truncated_preview) {
        heading += "  (" + "Preview truncated"_i18n + ")";
    } else if (m_mode == TextMode::View) {
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
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::InsertLine() {
    if (!m_editable) return;
    PushUndo();
    m_lines.insert(m_lines.begin() + m_line_index + 1, "");
    m_line_index++;
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    m_text_dirty = (BuildText() != m_saved_text);
    UpdateTextSubHeading();
}

void Menu::DeleteLine() {
    if (!m_editable) return;
    PushUndo();
    m_lines.erase(m_lines.begin() + m_line_index);
    if (m_lines.empty()) {
        m_lines.emplace_back();
    }
    m_line_index = std::clamp<s64>(m_line_index, 0, m_lines.size() - 1);
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
    if (m_text_list) {
        m_text_list->EnsureVisible(m_line_index, m_lines.size());
    }
    m_line_scroll.Reset();
    UpdateTextSubHeading();
}

auto Menu::SaveText() -> bool {
    if (!m_editable || m_is_truncated_preview || !m_fs) {
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
    PopupList::Items items;
    items.emplace_back("Edit line"_i18n);
    items.emplace_back("Insert line below"_i18n);
    items.emplace_back("Delete line"_i18n);
    items.emplace_back("Join with next line"_i18n);

    App::Push<PopupList>("Line "_i18n + std::to_string(m_line_index + 1), items, [this](auto op_index){
        if (!op_index) {
            return;
        }

        switch (*op_index) {
            case 0: EditLine(); break;
            case 1: InsertLine(); break;
            case 2: DeleteLine(); break;
            case 3: JoinLine(); break;
        }
    });
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

    if (!m_editable) {
        m_text_list->OnUpdateTouchOnly(touch, count);
        return;
    }

    Controller local_ctrl = *controller;
    const u64 rs_mask = static_cast<u64>(Button::RS_UP) | static_cast<u64>(Button::RS_DOWN) | static_cast<u64>(Button::RS_LEFT) | static_cast<u64>(Button::RS_RIGHT);
    local_ctrl.m_kdown &= ~rs_mask;
    local_ctrl.m_kheld &= ~rs_mask;
    local_ctrl.m_kup &= ~rs_mask;

    m_text_list->OnUpdate(&local_ctrl, touch, m_line_index, m_lines.size(), [this](bool touched, s64 index){
        if (touched) {
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
                    if (text_helper::IsIniFile(m_path)) {
                        const auto toggle = text_helper::ToggleIniBoolean(m_lines[index]);
                        if (toggle.toggled) {
                            PushUndo();
                            m_lines[index] = toggle.new_line;
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
            App::PlaySoundEffect(SoundEffect_Focus);
            m_line_index = index;
            m_line_scroll.Reset();
            UpdateTextSubHeading();
        }
    });
}

void Menu::DrawText(NVGcontext* vg, Theme* theme) {
    if (!m_text_list) {
        return;
    }

    const auto gutter = std::to_string(m_lines.size());
    float bounds[4];
    nvgFontSize(vg, 18.f);
    gfx::textBounds(vg, 0, 0, bounds, gutter.c_str());
    const float gutter_w = bounds[2] - bounds[0] + 16.f;
    const bool is_ini = text_helper::IsIniFile(m_path);

    m_text_list->Draw(vg, theme, m_lines.size(), [this, gutter_w, is_ini](auto* vg, auto* theme, const Vec4& pos, s64 index){
        const auto selected = (m_line_index == index);
        if (selected && m_editable) {
            gfx::drawRectOutline(vg, theme, 4.f, pos);
        }

        gfx::drawTextArgs(vg, pos.x + gutter_w - 8.f, pos.y + pos.h / 2.f, 16.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%ld", static_cast<long>(index + 1));

        const auto colour = theme->GetColour((selected && m_editable) ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        const auto text_x = pos.x + gutter_w;
        const auto text_w = pos.w - gutter_w - 10.f;

        if (selected && m_editable) {
            m_line_scroll.Draw(vg, true, text_x, pos.y + pos.h / 2.f, text_w, 18.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, m_lines[index]);
        } else if (is_ini) {
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, pos.y, text_w, pos.h);

            const auto info = text_helper::ParseIniLine(m_lines[index]);
            if (info.type == text_helper::IniLineType::Comment) {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, 18.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", m_lines[index].c_str());
            } else if (info.type == text_helper::IniLineType::Section) {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, 18.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", m_lines[index].c_str());
            } else if (info.type == text_helper::IniLineType::KeyValue) {
                const std::string key_str(info.key);
                const std::string eq_str(info.eq);
                const std::string val_str(info.val);

                float key_bounds[4];
                nvgFontSize(vg, 18.f);
                gfx::textBounds(vg, text_x, pos.y + pos.h / 2.f, key_bounds, key_str.c_str());
                const float key_w = key_bounds[2] - key_bounds[0];

                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, 18.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), "%s", key_str.c_str());

                float eq_bounds[4];
                gfx::textBounds(vg, text_x + key_w, pos.y + pos.h / 2.f, eq_bounds, eq_str.c_str());
                const float eq_w = eq_bounds[2] - eq_bounds[0];

                gfx::drawTextArgs(vg, text_x + key_w, pos.y + pos.h / 2.f, 18.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", eq_str.c_str());

                gfx::drawTextArgs(vg, text_x + key_w + eq_w, pos.y + pos.h / 2.f, 18.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", val_str.c_str());
            } else {
                gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, 18.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, "%s", m_lines[index].c_str());
            }
            nvgRestore(vg);
        } else {
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, pos.y, text_w, pos.h);
            gfx::drawTextArgs(vg, text_x, pos.y + pos.h / 2.f, 18.f,
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
        SetAction(Button::LEFT, Action{"Previous Image"_i18n, [this](){
            NextImage(-1);
        }});
        SetAction(Button::RIGHT, Action{"Next Image"_i18n, [this](){
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
    m_zoom = 1.f;
    m_pan_x = 0.f;
    m_pan_y = 0.f;
    UpdateImageSubHeading();
}

void Menu::ZoomImage(float factor) {
    m_zoom = std::clamp(m_zoom * factor, 1.f, 8.f);
    ClampPan();
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
    if (m_zoom > 1.001f) {
        return;
    }

    const auto count = static_cast<s64>(m_image_paths.size());
    m_image_index = (m_image_index + direction + count) % count;
    m_path = m_image_paths[m_image_index];
    LoadCurrentFile();
}

void Menu::PanImage(float dx, float dy) {
    m_pan_x += dx;
    m_pan_y += dy;
    ClampPan();
}

void Menu::ClampPan() {
    if (!m_image_w || !m_image_h) {
        m_pan_x = 0.f;
        m_pan_y = 0.f;
        return;
    }

    const auto bounds = ImageBounds(m_fullscreen);
    const auto fit_scale = std::min(bounds.w / static_cast<float>(m_image_w), bounds.h / static_cast<float>(m_image_h));
    const auto image_w = static_cast<float>(m_image_w) * fit_scale * m_zoom;
    const auto image_h = static_cast<float>(m_image_h) * fit_scale * m_zoom;
    const auto max_pan_x = std::max(0.f, (image_w - bounds.w) / 2.f);
    const auto max_pan_y = std::max(0.f, (image_h - bounds.h) / 2.f);

    m_pan_x = std::clamp(m_pan_x, -max_pan_x, max_pan_x);
    m_pan_y = std::clamp(m_pan_y, -max_pan_y, max_pan_y);
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
        const auto zoom_modifier = controller->GotDown(Button::L2) || controller->GotHeld(Button::L2);
        if (zoom_modifier) {
            const auto zoom_in = controller->GotDown(Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                controller->GotHeld(Button::DPAD_UP | Button::LS_UP | Button::RS_UP);
            const auto zoom_out = controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                controller->GotHeld(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN);

            if (zoom_in) {
                ZoomImage(1.05f);
            } else if (zoom_out) {
                ZoomImage(1.f / 1.05f);
            }
        } else if (m_zoom > 1.001f) {
            constexpr float PAN_SPEED = 12.f;
            if (controller->GotDown(Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                controller->GotHeld(Button::DPAD_UP | Button::LS_UP | Button::RS_UP)) {
                PanImage(0.f, -PAN_SPEED);
            }
            if (controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                controller->GotHeld(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN)) {
                PanImage(0.f, PAN_SPEED);
            }
            if (controller->GotDown(Button::DPAD_LEFT | Button::LS_LEFT | Button::RS_LEFT) ||
                controller->GotHeld(Button::DPAD_LEFT | Button::LS_LEFT | Button::RS_LEFT)) {
                PanImage(-PAN_SPEED, 0.f);
            }
            if (controller->GotDown(Button::DPAD_RIGHT | Button::LS_RIGHT | Button::RS_RIGHT) ||
                controller->GotHeld(Button::DPAD_RIGHT | Button::LS_RIGHT | Button::RS_RIGHT)) {
                PanImage(PAN_SPEED, 0.f);
            }
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

        const auto bounds = ImageBounds(m_fullscreen);
        const auto scale = std::min(bounds.w / static_cast<float>(m_image_w), bounds.h / static_cast<float>(m_image_h)) * m_zoom;
        const auto image_w = static_cast<float>(m_image_w) * scale;
        const auto image_h = static_cast<float>(m_image_h) * scale;
        const auto image_x = bounds.x + (bounds.w - image_w) / 2.f + m_pan_x;
        const auto image_y = bounds.y + (bounds.h - image_h) / 2.f + m_pan_y;

        nvgSave(vg);
        nvgIntersectScissor(vg, bounds.x, bounds.y, bounds.w, bounds.h);
        gfx::drawImage(vg, image_x, image_y, image_w, image_h, m_image, 5);
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
