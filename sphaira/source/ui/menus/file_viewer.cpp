#include "ui/menus/file_viewer.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <cctype>

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
    return IsJpegExtension(ext) || ExtensionEquals(ext, "png") || ExtensionEquals(ext, "bmp");
}

} // namespace

Menu::Menu(const fs::FsPath& path) : MenuBase{path, MenuFlag_None}, m_path{path} {
    SetAction(Button::B, Action{"Back"_i18n, [this](){
        SetPop();
    }});

    const auto ext = PathExtension(m_path);
    m_is_image_file = IsImageExtension(ext);

    if (m_is_image_file) {
        const auto data = ImageLoadFromFile(m_path, IsJpegExtension(ext) ? ImageFlag_JPEG : ImageFlag_None);
        if (!data.data.empty()) {
            m_image_w = data.w;
            m_image_h = data.h;
            m_image = nvgCreateImageRGBA(App::GetVg(), data.w, data.h, 0, data.data.data());
        }

        return;
    }

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

Menu::~Menu() {
    if (m_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_scroll_text) {
        m_scroll_text->Update(controller, touch);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_is_image_file) {
        if (!m_image || !m_image_w || !m_image_h) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to load image"_i18n.c_str());
            return;
        }

        const Vec4 bounds{60.f, 110.f, SCREEN_WIDTH - 120.f, 500.f};
        const auto scale = std::min(bounds.w / static_cast<float>(m_image_w), bounds.h / static_cast<float>(m_image_h));
        const auto image_w = static_cast<float>(m_image_w) * scale;
        const auto image_h = static_cast<float>(m_image_h) * scale;
        const auto image_x = bounds.x + (bounds.w - image_w) / 2.f;
        const auto image_y = bounds.y + (bounds.h - image_h) / 2.f;

        gfx::drawImage(vg, image_x, image_y, image_w, image_h, m_image, 5);
        return;
    }

    if (m_scroll_text) {
        m_scroll_text->Draw(vg, theme);
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

} // namespace sphaira::ui::menu::fileview
