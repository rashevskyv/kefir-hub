#include "ui/menus/ownfoil_title_internal.hpp"

namespace sphaira::ui::menu::ownfoil {

ScreenshotViewer::ScreenshotViewer(const sphaira::ownfoil::Config& config, const std::string& id, std::vector<api::ShopImage> images, std::vector<int> previews, s64 index)
: m_config{config}
, m_id{id}
, m_images{std::move(images)}
, m_previews{std::move(previews)} {
    SetAction(Button::B, Action{"Close"_i18n, [this]{
        SetPop();
    }});

    Show(index);
}

ScreenshotViewer::~ScreenshotViewer() {
    m_stop.request_stop();
    m_loader.reset();
    DeleteTexture(m_image);
}

void ScreenshotViewer::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);

    // left and right step through the images, as they do on the page.
    const auto index = m_index;
    if (controller->GotDown(Button::DPAD_LEFT | Button::LS_LEFT)) {
        Show(m_index - 1);
    } else if (controller->GotDown(Button::DPAD_RIGHT | Button::LS_RIGHT)) {
        Show(m_index + 1);
    }

    if (m_index != index) {
        App::PlaySoundEffect(SoundEffect_Scroll);
    }
}

void ScreenshotViewer::Show(s64 index) {
    const auto count = static_cast<s64>(m_images.size());
    index = std::clamp<s64>(index, 0, std::max<s64>(0, count - 1));
    if (index == m_index || index >= count) {
        return;
    }
    m_index = index;
    log_write("[OWNFOIL] viewer showing image %ld of %ld\n", m_index + 1, count);

    // whatever was loading belongs to the image being left. its request stops
    // on the token, so the join is short.
    m_stop.request_stop();
    m_loader.reset();
    m_stop = std::stop_source{};
    DeleteTexture(m_image);
    m_image = 0;

    const auto token = m_stop.get_token();
    const auto image = m_images[m_index];
    m_loader = std::make_unique<utils::Async>([this, token, image, index](){
        auto data = FetchImage(m_config, m_id, image, "screen", token);
        if (data.data.empty() || token.stop_requested()) {
            return;
        }

        // the shop fits its own copies to the screen, but a hotlink is whatever the
        // eshop serves - a 1920x1080 banner, say - so it is fitted here rather than
        // uploaded at a size nothing will draw.
        if (data.w > SCREEN_WIDTH || data.h > SCREEN_HEIGHT) {
            const auto scale = std::min(static_cast<float>(SCREEN_WIDTH) / data.w, static_cast<float>(SCREEN_HEIGHT) / data.h);
            auto fitted = ImageResize(data.data, data.w, data.h, static_cast<int>(data.w * scale), static_cast<int>(data.h * scale));
            if (!fitted.data.empty()) {
                data = std::move(fitted);
            }
        }

        evman::push(evman::CallbackEventData{[this, index, data = std::move(data)]() {
            if (index == m_index && !m_image) {
                m_image = CreateTexture(data);
            }
        }, token}, false);
    });
}

void ScreenshotViewer::Draw(NVGcontext* vg, Theme* theme) {
    gfx::drawRect(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, nvgRGB(0, 0, 0));

    // the page's own copy stands in until the full one lands.
    const auto image = m_image ? m_image : (m_index >= 0 && m_index < static_cast<s64>(m_previews.size()) ? m_previews[m_index] : 0);
    if (image) {
        int w{}, h{};
        nvgImageSize(vg, image, &w, &h);
        if (w > 0 && h > 0) {
            const auto scale = std::min(static_cast<float>(SCREEN_WIDTH) / w, static_cast<float>(SCREEN_HEIGHT) / h);
            const auto dw = w * scale;
            const auto dh = h * scale;
            gfx::drawImage(vg, (SCREEN_WIDTH - dw) / 2.f, (SCREEN_HEIGHT - dh) / 2.f, dw, dh, image);
        }
    }

    // a shade under the hints, so they read over any image.
    constexpr float shade_h = 110.f;
    const auto shade = nvgLinearGradient(vg, 0, SCREEN_HEIGHT - shade_h, 0, SCREEN_HEIGHT, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 200));
    gfx::drawRect(vg, 0, SCREEN_HEIGHT - shade_h, SCREEN_WIDTH, shade_h, shade);

    if (m_images.size() > 1) {
        gfx::drawTextArgs(vg, 80.f, 675.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%ld / %zu", m_index + 1, m_images.size());
    }

    Widget::Draw(vg, theme);
}

} // namespace sphaira::ui::menu::ownfoil
