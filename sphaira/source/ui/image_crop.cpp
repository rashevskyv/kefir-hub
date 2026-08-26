#include "ui/image_crop.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/widget.hpp"

#include <algorithm>
#include <cmath>

namespace sphaira::ui::image_crop {
namespace {

class CropEditor final : public Widget {
public:
    CropEditor(ImageResult img, std::string source, Callback on_apply)
    : m_raw_data{std::move(img.data)}
    , m_image_w{img.w}
    , m_image_h{img.h}
    , m_source{std::move(source)}
    , m_on_apply{std::move(on_apply)} {
        SetActions(
            std::make_pair(Button::A, Action{"Apply"_i18n, [this](){ Apply(); }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){ SetPop(); }})
        );

        if (!m_raw_data.empty() && m_image_w > 0 && m_image_h > 0) {
            m_image = nvgCreateImageRGBA(App::GetVg(), m_image_w, m_image_h, 0, m_raw_data.data());
        }
    }

    ~CropEditor() override {
        if (m_image > 0) {
            nvgDeleteImage(App::GetVg(), m_image);
        }
    }

    auto WantsChrome() const -> bool override { return false; }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);

        if (m_image_w <= 0 || m_image_h <= 0 || !m_image) {
            return;
        }

        const Vec4 crop_viewport{CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE};
        m_viewport.Update(controller, touch, m_image_w, m_image_h, crop_viewport, gfx::ImageFit::Cover);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);

        if (!m_image || m_image_w <= 0 || m_image_h <= 0) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to load image"_i18n.c_str());
            Widget::Draw(vg, theme);
            return;
        }

        const Vec4 crop_viewport{CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE};
        const auto img_rect = m_viewport.GetImageRect(m_image_w, m_image_h, crop_viewport, gfx::ImageFit::Cover);
        gfx::drawImage(vg, img_rect.x, img_rect.y, img_rect.w, img_rect.h, m_image, 0.f);

        const auto overlay_colour = nvgRGBA(0, 0, 0, 175);
        gfx::drawRect(vg, 0.f, 0.f, SCREEN_WIDTH, CROP_Y, overlay_colour);
        gfx::drawRect(vg, 0.f, CROP_Y + CROP_SIZE, SCREEN_WIDTH, SCREEN_HEIGHT - (CROP_Y + CROP_SIZE), overlay_colour);
        gfx::drawRect(vg, 0.f, CROP_Y, CROP_X, CROP_SIZE, overlay_colour);
        gfx::drawRect(vg, CROP_X + CROP_SIZE, CROP_Y, SCREEN_WIDTH - (CROP_X + CROP_SIZE), CROP_SIZE, overlay_colour);

        nvgBeginPath(vg);
        nvgRect(vg, CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE);
        nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        nvgStrokeWidth(vg, 2.5f);
        nvgStroke(vg);

        gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        gfx::drawRect(vg, 30.f, 646.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        m_scroll_title.Draw(vg, true, 70.f, 55.f, 400.f, 26.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT), "Crop Icon"_i18n);

        float source_bounds[4]{};
        nvgFontSize(vg, 18.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgTextBounds(vg, 0.f, 0.f, m_source.c_str(), nullptr, source_bounds);
        const auto source_width = std::min(520.f, std::max(0.f, source_bounds[2] - source_bounds[0]));
        m_scroll_source.Draw(vg, true, 1210.f - source_width, 55.f, source_width, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), m_source);

        gfx::drawText(vg, 70.f, 675.f, 18.f, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "\uE0E6 + \uE0EB/\uE0EC Zoom   \uE0EB/\uE0EC/\uE0ED/\uE0EE Move", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        Widget::Draw(vg, theme);
    }

private:
    static constexpr float CROP_SIZE = 512.f;
    static constexpr float CROP_X = (SCREEN_WIDTH - CROP_SIZE) / 2.f;
    static constexpr float CROP_Y = (SCREEN_HEIGHT - CROP_SIZE) / 2.f;

    void Apply() {
        if (m_raw_data.empty() || m_image_w <= 0 || m_image_h <= 0) {
            App::Notify("Failed to crop image"_i18n);
            SetPop();
            return;
        }

        const Vec4 crop_viewport{CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE};
        const auto img_rect = m_viewport.GetImageRect(m_image_w, m_image_h, crop_viewport, gfx::ImageFit::Cover);
        if (img_rect.w <= 0.f || img_rect.h <= 0.f) {
            App::Notify("Failed to crop image"_i18n);
            SetPop();
            return;
        }

        const float scale = img_rect.w / static_cast<float>(m_image_w);
        const int sx = std::clamp(static_cast<int>(std::round((crop_viewport.x - img_rect.x) / scale)), 0, m_image_w - 1);
        const int sy = std::clamp(static_cast<int>(std::round((crop_viewport.y - img_rect.y) / scale)), 0, m_image_h - 1);
        const int s_dim = std::clamp(static_cast<int>(std::round(crop_viewport.w / scale)), 1, std::min(m_image_w - sx, m_image_h - sy));

        auto cropped = ImageCrop(m_raw_data, m_image_w, m_image_h, sx, sy, s_dim, s_dim);
        if (cropped.data.empty()) {
            App::Notify("Failed to crop image"_i18n);
            SetPop();
            return;
        }

        auto resized = cropped.w == 256 && cropped.h == 256 ? std::move(cropped) : ImageResize(cropped.data, cropped.w, cropped.h, 256, 256);
        if (resized.data.empty() || resized.w != 256 || resized.h != 256) {
            App::Notify("Failed to resize icon"_i18n);
            SetPop();
            return;
        }

        auto jpg = ImageConvertToJpg(resized.data, 256, 256);
        if (jpg.data.empty()) {
            App::Notify("Failed to encode icon"_i18n);
            SetPop();
            return;
        }

        if (m_on_apply) {
            m_on_apply(std::move(jpg.data), std::move(m_source));
        }
        SetPop();
    }

    std::vector<u8> m_raw_data;
    int m_image_w{};
    int m_image_h{};
    int m_image{};
    std::string m_source;
    Callback m_on_apply;
    gfx::ImageViewport m_viewport{};
    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_source{};
};

} // namespace

void Show(ImageResult img, std::string source, Callback on_apply) {
    App::Push<CropEditor>(std::move(img), std::move(source), std::move(on_apply));
}

} // namespace sphaira::ui::image_crop
