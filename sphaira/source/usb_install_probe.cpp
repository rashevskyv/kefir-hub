#include "usb_install_probe.hpp"
#include "app.hpp"
#include "log.hpp"
#include "utils/thread.hpp"

#include <switch.h>
#include <usbhsfs.h>
#include <atomic>
#include <mutex>

namespace sphaira::usb_probe {
namespace {

// how long the PC gets to start talking once the device has enumerated. A
// backend that is up answers the first round; the rounds after that only cover
// an app the user is still clicking "start" in.
constexpr u64 ENUMERATE_TIMEOUT = 3'000'000'000ULL;
constexpr int DETECT_ROUNDS = 2;

std::atomic<State> g_state{State::Idle};
Thread g_thread{};
bool g_thread_created{};
std::mutex g_mutex;
std::unique_ptr<yati::source::Usb> g_source;
std::vector<std::string> g_names;

#if ENABLE_NETWORK_INSTALL
void ThreadFunc(void*) {
    auto source = std::make_unique<yati::source::Usb>(UINT64_MAX);
    std::vector<std::string> names;
    bool found = false;

    if (R_SUCCEEDED(source->GetOpenResult()) && R_SUCCEEDED(source->IsUsbConnected(ENUMERATE_TIMEOUT))) {
        for (int i = 0; i < DETECT_ROUNDS && !found; i++) {
            found = R_SUCCEEDED(source->WaitForConnection(UINT64_MAX, names));
        }
    }

    if (found) {
        log_write("[USB] probe: %s host answered with %zu files\n",
            yati::source::GetUsbProtocolName(source->GetProtocol()), names.size());
        std::scoped_lock lock{g_mutex};
        g_source = std::move(source);
        g_names = std::move(names);
        g_state = State::Found;
    } else {
        log_write("[USB] probe: no install host\n");
        source.reset(); // frees usb:ds before MTP takes it
        g_state = State::NotFound;
    }
}
#endif

} // namespace

void Start() {
#if ENABLE_NETWORK_INSTALL
    if (g_state != State::Idle) {
        return;
    }
    // usb:ds is exclusive: the install device cannot open while usb host
    // storage holds the port. PC Install (USB) does the same in its ctor.
    if (App::GetHddEnable()) {
        usbHsFsExit();
    }
    g_state = State::Running;
    if (R_FAILED(utils::CreateThread(&g_thread, ThreadFunc, nullptr, 1024 * 64, PRIO_PREEMPTIVE))) {
        g_state = State::NotFound;
        return;
    }
    if (R_FAILED(threadStart(&g_thread))) {
        threadClose(&g_thread);
        g_state = State::NotFound;
        return;
    }
    g_thread_created = true;
#endif
}

auto GetState() -> State {
    return g_state;
}

void Finish(std::unique_ptr<yati::source::Usb>& out_source, std::vector<std::string>& out_names) {
    if (g_thread_created) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_thread_created = false;
    }
    std::scoped_lock lock{g_mutex};
    out_source = std::move(g_source);
    out_names = std::move(g_names);
    g_source.reset();
    g_names.clear();
    if (!out_source && App::GetHddEnable()) {
        if (App::GetWriteProtect()) {
            usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
        }
        usbHsFsInitialize(1);
    }
    g_state = State::Idle;
}

} // namespace sphaira::usb_probe
