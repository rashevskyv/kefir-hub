#pragma once

#include "ui/menus/menu_base.hpp"
#include "fs.hpp"
#include <string>
#include <vector>

namespace sphaira::ui {
struct ProgressBox;
}

namespace sphaira::ui::menu::theme_creator {

struct Menu final : MenuBase {
    Menu(const fs::FsPath& path, u32 flags = MenuFlag_None);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Theme Creator"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void LoadImageFile();
    void FreeImage();
    void ResetImageView();
    void ZoomImage(float factor);
    void PanImage(float dx, float dy);
    void ClampPan();
    void DisplayTargetSelector();
    void EditThemeName();
    void EditAuthor();
    Result GenerateTheme(ui::ProgressBox* pbox);
    void GenerateThemeCallback();

private:
    fs::FsPath m_path;
    fs::FsNativeSd m_fs{};
    
    int m_image{};
    int m_image_w{};
    int m_image_h{};
    std::vector<u8> m_raw_image_data{};
    int m_raw_w{};
    int m_raw_h{};

    float m_zoom{1.f};
    float m_pan_x{};
    float m_pan_y{};

    std::string m_theme_name;
    std::string m_author;
    std::string m_target{"home"};
};

} // namespace sphaira::ui::menu::theme_creator
