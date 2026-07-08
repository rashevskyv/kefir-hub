#include "ui/menus/file_viewer.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "minizip_helper.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/theme_creator.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "web.hpp"

#include <minizip/zip.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <utility>

namespace sphaira::ui::menu::fileview {
namespace {

auto PathExtension(const fs::FsPath& path) -> std::string_view {
    const std::string_view view{path};
    const auto slash = view.find_last_of('/');
    const auto dot = view.find_last_of('.');
    if (dot == view.npos || (slash != view.npos && dot < slash)) {
        return {};
    }

    return view.substr(dot + 1);
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

auto ExtensionEquals(std::string_view a, std::string_view b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

auto IsJpegExtension(std::string_view ext) -> bool {
    return ExtensionEquals(ext, "jpg") || ExtensionEquals(ext, "jpeg");
}

auto IsImageExtension(std::string_view ext) -> bool {
    return IsJpegExtension(ext) || ExtensionEquals(ext, "png") || ExtensionEquals(ext, "bmp") || ExtensionEquals(ext, "gif");
}

auto ImageBounds(bool fullscreen) -> Vec4 {
    if (fullscreen) {
        return {0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT};
    }

    return {60.f, 110.f, SCREEN_WIDTH - 120.f, 500.f};
}

} // namespace

Menu::Menu(const fs::FsPath& path) : MenuBase{path, MenuFlag_None}, m_path{path} {
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    LoadCurrentFile();
}

Menu::Menu(const fs::FsPath& path, std::vector<fs::FsPath> image_paths, s64 image_index, std::vector<std::string> image_titles)
: MenuBase{path, MenuFlag_None}
, m_path{path}
, m_image_paths{std::move(image_paths)}
, m_image_titles{std::move(image_titles)}
, m_image_index{image_index} {
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
    m_file.Close();
    m_file_size = 0;
    m_file_offset = 0;
    m_is_image_file = IsImageExtension(PathExtension(m_path));

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

    if (m_is_image_file) {
        LoadImageFile();
    } else {
        LoadTextFile();
    }
}

void Menu::LoadTextFile() {
    std::string buf;
    if (R_SUCCEEDED(m_fs.OpenFile(m_path, FsOpenMode_Read, &m_file))) {
        m_file.GetSize(&m_file_size);
        buf.resize(m_file_size + 1);

        u64 read_bytes;
        m_file.Read(m_file_offset, buf.data(), buf.size(), 0, &read_bytes);
        buf[m_file_size] = '\0';
    }

    m_scroll_text = std::make_unique<ScrollableText>(buf, 0, 120, 500, 1150-110, 18);
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

    const auto ext = PathExtension(m_path);
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
    });

    options->Add<SidebarEntryCallback>("Compress to zip"_i18n, [this](){
        App::PopToMenu();
        ZipImages("");
    });

    options->Add<SidebarEntryCallback>("Create Switch Theme"_i18n, [this](){
        App::PopToMenu();
        CreateSwitchTheme();
    });

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
                if (!m_fs.FileExists(zip_out)) {
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
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    if (m_is_image_file) {
        DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);

        if (!m_fullscreen) {
            const auto title = GetDisplayName();
            gfx::drawText(vg, 80, 70, 28.f, theme->GetColour(ThemeEntryID_TEXT), title.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
            gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
            gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        }

        if (!m_image || !m_image_w || !m_image_h) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to load image"_i18n.c_str());
            if (!m_fullscreen) {
                Widget::Draw(vg, theme);
            }
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

        if (!m_fullscreen) {
            Widget::Draw(vg, theme);
        }
        return;
    }

    MenuBase::Draw(vg, theme);

    if (m_scroll_text) {
        m_scroll_text->Draw(vg, theme);
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

} // namespace sphaira::ui::menu::fileview
