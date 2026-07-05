#include "ui/menus/theme_creator.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "log.hpp"
#include "nro.hpp"
#include "defines.hpp"
#include "minizip_helper.hpp"

#include <minizip/zip.h>
#include <yyjson.h>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sphaira::ui::menu::theme_creator {
namespace {

constexpr const char* NRO_PATHS[]{
    "/switch/NXThemesInstaller.nro",
    "/switch/NXThemesInstaller/NXThemesInstaller.nro",
    "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
};

auto GetNroPath() -> const char* {
    fs::FsNativeSd fs;
    for (auto& path : NRO_PATHS) {
        if (fs.FileExists(path)) {
            return path;
        }
    }
    return nullptr;
}

auto HasNro() -> bool {
    return GetNroPath() != nullptr;
}

} // namespace

Menu::Menu(const fs::FsPath& path, u32 flags) : MenuBase{path, flags}, m_path{path} {
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});
    
    // Set default theme name based on filename
    std::string orig_name = m_path.toString();
    const auto last_slash = orig_name.find_last_of('/');
    if (last_slash != std::string::npos) {
        orig_name = orig_name.substr(last_slash + 1);
    }
    const auto last_dot = orig_name.find_last_of('.');
    if (last_dot != std::string::npos) {
        orig_name = orig_name.substr(0, last_dot);
    }
    m_theme_name = orig_name;
    m_author = "Sphaira"; // Default author
    
    LoadImageFile();
}

Menu::~Menu() {
    FreeImage();
}

void Menu::LoadImageFile() {
    SetAction(Button::A, Action{"Select Target"_i18n, [this](){
        DisplayTargetSelector();
    }});
    SetAction(Button::X, Action{"Theme Name"_i18n, [this](){
        EditThemeName();
    }});
    SetAction(Button::Y, Action{"Author"_i18n, [this](){
        EditAuthor();
    }});
    SetAction(Button::START, Action{"Generate Theme"_i18n, [this](){
        GenerateThemeCallback();
    }});

    const auto ext = std::string_view{m_path}.substr(std::string_view{m_path}.find_last_of('.') + 1);
    const bool is_jpeg = (ext == "jpg" || ext == "jpeg" || ext == "JPG" || ext == "JPEG");
    
    const auto data = ImageLoadFromFile(m_path, is_jpeg ? ImageFlag_JPEG : ImageFlag_None);
    if (!data.data.empty()) {
        m_raw_w = data.w;
        m_raw_h = data.h;
        m_raw_image_data = std::move(data.data);
        
        m_image_w = m_raw_w;
        m_image_h = m_raw_h;
        
        m_image = nvgCreateImageRGBA(App::GetVg(), m_image_w, m_image_h, 0, m_raw_image_data.data());
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
    m_raw_image_data.clear();
}

void Menu::ResetImageView() {
    m_zoom = 1.f;
    m_pan_x = 0.f;
    m_pan_y = 0.f;
}

void Menu::ZoomImage(float factor) {
    m_zoom = std::clamp(m_zoom * factor, 1.f, 8.f);
    ClampPan();
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

    const float fill_scale = std::max(SCREEN_WIDTH / m_image_w, SCREEN_HEIGHT / m_image_h);
    const float scale = fill_scale * m_zoom;
    const float image_w = m_image_w * scale;
    const float image_h = m_image_h * scale;
    const float max_pan_x = std::max(0.f, (image_w - SCREEN_WIDTH) / 2.f);
    const float max_pan_y = std::max(0.f, (image_h - SCREEN_HEIGHT) / 2.f);

    m_pan_x = std::clamp(m_pan_x, -max_pan_x, max_pan_x);
    m_pan_y = std::clamp(m_pan_y, -max_pan_y, max_pan_y);
}

void Menu::EditThemeName() {
    std::string out;
    if (R_SUCCEEDED(swkbd::ShowText(out, "Set Theme Name"_i18n.c_str(), m_theme_name.c_str())) && !out.empty()) {
        m_theme_name = out;
    }
}

void Menu::EditAuthor() {
    std::string out;
    if (R_SUCCEEDED(swkbd::ShowText(out, "Set Author Name"_i18n.c_str(), m_author.c_str())) && !out.empty()) {
        m_author = out;
    }
}

void Menu::DisplayTargetSelector() {
    std::vector<std::string> items = {
        "Home Menu (ResidentMenu)"_i18n,
        "Lock Screen (Entrance)"_i18n,
        "All Apps (Flaunch)"_i18n,
        "Settings (Set)"_i18n,
        "User Page (MyPage)"_i18n,
        "News (Notification)"_i18n,
        "Player Select (Psl)"_i18n
    };
    
    std::vector<std::string> targets = {
        "home", "lock", "apps", "set", "user", "news", "psl"
    };

    App::Push<PopupList>(
        "Select Theme Target"_i18n, items, [this, targets](auto op_index){
            if (op_index) {
                m_target = targets[*op_index];
            }
        }
    );
}

Result Menu::GenerateTheme(ProgressBox* pbox) {
    pbox->NewTransfer("Cropping image...");
    const float fill_scale = std::max(SCREEN_WIDTH / m_image_w, SCREEN_HEIGHT / m_image_h);
    const float S = fill_scale * m_zoom;
    
    int crop_w = static_cast<int>(SCREEN_WIDTH / S);
    int crop_h = static_cast<int>(SCREEN_HEIGHT / S);
    int crop_x = static_cast<int>(m_raw_w / 2.f - (SCREEN_WIDTH / 2.f + m_pan_x) / S);
    int crop_y = static_cast<int>(m_raw_h / 2.f - (SCREEN_HEIGHT / 2.f + m_pan_y) / S);
    
    crop_x = std::clamp(crop_x, 0, m_raw_w - crop_w);
    crop_y = std::clamp(crop_y, 0, m_raw_h - crop_h);
    
    auto crop_res = ImageCrop(m_raw_image_data, m_raw_w, m_raw_h, crop_x, crop_y, crop_w, crop_h);
    if (crop_res.data.empty()) {
        return -1;
    }
    
    pbox->NewTransfer("Resizing image to 1280x720...");
    auto resize_res = ImageResize(crop_res.data, crop_res.w, crop_res.h, 1280, 720);
    if (resize_res.data.empty()) {
        return -1;
    }
    
    pbox->NewTransfer("Converting to JPEG...");
    auto jpg_res = ImageConvertToJpg(resize_res.data, 1280, 720);
    if (jpg_res.data.empty()) {
        return -1;
    }
    
    pbox->NewTransfer("Generating manifest...");
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "Version", 17);
    yyjson_mut_obj_add_str(doc, root, "Target", m_target.c_str());
    yyjson_mut_obj_add_str(doc, root, "Author", m_author.c_str());
    yyjson_mut_obj_add_str(doc, root, "ThemeName", m_theme_name.c_str());
    
    char theme_id[9];
    for (int i = 0; i < 8; ++i) {
        std::snprintf(theme_id + i, 2, "%x", rand() % 16);
    }
    theme_id[8] = '\0';
    yyjson_mut_obj_add_str(doc, root, "Id", theme_id);
    
    char *json_str_ptr = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, nullptr);
    std::string info_json(json_str_ptr);
    free(json_str_ptr);
    yyjson_mut_doc_free(doc);
    
    pbox->NewTransfer("Packing NXTtheme archive...");
    
    std::string orig_name = m_path.toString();
    const auto last_slash = orig_name.find_last_of('/');
    if (last_slash != std::string::npos) {
        orig_name = orig_name.substr(last_slash + 1);
    }
    const auto last_dot = orig_name.find_last_of('.');
    if (last_dot != std::string::npos) {
        orig_name = orig_name.substr(0, last_dot);
    }
    
    orig_name.erase(std::remove_if(orig_name.begin(), orig_name.end(), [](char c) {
        return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    }), orig_name.end());
    
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char date_str[64];
    std::strftime(date_str, sizeof(date_str), "%d-%m-%y_%H-%M", tm);
    
    fs::FsNativeSd().CreateDirectoryRecursively("/themes");
    
    fs::FsPath out_path;
    std::snprintf(out_path, sizeof(out_path), "/themes/%s_%s.nxtheme", orig_name.c_str(), date_str);
    
    zlib_filefunc64_def file_funcs;
    sphaira::mz::FileFuncStdio(&file_funcs);
    zipFile zf = zipOpen2_64(out_path.s, APPEND_STATUS_CREATE, NULL, &file_funcs);
    if (!zf) {
        return Result_ZipOpen2_64;
    }
    
    zip_fileinfo zi = {};
    int rc = zipOpenNewFileInZip(zf, "info.json", &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    if (rc != ZIP_OK) {
        zipClose(zf, NULL);
        return Result_ZipOpenNewFileInZip;
    }
    zipWriteInFileInZip(zf, info_json.data(), info_json.size());
    zipCloseFileInZip(zf);
    
    rc = zipOpenNewFileInZip(zf, "image.jpg", &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
    if (rc != ZIP_OK) {
        zipClose(zf, NULL);
        return Result_ZipOpenNewFileInZip;
    }
    zipWriteInFileInZip(zf, jpg_res.data.data(), jpg_res.data.size());
    zipCloseFileInZip(zf);
    
    zipClose(zf, NULL);
    
    pbox->NewTransfer("Theme saved successfully");
    
    m_path = out_path;
    return 0;
}

void Menu::GenerateThemeCallback() {
    App::Push<ProgressBox>(0, "Generating Theme..."_i18n, m_theme_name, [this](auto pbox) -> Result {
        return GenerateTheme(pbox);
    }, [this](Result rc){
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to generate theme!"_i18n);
            return;
        }
        
        App::Notify("Theme generated!"_i18n);
        
        if (HasNro()) {
            m_state = State_Confirm;
            m_holding_a = false;
            m_holding_y = false;
            m_hold_progress = 0.f;
            
            SetAction(Button::A, Action{[](){}});
            SetAction(Button::X, Action{[](){}});
            SetAction(Button::Y, Action{[](){}});
            SetAction(Button::START, Action{[](){}});
            
            SetAction(Button::B, Action{"Back"_i18n, [this](){
                m_state = State_Crop;
                LoadImageFile();
            }});
        } else {
            SetPop();
        }
    });
}

void Menu::InstallThemeAction(bool reboot) {
    std::string args = nro_add_arg_file(m_path.s) + " --auto-install";
    if (reboot) {
        args += " --reboot";
    }
    log_write("theme creator nro: %s\n", GetNroPath());
    log_write("theme creator args: %s\n", args.c_str());
    
    const auto rc = nro_launch(GetNroPath(), args);
    App::PushErrorBox(rc, "Failed to launch NXThemesInstaller.nro"_i18n);
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_state == State_Crop) {
        MenuBase::Update(controller, touch);
        
        const auto zoom_in = controller->GotDown(Button::R2) || controller->GotHeld(Button::R2);
        const auto zoom_out = controller->GotDown(Button::L2) || controller->GotHeld(Button::L2);
        
        if (zoom_in) {
            ZoomImage(1.05f);
        } else if (zoom_out) {
            ZoomImage(1.f / 1.05f);
        }
        
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
    } else if (m_state == State_Confirm) {
        MenuBase::Update(controller, touch);

        const bool hold_a = controller->GotHeld(Button::A);
        const bool hold_y = controller->GotHeld(Button::Y);

        if (controller->GotDown(Button::A)) {
            m_hold_start = std::chrono::steady_clock::now();
            m_holding_a = true;
            m_holding_y = false;
        } else if (controller->GotDown(Button::Y)) {
            m_hold_start = std::chrono::steady_clock::now();
            m_holding_y = true;
            m_holding_a = false;
        }

        if (m_holding_a && hold_a) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_hold_start
            ).count();
            m_hold_progress = std::min(1.f, elapsed / 3000.f);
            if (elapsed >= 3000) {
                m_holding_a = false;
                m_hold_progress = 0.f;
                InstallThemeAction(false);
            }
        } else if (m_holding_y && hold_y) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_hold_start
            ).count();
            m_hold_progress = std::min(1.f, elapsed / 3000.f);
            if (elapsed >= 3000) {
                m_holding_y = false;
                m_hold_progress = 0.f;
                InstallThemeAction(true);
            }
        } else {
            m_holding_a = false;
            m_holding_y = false;
            m_hold_progress = 0.f;
        }
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);

    if (!m_image || !m_image_w || !m_image_h) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to load image"_i18n.c_str());
        Widget::Draw(vg, theme);
        return;
    }

    const float fill_scale = std::max(SCREEN_WIDTH / m_image_w, SCREEN_HEIGHT / m_image_h);
    const float scale = fill_scale * m_zoom;
    const float image_w = m_image_w * scale;
    const float image_h = m_image_h * scale;
    const float image_x = (SCREEN_WIDTH - image_w) / 2.f + m_pan_x;
    const float image_y = (SCREEN_HEIGHT - image_h) / 2.f + m_pan_y;

    gfx::drawImage(vg, image_x, image_y, image_w, image_h, m_image, 5);

    if (m_state == State_Crop) {
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, SCREEN_WIDTH, 70);
        nvgRect(vg, 0, SCREEN_HEIGHT - 60, SCREEN_WIDTH, 60);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);

        char top_text[256];
        std::snprintf(top_text, sizeof(top_text), "Theme: %s  |  Author: %s  |  Target: %s", m_theme_name.c_str(), m_author.c_str(), m_target.c_str());
        gfx::drawText(vg, 30, 45, 24.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), top_text, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        gfx::drawText(vg, 30, SCREEN_HEIGHT - 30, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), "LS/DPAD: Move  |  ZL/ZR: Zoom  |  A: Target  |  X: Name  |  Y: Author  |  +: Generate", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    } else if (m_state == State_Confirm) {
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
        nvgFill(vg);

        const float dialog_w = 700.f;
        const float dialog_h = 350.f;
        const float dialog_x = (SCREEN_WIDTH - dialog_w) / 2.f;
        const float dialog_y = (SCREEN_HEIGHT - dialog_h) / 2.f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, dialog_x, dialog_y, dialog_w, dialog_h, 15.f);
        nvgFillColor(vg, nvgRGBA(20, 20, 20, 245));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, dialog_x, dialog_y, dialog_w, dialog_h, 15.f);
        nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        nvgStrokeWidth(vg, 3.f);
        nvgStroke(vg);

        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, dialog_y + 50.f, 32.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "Theme Created Successfully!"_i18n.c_str());

        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, dialog_y + 130.f, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Hold A (3s): Install Theme"_i18n.c_str());
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, dialog_y + 180.f, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Hold Y (3s): Install & Reboot"_i18n.c_str());
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, dialog_y + 230.f, 20.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Press B: Cancel"_i18n.c_str());

        if (m_hold_progress > 0.f) {
            const float bar_w = 400.f;
            const float bar_h = 10.f;
            const float bar_x = (SCREEN_WIDTH - bar_w) / 2.f;
            const float bar_y = dialog_y + 280.f;

            nvgBeginPath(vg);
            nvgRoundedRect(vg, bar_x, bar_y, bar_w, bar_h, bar_h / 2.f);
            nvgFillColor(vg, nvgRGBA(50, 50, 50, 255));
            nvgFill(vg);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, bar_x, bar_y, bar_w * m_hold_progress, bar_h, bar_h / 2.f);
            nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
            nvgFill(vg);
        }
    }

    Widget::Draw(vg, theme);
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

} // namespace sphaira::ui::menu::theme_creator
