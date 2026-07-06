#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/scrollable_text.hpp"
#include "fs.hpp"
#include <string>
#include <vector>

namespace sphaira::ui::menu::fileview {

struct Menu final : MenuBase {
    Menu(const fs::FsPath& path);
    Menu(const fs::FsPath& path, std::vector<fs::FsPath> image_paths, s64 image_index, std::vector<std::string> image_titles = {});
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "File"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void LoadCurrentFile();
    void LoadTextFile();
    void LoadImageFile();
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
    void UploadImages();
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
    fs::FsNativeSd m_fs{};
    fs::File m_file{};
    s64 m_file_size{};
    s64 m_file_offset{};

    std::unique_ptr<ScrollableText> m_scroll_text{};
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
