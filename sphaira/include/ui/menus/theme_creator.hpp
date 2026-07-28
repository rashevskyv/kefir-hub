#pragma once

#include "ui/menus/menu_base.hpp"
#include "fs.hpp"
#include <string>
#include <vector>
#include <chrono>

namespace sphaira::ui {
struct ProgressBox;
}

namespace sphaira::ui::menu::theme_creator {

enum State {
    State_Crop,
    State_Confirm
};

struct Menu final : MenuBase {
    Menu(const fs::FsPath& path, u32 flags = MenuFlag_None);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Theme Creator"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    // full screen cropper: it never draws the standard menu body and puts its
    // own bar across the top of the screen, so the shared chrome stays off.
    auto WantsChrome() const -> bool override {
        return false;
    }

private:
    void LoadImageFile();
    void FreeImage();
    void ResetImageView();
    void ZoomImage(float factor);
    void PanImage(float dx, float dy);
    void ClampPan();
    void ToggleFullscreen();
    void UpdateFullscreenAction();
    void DisplayTargetSelector();
    void EditThemeName();
    void EditAuthor();
    Result GenerateTheme(ui::ProgressBox* pbox);
    void GenerateThemeCallback();
    void InstallThemeAction(bool reboot);

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
    bool m_fullscreen{};

    std::string m_theme_name;
    std::string m_author;
    std::string m_target{"home"};

    State m_state = State_Crop;
    bool m_holding_a = false;
    bool m_holding_y = false;
    std::chrono::steady_clock::time_point m_hold_start;
    float m_hold_progress = 0.f;
};

} // namespace sphaira::ui::menu::theme_creator
