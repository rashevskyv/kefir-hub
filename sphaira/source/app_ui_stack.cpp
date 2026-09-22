#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "net.hpp"
#include "ui/error_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/dbi_menu.hpp"
#include <switch.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sphaira {

extern App* g_app;

auto App::Push(std::unique_ptr<ui::Widget>&& widget) -> void {
    log_write("[Mui] pushing widget\n");

    if (!g_app->m_widgets.empty()) {
        g_app->m_widgets.back()->OnFocusLost();
    }

    log_write("doing focus gained\n");
    g_app->m_widgets.emplace_back(std::forward<decltype(widget)>(widget))->OnFocusGained();
    log_write("did it\n");
}

auto App::PushTransfer(std::unique_ptr<ui::ProgressBox>&& pbox) -> bool {
    pbox->SetDetached(true);
    // if one is already running (shouldn't normally happen, callers are expected
    // to serialise transfers via HasActiveTransfer()), let the old one keep
    // running and tell the caller we refused, so it can fail its transfer
    // instead of leaving a source pushing data into a box nobody owns.
    if (g_app->m_active_transfer_pbox) {
        log_write("[Mui] PushTransfer called while one is already active, refusing\n");
        return false;
    }
    g_app->m_active_transfer_pbox = std::move(pbox);
    return true;
}

auto App::PopToMenu() -> void {
    for (auto& p : std::ranges::views::reverse(g_app->m_widgets)) {
        if (p->IsMenu()) {
            break;
        }

        p->SetPop();
    }
}

auto App::Pop() -> void {
    if (g_app && !g_app->m_widgets.empty()) {
        g_app->m_widgets.back()->SetPop();
    }
}

auto App::IsMainScreen() -> bool {
    if (!g_app || g_app->m_widgets.empty()) {
        return false;
    }
    if (g_app->m_widgets.size() != 1) {
        return false;
    }
    return g_app->m_widgets.front()->IsMainScreen();
}

auto App::OpenMainScreen() -> void {
    if (!g_app || g_app->m_widgets.empty()) {
        return;
    }
    for (size_t i = 1; i < g_app->m_widgets.size(); ++i) {
        g_app->m_widgets[i]->SetPop();
    }
    g_app->m_widgets.front()->OpenMainScreen();
}

auto App::HandleMinus() -> void {
    if (IsMainScreen()) {
        App::Exit();
    } else {
        OpenMainScreen();
    }
}

namespace {

// follows a container's delegation down to the page it is actually showing.
// bounded so a cycle in an override can't hang the draw loop.
auto ResolveFooterOwner(ui::Widget* widget) -> ui::Widget* {
    for (int guard = 0; guard < 8 && widget; guard++) {
        auto* next = widget->GetFooterOwner();
        if (!next || next == widget) {
            break;
        }
        widget = next;
    }

    return widget;
}

} // namespace

auto App::OwnsFooter(const ui::Widget* widget) -> bool {
    if (!g_app || g_app->m_widgets.empty()) {
        return true;
    }

    if (widget && widget->IsMinimized()) {
        return false;
    }

    ui::Widget* top = nullptr;
    for (auto it = g_app->m_widgets.rbegin(); it != g_app->m_widgets.rend(); ++it) {
        if (!(*it)->IsMinimized() && !(*it)->IsHidden()) {
            top = it->get();
            break;
        }
    }
    if (!top) {
        top = g_app->m_widgets.back().get();
    }

    std::shared_ptr<ui::menu::dbi::InstallSession> session;
    {
        SCOPED_MUTEX(&g_app->m_install_session_mutex);
        session = g_app->m_active_install_session;
    }
    // Detached installs own the footer over the menu, but a real modal pushed
    // above them owns it until the dialog closes.
    if (session && !session->IsMinimized() && !top->IsModal()) {
        return widget == session.get();
    }

    if (ResolveFooterOwner(top) == widget) {
        return true;
    }

    // on the stack but not the owner: covered by whatever sits above it.
    for (const auto& p : g_app->m_widgets) {
        if (p.get() == widget || ResolveFooterOwner(p.get()) == widget) {
            return false;
        }
    }

    // not on the stack at all - a child object drawn by its parent, or the
    // detached transfer box. those keep their own hints.
    return true;
}

auto App::GetChromeOcclusion() -> Vec4 {
    if (!g_app) {
        return {};
    }

    {
        SCOPED_MUTEX(&g_app->m_install_session_mutex);
        if (g_app->m_active_install_session && !g_app->m_active_install_session->IsMinimized()) {
            return {};
        }
    }

    // only what is stacked above the *active* menu can cover its chrome, and
    // the active menu is the last one on the stack - the same one App::Draw
    // starts drawing from.
    auto begin = g_app->m_widgets.begin();
    for (auto it = g_app->m_widgets.begin(); it != g_app->m_widgets.end(); it++) {
        if (!(*it)->IsHidden() && (*it)->IsMenu() && !(*it)->IsMinimized()) {
            begin = it + 1;
        }
    }

    Vec4 out{};

    for (auto it = begin; it != g_app->m_widgets.end(); it++) {
        const auto& p = *it;
        if (p->IsHidden()) {
            continue;
        }

        const auto v = p->GetChromeOcclusion();
        if (v.w <= 0.f || v.h <= 0.f) {
            continue;
        }

        if (out.w <= 0.f || out.h <= 0.f) {
            out = v;
            continue;
        }

        const auto x2 = std::max(out.x + out.w, v.x + v.w);
        const auto y2 = std::max(out.y + out.h, v.y + v.h);
        out.x = std::min(out.x, v.x);
        out.y = std::min(out.y, v.y);
        out.w = x2 - out.x;
        out.h = y2 - out.y;
    }

    return out;
}

void App::Notify(std::string text, ui::NotifEntry::Side side) {
    g_app->m_notif_manager.Push({text, side});
}

void App::Notify(ui::NotifEntry entry) {
    g_app->m_notif_manager.Push(entry);
}

void App::NotifyFlashLed() {
    // ftpsrv calls this from its transfer loop (once per buffer, plus once per
    // log line), and each call is 2-4 blocking hidsys IPC round trips for a
    // 12.5ms blink. flashing more often than the blink lasts buys nothing and
    // taxes every transferred buffer, so rate limit it.
    static constexpr u64 MIN_INTERVAL_NS = 250ULL*1000ULL*1000ULL;
    static std::atomic<u64> last_flash_ns{0};

    const auto now = armTicksToNs(armGetSystemTick());
    auto last = last_flash_ns.load(std::memory_order_relaxed);
    if (last && now - last < MIN_INTERVAL_NS) {
        return;
    }
    if (!last_flash_ns.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return; // another thread just flashed.
    }

    static constexpr HidsysNotificationLedPattern pattern = {
        .baseMiniCycleDuration = 0x1,             // 12.5ms.
        .totalMiniCycles = 0x1,                   // 1 mini cycle(s).
        .totalFullCycles = 0x1,                   // 1 full run(s).
        .startIntensity = 0xF,                    // 100%.
        .miniCycles = {{
            .ledIntensity = 0xF,                  // 100%.
            .transitionSteps = 0xF,               // 1 step(s). Total 12.5ms.
            .finalStepDuration = 0xF,             // Forced 12.5ms.
        }}
    };

    Result rc;
    s32 total;
    HidsysUniquePadId unique_pad_id;

    rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_Handheld, &unique_pad_id, 1, &total);
    if (R_SUCCEEDED(rc) && total) {
        rc = hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
    }

    if (R_FAILED(rc) || !total) {
        rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_No1, &unique_pad_id, 1, &total);
        if (R_SUCCEEDED(rc) && total) {
            hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
        }
    }
}

Result App::PushErrorBox(Result rc, const std::string& message) {
    if (R_FAILED(rc)) {
        // a result code tells the user nothing about a console with wi-fi
        // turned off. say what actually happened instead.
        if (net::IsOfflineError(rc)) {
            log_write("[ERROR] %s (0x%X): no network connection\n", message.c_str(), R_VALUE(rc));
            net::ShowNoConnectionPopup();
            return rc;
        }

        App::Push<ui::ErrorBox>(rc, message);
    }
    return rc;
}

} // namespace sphaira
