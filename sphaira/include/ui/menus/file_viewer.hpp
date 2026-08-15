#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/scrollable_text.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/list.hpp"
#include "fs.hpp"
#include "text_helper.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::fileview {

enum class TextMode {
    View,
    Edit,
};

struct Menu final : MenuBase {
    Menu(const fs::FsPath& path);
    Menu(fs::Fs* fs, const fs::FsPath& path, TextMode mode = TextMode::View, bool writable = false);
    Menu(const fs::FsPath& path, std::vector<fs::FsPath> image_paths, s64 image_index, std::vector<std::string> image_titles = {});
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "File"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    // standard chrome is drawn in normal (non-fullscreen) mode, and hidden in fullscreen.
    auto WantsChrome() const -> bool override {
        return !m_fullscreen;
    }

private:
    void LoadCurrentFile();
    void LoadTextFile();
    void LoadImageFile();

    // text editing. only enabled for files small enough to hold in memory as
    // lines, see EDIT_MAX_SIZE - anything larger stays the read only view.
    void DrawText(NVGcontext* vg, Theme* theme);
    void UpdateText(Controller* controller, TouchInfo* touch);
    void ShowLineActions();
    void EditLine();
    void InsertLine();
    void DeleteLine();
    void JoinLine();
    void GoToLine();
    void SetupViewActions();
    void SetupEditActions();
    void SwitchToEditMode();
    void DisplayTextOptions();
    void PushUndo();
    void Undo();
    void Redo();
    auto SaveText() -> bool;
    void PromptTextExit();
    void UpdateTextSubHeading();
    auto BuildText() const -> std::string;
    void PageUp(s64 count);
    void PageDown(s64 count);
    void LineUp();
    void LineDown();
    void ZoomText(float delta);
    void LoadPage(s64 page_idx);
    void PreloadPages();
    void RecreateList();
    void FreeImage();
    void ResetImageView();
    void ZoomImage(float factor);
    void NextImage(s64 direction);
    void PanImage(float dx, float dy);
    void ClampPan();
    void ToggleFullscreen();
    void UpdateFullscreenAction();
    void UpdateImageSubHeading();
    void ToggleCurrentSelection();
    void InvertSelection();
    void DisplayImageOptions();
    void DeleteImages();
    void ZipImages(fs::FsPath zip_path);
    void CreateSwitchTheme();
    void RemoveDeletedImages(const std::vector<s64>& indices);
    auto GetDisplayName() const -> std::string;
    auto GetSelectedCount() const -> size_t;
    auto GetTargetIndices() const -> std::vector<s64>;
    auto GetTargetPaths() const -> std::vector<fs::FsPath>;
    auto CurrentImageSelected() const -> bool;

private:
    fs::FsPath m_path;
    std::vector<fs::FsPath> m_image_paths{};
    std::vector<std::string> m_image_titles{};
    std::vector<bool> m_image_selected{};
    s64 m_image_index{};
    fs::FsNativeSd m_sd_fs{};
    fs::Fs* m_fs{&m_sd_fs};
    fs::File m_file{};
    s64 m_file_size{};
    s64 m_file_offset{};

    TextMode m_mode{TextMode::View};
    bool m_writable{false};
    Result m_load_result{0};
    bool m_load_failed{false};
    std::string m_saved_text{};
    bool m_is_streamed{false};

    s64 m_last_tapped_row{-1};
    u64 m_last_tap_time{0};

    // reading a file in as lines costs roughly its size in ram twice over once
    // an undo snapshot exists, so past this the viewer stays read only.
    static constexpr s64 EDIT_MAX_SIZE = 4 * 1024 * 1024;

    std::unique_ptr<ScrollableText> m_scroll_text{};
    std::unique_ptr<List> m_text_list{};
    ScrollingText m_line_scroll{};
    std::vector<std::string> m_lines{};
    std::vector<std::vector<std::string>> m_undo{};
    std::vector<std::vector<std::string>> m_redo{};
    // whichever break the file already used, so saving does not rewrite every
    // line ending of a crlf file.
    std::string m_line_break{"\n"};
    s64 m_line_index{};
    bool m_editable{};
    bool m_text_dirty{};

    // Streamed large file pager state
    s64 m_current_page{0};
    s64 m_viewport_rows{17};
    s64 m_buffer_rows{34};
    float m_font_size{18.f};
    bool m_l_modifier_used{false};
    bool m_touch_was_pinch{false};
    std::vector<s64> m_page_offsets{0};
    std::vector<s64> m_page_start_lines{1};
    s64 m_stream_start_line{1};
    std::map<s64, text_helper::Page> m_page_cache{};

    bool m_is_image_file{};
    int m_image{};
    int m_image_w{};
    int m_image_h{};
    float m_zoom{1.f};
    float m_pan_x{};
    float m_pan_y{};
    bool m_fullscreen{};

    s64 m_start{};
    s64 m_index{}; // where i am in the array
};

} // namespace sphaira::ui::menu::fileview
