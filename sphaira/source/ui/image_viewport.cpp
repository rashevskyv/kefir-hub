#include "ui/nvg_util.hpp"
#include <algorithm>
#include <cmath>

namespace sphaira::ui::gfx {

void ImageViewport::Reset() {
    m_zoom = 1.f;
    m_pan_x = 0.f;
    m_pan_y = 0.f;
    m_prev_touch_x = 0;
    m_prev_touch_y = 0;
    m_was_touching = false;
    m_pinch_active = false;
}

void ImageViewport::SetZoom(float zoom, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    m_zoom = std::clamp(zoom, 1.f, 8.f);
    ClampPan(image_w, image_h, viewport, fit);
}

void ImageViewport::SetPan(float pan_x, float pan_y, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    m_pan_x = pan_x;
    m_pan_y = pan_y;
    ClampPan(image_w, image_h, viewport, fit);
}

auto ImageViewport::GetScale(int image_w, int image_h, const Vec4& viewport, ImageFit fit) const -> float {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        return 0.f;
    }

    const float scale_x = viewport.w / static_cast<float>(image_w);
    const float scale_y = viewport.h / static_cast<float>(image_h);
    const float base_scale = (fit == ImageFit::Cover) ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);
    return base_scale * m_zoom;
}

auto ImageViewport::GetImageRect(int image_w, int image_h, const Vec4& viewport, ImageFit fit) const -> Vec4 {
    const float scale = GetScale(image_w, image_h, viewport, fit);
    if (scale <= 0.f) {
        return {viewport.x, viewport.y, 0.f, 0.f};
    }

    const float w = static_cast<float>(image_w) * scale;
    const float h = static_cast<float>(image_h) * scale;
    const float x = viewport.x + (viewport.w - w) * 0.5f + m_pan_x;
    const float y = viewport.y + (viewport.h - h) * 0.5f + m_pan_y;
    return {x, y, w, h};
}

void ImageViewport::Zoom(float factor, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    const float center_x = viewport.x + viewport.w * 0.5f;
    const float center_y = viewport.y + viewport.h * 0.5f;
    ZoomAt(factor, center_x, center_y, image_w, image_h, viewport, fit);
}

void ImageViewport::ZoomAt(float factor, float anchor_x, float anchor_y, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        return;
    }

    const float old_zoom = m_zoom;
    const float new_zoom = std::clamp(old_zoom * factor, 1.f, 8.f);
    if (old_zoom <= 0.f) {
        m_zoom = new_zoom;
        ClampPan(image_w, image_h, viewport, fit);
        return;
    }

    const float effective_factor = new_zoom / old_zoom;
    const float scale_x = viewport.w / static_cast<float>(image_w);
    const float scale_y = viewport.h / static_cast<float>(image_h);
    const float base_scale = (fit == ImageFit::Cover) ? std::max(scale_x, scale_y) : std::min(scale_x, scale_y);

    const float old_scaled_w = static_cast<float>(image_w) * base_scale * old_zoom;
    const float old_scaled_h = static_cast<float>(image_h) * base_scale * old_zoom;
    const float old_image_x = viewport.x + (viewport.w - old_scaled_w) * 0.5f + m_pan_x;
    const float old_image_y = viewport.y + (viewport.h - old_scaled_h) * 0.5f + m_pan_y;

    const float new_scaled_w = static_cast<float>(image_w) * base_scale * new_zoom;
    const float new_scaled_h = static_cast<float>(image_h) * base_scale * new_zoom;
    const float new_image_x = anchor_x + (old_image_x - anchor_x) * effective_factor;
    const float new_image_y = anchor_y + (old_image_y - anchor_y) * effective_factor;

    m_zoom = new_zoom;
    m_pan_x = new_image_x - (viewport.x + (viewport.w - new_scaled_w) * 0.5f);
    m_pan_y = new_image_y - (viewport.y + (viewport.h - new_scaled_h) * 0.5f);

    ClampPan(image_w, image_h, viewport, fit);
}

void ImageViewport::Pan(float dx, float dy, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    m_pan_x += dx;
    m_pan_y += dy;
    ClampPan(image_w, image_h, viewport, fit);
}

void ImageViewport::ClampPan(int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        m_pan_x = 0.f;
        m_pan_y = 0.f;
        return;
    }

    const float scale = GetScale(image_w, image_h, viewport, fit);
    const float w = static_cast<float>(image_w) * scale;
    const float h = static_cast<float>(image_h) * scale;
    const float max_pan_x = std::max(0.f, (w - viewport.w) * 0.5f);
    const float max_pan_y = std::max(0.f, (h - viewport.h) * 0.5f);

    m_pan_x = std::clamp(m_pan_x, -max_pan_x, max_pan_x);
    m_pan_y = std::clamp(m_pan_y, -max_pan_y, max_pan_y);
}

void ImageViewport::Update(Controller* controller, TouchInfo* touch, int image_w, int image_h, const Vec4& viewport, ImageFit fit) {
    if (image_w <= 0 || image_h <= 0 || viewport.w <= 0.f || viewport.h <= 0.f) {
        return;
    }

    if (controller) {
        const auto zoom_modifier = controller->GotDown(Button::L2) || controller->GotHeld(Button::L2);
        if (zoom_modifier) {
            const auto zoom_in = controller->GotDown(Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                controller->GotHeld(Button::DPAD_UP | Button::LS_UP | Button::RS_UP);
            const auto zoom_out = controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                controller->GotHeld(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN);

            if (zoom_in) {
                Zoom(1.05f, image_w, image_h, viewport, fit);
            } else if (zoom_out) {
                Zoom(1.f / 1.05f, image_w, image_h, viewport, fit);
            }
        } else {
            constexpr float PAN_SPEED = 12.f;
            if (controller->GotDown(Button::DPAD_UP | Button::LS_UP | Button::RS_UP) ||
                controller->GotHeld(Button::DPAD_UP | Button::LS_UP | Button::RS_UP)) {
                Pan(0.f, PAN_SPEED, image_w, image_h, viewport, fit);
            }
            if (controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN) ||
                controller->GotHeld(Button::DPAD_DOWN | Button::LS_DOWN | Button::RS_DOWN)) {
                Pan(0.f, -PAN_SPEED, image_w, image_h, viewport, fit);
            }
            if (controller->GotDown(Button::DPAD_LEFT | Button::LS_LEFT | Button::RS_LEFT) ||
                controller->GotHeld(Button::DPAD_LEFT | Button::LS_LEFT | Button::RS_LEFT)) {
                Pan(PAN_SPEED, 0.f, image_w, image_h, viewport, fit);
            }
            if (controller->GotDown(Button::DPAD_RIGHT | Button::LS_RIGHT | Button::RS_RIGHT) ||
                controller->GotHeld(Button::DPAD_RIGHT | Button::LS_RIGHT | Button::RS_RIGHT)) {
                Pan(-PAN_SPEED, 0.f, image_w, image_h, viewport, fit);
            }
        }
    }

    if (touch) {
        if (touch->touch_count >= 2) {
            m_pinch_active = true;
            m_was_touching = false;
            if (touch->is_pinch) {
                const float clamped_scale = std::clamp(touch->pinch_scale, 0.9f, 1.1f);
                ZoomAt(clamped_scale, touch->pinch_x, touch->pinch_y, image_w, image_h, viewport, fit);
            }
        } else if (!touch->is_touching || touch->touch_count == 0) {
            m_was_touching = false;
            m_pinch_active = false;
        } else if (!m_pinch_active) {
            if (m_was_touching) {
                const float dx = static_cast<float>(static_cast<s32>(touch->cur.x) - m_prev_touch_x);
                const float dy = static_cast<float>(static_cast<s32>(touch->cur.y) - m_prev_touch_y);
                Pan(dx, dy, image_w, image_h, viewport, fit);
            }
            m_prev_touch_x = touch->cur.x;
            m_prev_touch_y = touch->cur.y;
            m_was_touching = true;
        }
    }
}

} // namespace sphaira::ui::gfx
