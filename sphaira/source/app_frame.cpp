#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/progress_box.hpp"

#include <switch.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace sphaira {

extern App* g_app;

void App::Poll() {
    m_controller.Reset();

    HidTouchScreenState state{};
    hidGetTouchScreenStates(&state, 1);
    m_touch_info.touch_count = state.count;
    m_touch_info.is_clicked = false;
    m_touch_info.is_end = false;
    m_touch_info.is_pinch = false;
    m_touch_info.pinch_delta = 0.f;
    m_touch_info.pinch_scale = 1.f;
    m_touch_info.pinch_x = 0.f;
    m_touch_info.pinch_y = 0.f;

    static float prev_pinch_dist = 0.f;

// todo: replace old touch code with gestures from below
#if 0
    static HidGestureState prev_gestures[17]{};
    HidGestureState gestures[17]{};
    const auto gesture_count = hidGetGestureStates(gestures, std::size(gestures));
    for (int i = (int)gesture_count - 1; i >= 0; i--) {
        bool found = false;
        for (int j = 0; j < gesture_count; j++) {
            if (gestures[i].type == prev_gestures[j].type && gestures[i].sampling_number == prev_gestures[j].sampling_number) {
                found = true;
                break;
            }
        }

        if (found) {
            continue;
        }

        auto gesture = gestures[i];
        if (gesture_count && gesture.type == HidGestureType_Touch) {
            log_write("[TOUCH] got gesture attr: %u direction: %u sampling_number: %zu context_number: %zu\n", gesture.attributes, gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Swipe) {
            log_write("[SWIPE] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Tap) {
            log_write("[TAP] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Press) {
            log_write("[PRESS] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Cancel) {
            log_write("[CANCEL] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Complete) {
            log_write("[COMPLETE] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Pan) {
            log_write("[PAN] got gesture direction: %u sampling_number: %zu context_number: %zu x: %d y: %d dx: %d dy: %d vx: %.2f vy: %.2f count: %d\n", gesture.direction, gesture.sampling_number, gesture.context_number, gesture.x, gesture.y, gesture.delta_x, gesture.delta_y, gesture.velocity_x, gesture.velocity_y, gesture.point_count);
        }
    }

    memcpy(prev_gestures, gestures, sizeof(gestures));
#endif

    if (state.count >= 2) {
        const float dx = static_cast<float>(static_cast<s32>(state.touches[0].x) - static_cast<s32>(state.touches[1].x));
        const float dy = static_cast<float>(static_cast<s32>(state.touches[0].y) - static_cast<s32>(state.touches[1].y));
        const float current_dist = std::sqrt(dx * dx + dy * dy);

        if (prev_pinch_dist > 0.f && current_dist > 0.f) {
            m_touch_info.pinch_delta = current_dist - prev_pinch_dist;
            m_touch_info.pinch_scale = current_dist / prev_pinch_dist;
            m_touch_info.is_pinch = true;
        }
        prev_pinch_dist = current_dist;
        m_touch_info.pinch_x = static_cast<float>(state.touches[0].x + state.touches[1].x) * 0.5f;
        m_touch_info.pinch_y = static_cast<float>(state.touches[0].y + state.touches[1].y) * 0.5f;
        m_touch_info.is_touching = true;
        m_touch_info.is_tap = false;
        m_touch_info.is_scroll = false;
        m_touch_info.cur = state.touches[0];
    } else {
        prev_pinch_dist = 0.f;
        if (state.count == 1 && !m_touch_info.is_touching) {
            m_touch_info.initial = m_touch_info.cur = state.touches[0];
            m_touch_info.is_touching = true;
            m_touch_info.is_tap = true;
        } else if (state.count == 1 && m_touch_info.is_touching) {
            m_touch_info.cur = state.touches[0];

            if (m_touch_info.is_tap &&
                (std::abs((s32)m_touch_info.initial.x - (s32)m_touch_info.cur.x) > 20 ||
                std::abs((s32)m_touch_info.initial.y - (s32)m_touch_info.cur.y) > 20)) {
                m_touch_info.is_tap = false;
                m_touch_info.is_scroll = true;
            }
        } else if (m_touch_info.is_touching) {
            m_touch_info.is_touching = false;
            m_touch_info.is_scroll = false;
            if (m_touch_info.is_tap) {
                m_touch_info.is_clicked = true;
            } else {
                m_touch_info.is_end = true;
            }
        }
    }

    // todo: better implement this to match hos
    if (!m_touch_info.is_touching && !m_touch_info.is_clicked) {
        padUpdate(&m_pad);
        m_controller.m_kdown = padGetButtonsDown(&m_pad);
        m_controller.m_kheld = padGetButtons(&m_pad);
        m_controller.m_kup = padGetButtonsUp(&m_pad);
        m_controller.m_stick_l = padGetStickPos(&m_pad, 0);
        m_controller.m_stick_r = padGetStickPos(&m_pad, 1);
        m_controller.UpdateButtonHeld(static_cast<u64>(Button::ANY_DIRECTION), m_delta_time);
    }
}

void App::Update() {
    std::shared_ptr<ui::menu::dbi::InstallSession> session;
    {
        SCOPED_MUTEX(&m_install_session_mutex);
        session = m_active_install_session;
    }

    const bool has_modal = !m_widgets.empty() && m_widgets.back()->IsModal() && !m_widgets.back()->IsMinimized();
    bool block_background_update = false;
    const bool active_install = session && !session->ShouldExit() && !session->ShouldPop()
        && (!m_active_transfer_pbox || (session->GetState() == ui::menu::dbi::State::Installing && !session->AllPackagesTerminal()));

    if (m_active_transfer_pbox) {
        // An install session is the sole input owner even while minimized; the
        // server box's worker keeps running without calling its input method.
        if (!active_install && !has_modal) {
            if (m_controller.GotDown(Button::R3)) {
                m_active_transfer_pbox->ToggleMinimized();
                App::PlaySoundEffect(SoundEffect_Focus);
                block_background_update = true;
            } else if (!m_active_transfer_pbox->IsMinimized()) {
                if (m_widgets.back()->IsMenu()) {
                    block_background_update = true;
                    m_active_transfer_pbox->Update(&m_controller, &m_touch_info);
                }
            }
        }

        // its worker thread signals exit once the transfer finishes; reclaim
        // it here rather than in the ProgressBox's own (never-called) Update().
        if (m_active_transfer_pbox->ShouldExit()) {
            m_active_transfer_pbox.reset();
            block_background_update = false;
        }
    }

    if (session && active_install) {
        constexpr float bw = 320.f;
        constexpr float bh = 40.f;
        constexpr float bx = SCREEN_WIDTH - bw - 20.f;
        constexpr float by = 12.f;
        const bool touch_badge = !has_modal && session->IsMinimized() && m_touch_info.is_clicked &&
                                 m_touch_info.in_range(Vec4(bx, by, bw, bh));

        if (!has_modal && (m_controller.GotDown(Button::R3) || touch_badge)) {
            session->ToggleMinimized();
            App::PlaySoundEffect(SoundEffect_Focus);
            session->Update(nullptr, nullptr);
            block_background_update = true;
        } else if (!has_modal && !session->IsMinimized()) {
            block_background_update = true;
            session->Update(&m_controller, &m_touch_info);
        } else {
            session->Update(nullptr, nullptr);
        }
    }

    if (session && (session->ShouldExit() || session->ShouldPop())) {
        {
            SCOPED_MUTEX(&m_install_session_mutex);
            if (m_active_install_session == session) {
                m_active_install_session.reset();
            }
        }
        session.reset();
        block_background_update = false;
    }

    if (!block_background_update) {
        if (m_widgets.back()->IsMinimized()) {
            constexpr float bw = 320.f;
            constexpr float bh = 40.f;
            constexpr float bx = SCREEN_WIDTH - bw - 20.f;
            constexpr float by = 12.f;
            const bool touch_badge = !has_modal && m_touch_info.is_clicked &&
                                     m_touch_info.in_range(Vec4(bx, by, bw, bh));

            if (!has_modal && (m_controller.GotDown(Button::R3) || touch_badge)) {
                m_widgets.back()->ToggleMinimized();
                App::PlaySoundEffect(SoundEffect_Focus);
                m_widgets.back()->Update(nullptr, nullptr);
            } else {
                m_widgets.back()->Update(nullptr, nullptr);

                ui::Widget* target = nullptr;
                for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
                    if (!(*it)->IsMinimized()) {
                        target = it->get();
                        break;
                    }
                }
                if (target) {
                    target->Update(&m_controller, &m_touch_info);
                }
            }
        } else {
            m_widgets.back()->Update(&m_controller, &m_touch_info);
        }
    }

    bool popped_at_least1 = false;
    while (true) {
        if (m_widgets.empty()) {
            log_write("[Mui] no widgets left, so we exit...");
            App::Exit();
            return;
        }

        if (m_widgets.back()->ShouldPop()) {
            log_write("popping widget\n");
            m_widgets.pop_back();
            popped_at_least1 = true;
        } else {
            break;
        }
    }

    if (!m_widgets.empty() && popped_at_least1) {
        m_widgets.back()->OnFocusGained();
    }

    if (!App::GetProgressActive()) {
        PollUsbStorage();
    }
}

void App::Draw() {
    const TimeStamp ts_wait;
    const auto slot = this->queue.acquireImage(this->swapchain);
    m_frame_wait_accum_ms += (double)ts_wait.GetNs() / 1e+6;
    this->queue.submitCommands(this->framebuffer_cmdlists[slot]);
    this->queue.submitCommands(this->render_cmdlist);
    nvgBeginFrame(this->vg, s_width, s_height, 1.f);
    nvgScale(vg, m_scale.x, m_scale.y);

    // a blocking ProgressBox on the widget stack (NAND/SD move, install)
    // already dims the screen. redrawing the games grid behind it fights ncm
    // for the GPU and freezes B/Stop. detached transfers (web server, MTP)
    // must still paint the menu underneath so the dim is actually translucent.
    std::shared_ptr<ui::menu::dbi::InstallSession> session;
    {
        SCOPED_MUTEX(&m_install_session_mutex);
        session = m_active_install_session;
    }
    const bool detached_blocks_under = session && session->BlocksDrawUnder();

    // find the last menu in the list, start drawing from there
    auto menu_it = m_widgets.rend();
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); it++) {
        const auto& p = *it;
        if (!p->IsHidden() && p->IsMenu() && !p->IsMinimized()) {
            menu_it = it;
            break;
        }
    }

    // the detached transfer box belongs directly above the menu, not above the
    // whole stack: anything pushed on top of it (notably the cancel
    // confirmation it raises itself) has to draw in front of it, not behind.
    bool transfer_drawn = false;

    // reverse itr so loop backwards to go forwarders.
    if (menu_it != m_widgets.rend()) {
        if (!detached_blocks_under) {
            // Start at the highest widget that blocks what is underneath it.
            // The blocker itself must still draw; skipping the whole stack is
            // what reduced DBI/local/USB install screens to the bare backdrop.
            auto draw_it = menu_it;
            for (auto it = menu_it; ; it--) {
                if (!(*it)->IsHidden() && (*it)->BlocksDrawUnder()) {
                    draw_it = it;
                }
                if (it == m_widgets.rbegin()) {
                    break;
                }
            }

            for (auto it = draw_it; ; it--) {
                const auto& p = *it;

                // draw all normal (non-modal) content/widgets on top of the menu first.
                if (!p->IsHidden() && !p->IsModal()) {
                    p->Draw(vg, &m_theme);
                }

                if (it == m_widgets.rbegin()) {
                    break;
                }
            }

            // draw standard header/footer chrome after normal content/widgets if no stacked widget opts out.
            bool allow_chrome = true;
            for (auto it = draw_it; ; it--) {
                const auto& p = *it;
                if (!p->IsHidden() && !p->WantsChrome()) {
                    allow_chrome = false;
                    break;
                }
                if (it == m_widgets.rbegin()) {
                    break;
                }
            }

            if (allow_chrome && !(*draw_it)->IsHidden()) {
                if (auto* chrome = (*draw_it)->GetChromeOwner()) {
                    chrome->DrawChrome(vg, &m_theme);
                }
            }
        }

        const bool draw_session = session && !session->ShouldExit() && !session->ShouldPop()
            && (!m_active_transfer_pbox || (session->GetState() == ui::menu::dbi::State::Installing && !session->AllPackagesTerminal()));
        if (draw_session) {
            session->Draw(vg, &m_theme);
            if (session->WantsChrome()) {
                session->DrawChrome(vg, &m_theme);
            }
            transfer_drawn = true;
        } else {
            if (m_active_transfer_pbox) {
                m_active_transfer_pbox->Draw(vg, &m_theme);
                transfer_drawn = true;
            }
        }

        // draw full-screen modal overlays on top.
        for (auto it = menu_it; ; it--) {
            const auto& p = *it;

            if (!p->IsHidden() && p->IsModal()) {
                p->Draw(vg, &m_theme);
            }

            if (it == m_widgets.rbegin()) {
                break;
            }
        }
    }

    // no menu on the stack to anchor it to, so it just goes on top.
    if (!transfer_drawn) {
        const bool draw_session = session && !session->ShouldExit() && !session->ShouldPop()
            && (!m_active_transfer_pbox || (session->GetState() == ui::menu::dbi::State::Installing && !session->AllPackagesTerminal()));
        if (draw_session) {
            session->Draw(vg, &m_theme);
            if (session->WantsChrome()) {
                session->DrawChrome(vg, &m_theme);
            }
        } else {
            if (m_active_transfer_pbox) {
                m_active_transfer_pbox->Draw(vg, &m_theme);
            }
        }
    }

    m_notif_manager.Draw(vg, &m_theme);

    nvgResetTransform(vg);
    nvgEndFrame(this->vg);
    this->queue.presentImage(this->swapchain, slot);
}

} // namespace sphaira
