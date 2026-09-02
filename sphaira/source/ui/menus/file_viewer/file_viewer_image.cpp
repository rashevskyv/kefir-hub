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
void Menu::SetImagePickCallback(std::function<bool(const fs::FsPath&)> cb) {
    m_image_pick = std::move(cb);
    if (m_is_image_file) {
        UpdateImageAAction();
    }
}

void Menu::LoadImageFile() {
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
        SetAction(Button::LEFT, Action{"Prev / Next Image"_i18n, "\uE0ED / \uE0EE", [this](){
            NextImage(-1);
        }});
        SetAction(Button::RIGHT, Action{"", [this](){
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
        if (auto* vg = App::GetVg()) {
            nvgDeleteImage(vg, m_image);
        }
        m_image = 0;
    }

    m_image_w = 0;
    m_image_h = 0;
}

void Menu::ResetImageView() {
    m_viewport.Reset();
    UpdateImageSubHeading();
    UpdateImageAAction();
}

void Menu::UpdateImageAAction() {
    if (!m_is_image_file) {
        return;
    }

    // Picker A is select unless zoomed (then Fit, next A selects). Viewer A always fits.
    if (m_image_pick && !m_viewport.IsZoomed()) {
        SetAction(Button::A, Action{"Use this image"_i18n, [this](){
            if (m_image_pick && m_image_pick(m_path)) {
                SetPop();
            }
        }});
        return;
    }

    SetAction(Button::A, Action{"Fit Image"_i18n, [this](){
        ResetImageView();
    }});
}

void Menu::NextImage(s64 direction) {
    if (m_image_paths.empty()) {
        return;
    }

    const auto app = App::GetApp();
    if (app && (app->m_controller.GotHeld(Button::L2) || app->m_controller.GotDown(Button::L2))) {
        return;
    }
    if (m_viewport.IsZoomed()) {
        return;
    }

    const auto count = static_cast<s64>(m_image_paths.size());
    m_image_index = (m_image_index + direction + count) % count;
    m_path = m_image_paths[m_image_index];
    LoadCurrentFile();
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

    if (m_image_pick) {
        options->Add<SidebarEntryCallback>("Use this image"_i18n, [this](){
            App::PopToMenu();
            if (m_image_pick && m_image_pick(m_path)) {
                SetPop();
            }
        }, "Use the image on screen as the selection."_i18n);
        options->Add<SidebarEntryCallback>("Fit Image"_i18n, [this](){
            App::PopToMenu();
            ResetImageView();
        }, "Reset zoom and pan."_i18n);
        App::Push(std::move(options));
        return;
    }

    options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
        App::PopToMenu();
        DeleteImages();
    }, "Permanently delete the selected image(s) from the SD card."_i18n)->SetIcon(ActionIcon::Delete);

    options->Add<SidebarEntryCallback>("Compress to zip"_i18n, [this](){
        App::PopToMenu();
        ZipImages("");
    }, "Compress the selected image(s) into a zip archive."_i18n)->SetIcon(ActionIcon::Compress);

    options->Add<SidebarEntryCallback>("Create Switch Theme"_i18n, [this](){
        App::PopToMenu();
        CreateSwitchTheme();
    }, "Use the selected image to create a custom Switch theme."_i18n)->SetIcon(ActionIcon::Layout);

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
} // namespace sphaira::ui::menu::fileview
