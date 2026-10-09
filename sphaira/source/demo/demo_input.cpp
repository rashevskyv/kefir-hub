// Buttons from a file: tools/docs/eden.ps1 drives the Hub in Eden (DOCS_DEMO, where posted keys are ignored
// unless the window is active), tools/dev/hub-input.ps1 drives a console over MTP when the "Scripted input"
// setting is on. Commands: include/demo/demo_cmd.hpp.

#include "demo/demo_input.hpp"
#include "demo/demo_cmd.hpp"
#include "app.hpp"
#include "log.hpp"
#include "title_info.hpp"
#include "ui/menus/main_menu.hpp"
#include "ui/menus/menu_base.hpp"

#include <cstdio>
#include <deque>

namespace sphaira {
extern App* g_app;
}

namespace sphaira::demo {
namespace {

static_assert(pad::A == HidNpadButton_A && pad::B == HidNpadButton_B && pad::X == HidNpadButton_X && pad::Y == HidNpadButton_Y);
static_assert(pad::L3 == HidNpadButton_StickL && pad::R3 == HidNpadButton_StickR && pad::L == HidNpadButton_L && pad::R == HidNpadButton_R);
static_assert(pad::ZL == HidNpadButton_ZL && pad::ZR == HidNpadButton_ZR && pad::Plus == HidNpadButton_Plus && pad::Minus == HidNpadButton_Minus);
static_assert(pad::Left == HidNpadButton_Left && pad::Up == HidNpadButton_Up && pad::Right == HidNpadButton_Right && pad::Down == HidNpadButton_Down);

constexpr const char* INPUT_TXT = "/config/kefir/demo/input.txt";
constexpr const char* READY_FILE = "/config/kefir/demo/ready";
constexpr const char* STATE_TXT = "/config/kefir/demo/state.txt";
constexpr u64 MS = 1000000ull;
constexpr u64 PRESS_GAP_NS = 350 * MS; // same pace as eden.ps1 used for posted keys
constexpr u64 POLL_NS = 100 * MS;
constexpr int HOLD_FRAMES = 3;

// main thread only (App::Poll).
std::deque<std::string> g_queue;
u64 g_next_ns{};
u64 g_held{};
int g_held_frames{};

void ReadInputFile() {
    auto f = std::fopen(INPUT_TXT, "rb");
    if (!f) {
        return;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        g_queue.emplace_back(line);
    }
    std::fclose(f);
    std::remove(INPUT_TXT);
}

// fresh main screen in the new language: menu labels are translated when a menu is built.
void SwitchLanguage(const std::string& code) {
    App::SetLanguage(code, false);
    title::Clear();  // game names are loaded in the UI language
    while (!g_app->m_widgets.empty()) {
        g_app->m_widgets.pop_back();
    }
    App::Push<ui::menu::main::MainMenu>();
    log_write("[demo] lang %s, menus rebuilt\n", code.c_str());
}

// one line per open widget, bottom to top: "menu <short title>", "modal" or "widget".
void DumpState() {
    auto f = std::fopen(STATE_TXT, "wb");
    if (!f) {
        return;
    }
    for (const auto& w : g_app->m_widgets) {
        const auto* menu = w->IsMenu() ? static_cast<const ui::menu::MenuBase*>(w.get()) : w->GetChromeOwner();
        if (menu) {
            std::fprintf(f, "menu %s\n", menu->GetShortTitle());
        } else {
            std::fprintf(f, "%s\n", w->IsModal() ? "modal" : "widget");
        }
    }
    std::fclose(f);
    log_write("[demo] state dumped\n");
}

} // namespace

void PollInput(u64& kdown, u64& kheld, u64& kup) {
    if (g_held) {
        if (--g_held_frames > 0) {
            kheld |= g_held;
        } else {
            kup |= g_held;
            g_held = 0;
        }
    }

    const u64 now = armTicksToNs(armGetSystemTick());
    if (now < g_next_ns) {
        return;
    }
    if (g_queue.empty()) {
        ReadInputFile();
        if (g_queue.empty()) {
            g_next_ns = now + POLL_NS;
            return;
        }
    }

    const auto line = std::move(g_queue.front());
    g_queue.pop_front();
    const auto cmd = ParseCmd(line);
    switch (cmd.kind) {
        case Cmd::Button:
            kdown |= cmd.button;
            kheld |= cmd.button;
            g_held = cmd.button;
            g_held_frames = HOLD_FRAMES;
            g_next_ns = now + PRESS_GAP_NS;
            break;
        case Cmd::Wait:
            g_next_ns = now + u64(cmd.seconds * 1e9);
            break;
        case Cmd::Lang:
            SwitchLanguage(cmd.arg);
            g_next_ns = now + 1000 * MS;
            break;
        case Cmd::Dump:
            DumpState();
            break;
        case Cmd::Ready:
            if (auto f = std::fopen(READY_FILE, "wb")) {
                std::fclose(f);
            }
            break;
        case Cmd::Invalid:
            log_write("[demo] bad input line: %s", line.c_str());
            break;
    }
}

} // namespace sphaira::demo
