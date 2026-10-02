#include "web.hpp"
#include "path_util.hpp"
#include "web_http.hpp"
#include "web_qr.hpp"
#include "web_upload.hpp"
#include "web_mdns.hpp"
#include "web_router.hpp"
#include "log.hpp"
#include "app.hpp"
#include "net.hpp"
#include "i18n.hpp"
#include "defines.hpp"
#include "title_info.hpp"
#include "location.hpp"
#include "utils/thread.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>
#include <memory>
#include <unordered_set>

#include "ui/progress_box.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>

namespace sphaira {
namespace {

using namespace web::detail;

constexpr u16 SHARE_PORT_DEFAULT = 80;
constexpr u16 SHARE_PORT_FALLBACK_FIRST = 8080;
constexpr u16 SHARE_PORT_FALLBACK_LAST = 8090;
// Multiple worker threads accept() on the same listening socket so a status
// poll (e.g. from a second device) can still be served while another thread
// is blocked handling a long upload/install request.
constexpr size_t SHARE_WORKER_COUNT = 3;
// seconds without an ip before the server gives up and closes itself.
constexpr unsigned SHARE_OFFLINE_GIVEUP = 30;

Thread g_share_threads[SHARE_WORKER_COUNT]{};
std::atomic<size_t> g_share_thread_count{};
std::atomic_bool g_share_running{false};
std::atomic_bool g_share_self_test{false};
std::atomic<Socket> g_share_socket{-1};
std::atomic<u16> g_share_port{};
// StartShareServer() and WebShareStop() run on different threads (main / the server's
// ProgressBox worker): a restart must not touch g_share_threads while a stop still joins them.
Mutex g_share_lifecycle_mutex{};
// the ip the listener was bound under, and the applet-hook resume counter it
// was last checked against. see TickShareNetwork().
std::atomic<u32> g_share_ip{};
std::atomic<u32> g_share_resume_gen{};

// The advertised TCP window equals the socket buffer size, and the default
// initial buffer (64K, see SocketInitConfig in main.cpp) caps throughput at
// window/RTT — with the Switch's high wifi power-save latency that lands at
// only a few Mbit/s. Request bigger buffers, falling back if the bsd service
// rejects the size (applet mode has a much lower tcp_*_buf_max_size).
void TuneShareSocket(Socket sock) {
    for (int size = 1024 * 1024; size >= 1024 * 128; size /= 2) {
        if (!setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size))) {
            break;
        }
    }

    for (int size = 1024 * 1024; size >= 1024 * 128; size /= 2) {
        if (!setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))) {
            break;
        }
    }

    const int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
}

auto CreateShareListener(u16 port) -> Socket {
    const auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_write("[WEB] socket() failed for port %u: %d %s\n", port, errno, std::strerror(errno));
        return -1;
    }

    const int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // set before listen() so the window scale factor negotiated during the
    // handshake accounts for the enlarged buffer (inherited by accept()).
    TuneShareSocket(sock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_write("[WEB] bind() failed for port %u: %d %s\n", port, errno, std::strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 4) < 0) {
        log_write("[WEB] listen() failed for port %u: %d %s\n", port, errno, std::strerror(errno));
        close(sock);
        return -1;
    }

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    return sock;
}

// Called once a second from the server's progress box -- the one thread that
// owns the server's lifetime -- so it needs no locking of its own. Returns
// false when the server should be stopped.
//
// A sleep takes the network interface down with it: the listening socket
// survives the wake as a valid fd that is no longer attached to anything, so
// accept() just goes quiet and the server is unreachable while looking healthy
// from here. Same address after the wake means the url and qr on screen still
// point here and a fresh bind() is enough. A different address (or none at all)
// means they do not, and there is nothing worth keeping alive.
TimeStamp g_share_net_ts{};
std::atomic<unsigned> g_share_offline{};

auto TickShareNetwork() -> bool {
    if (g_share_net_ts.GetMs() < 1000) {
        return true;
    }
    g_share_net_ts.Update();

    u32 ip{};
    if (R_FAILED(nifmGetCurrentIpAddress(&ip)) || !ip) {
        // wi-fi re-associates a few seconds after a wake, so being offline is
        // only conclusive once it has had time to come back.
        if (++g_share_offline < SHARE_OFFLINE_GIVEUP) {
            return true;
        }

        log_write("[WEB] no ip for %us, stopping server\n", g_share_offline.load());
        App::Notify("Web server stopped: the console went offline"_i18n);
        return false;
    }
    g_share_offline = 0;

    if (ip != g_share_ip) {
        log_write("[WEB] ip changed %08X -> %08X, stopping server\n", g_share_ip.load(), ip);
        App::Notify("Web server stopped: the console's IP address changed"_i18n);
        return false;
    }

    const auto gen = net::ResumeGeneration();
    if (gen == g_share_resume_gen) {
        return true;
    }
    g_share_resume_gen = gen;

    const auto old = g_share_socket.exchange(-1);
    if (old >= 0) {
        shutdown(old, SHUT_RDWR);
        close(old);
    }

    // same port, so the url and qr code already on screen stay valid.
    const auto sock = CreateShareListener(g_share_port);
    if (sock < 0) {
        log_write("[WEB] rebind failed on port %u, stopping server\n", g_share_port.load());
        return false;
    }

    g_share_socket = sock;
    log_write("[WEB] listener rebound on port %u after resume\n", g_share_port.load());
    StartMdnsResponder(g_share_ip);
    return true;
}

// Multiple instances of this run concurrently (see SHARE_WORKER_COUNT), each
// accept()-ing on the shared listening socket, so one client's long-running
// request (e.g. install) doesn't block other clients (e.g. a status poll).
void ShareThreadFunc(void*) {
    while (g_share_running) {
        sockaddr_in remote{};
        socklen_t remote_len = sizeof(remote);
        const auto client = accept(g_share_socket, reinterpret_cast<sockaddr*>(&remote), &remote_len);
        if (client < 0) {
            // EBADF means a rebind is swapping the listener under us: back off
            // like any other hard error and pick the new fd up next time round.
            svcSleepThread(errno == EWOULDBLOCK || errno == EAGAIN ? 5'000'000 : 50'000'000);
            continue;
        }

        fcntl(client, F_SETFL, fcntl(client, F_GETFL) | O_NONBLOCK);
        TuneShareSocket(client);
        HandleRequest(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}

auto TestShareServerLoopback(u16 port) -> bool {
    const auto client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        log_write("[WEB] loopback socket() failed: %d %s\n", errno, std::strerror(errno));
        return false;
    }
    ON_SCOPE_EXIT(close(client));

    const timeval timeout{2, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(client, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_write("[WEB] loopback connect() failed on port %u: %d %s\n", port, errno, std::strerror(errno));
        return false;
    }

    constexpr std::string_view request{"GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"};
    if (!SendAll(client, request.data(), request.size())) {
        log_write("[WEB] loopback request send failed on port %u\n", port);
        return false;
    }

    char response[64]{};
    const auto received = recv(client, response, sizeof(response) - 1, 0);
    const bool passed = received > 0 && std::string_view{response, static_cast<size_t>(received)}.starts_with("HTTP/1.1 200");
    log_write("[WEB] loopback listener self-test %s on port %u\n", passed ? "passed" : "failed", port);
    return passed;
}

auto StartShareServer() -> Result {
    SCOPED_MUTEX(&g_share_lifecycle_mutex);
    if (g_share_running) {
        R_SUCCEED();
    }

    std::vector<u16> candidate_ports{SHARE_PORT_DEFAULT};
    for (u16 port = SHARE_PORT_FALLBACK_FIRST; port <= SHARE_PORT_FALLBACK_LAST; port++) {
        candidate_ports.push_back(port);
    }

    for (u16 port : candidate_ports) {
        const auto sock = CreateShareListener(port);
        if (sock < 0) {
            continue;
        }

        g_share_socket = sock;
        g_share_port = port;
        g_share_offline = 0;
        g_share_resume_gen = net::ResumeGeneration();
        g_share_net_ts.Update();
        u32 share_ip{};
        nifmGetCurrentIpAddress(&share_ip);
        g_share_ip = share_ip;
        g_share_running = true;

        const size_t target_worker_count = App::IsApplet() ? 2 : SHARE_WORKER_COUNT;
        size_t started = 0;
        for (; started < target_worker_count; started++) {
            Result rc = utils::CreateThread(&g_share_threads[started], ShareThreadFunc, nullptr, 1024 * 128, PRIO_PREEMPTIVE);
            if (R_SUCCEEDED(rc)) {
                rc = threadStart(&g_share_threads[started]);
                if (R_FAILED(rc)) {
                    threadClose(&g_share_threads[started]);
                }
            }
            if (R_FAILED(rc)) {
                log_write("[WEB] failed to start worker %u/%u: 0x%X\n",
                    static_cast<unsigned>(started + 1), static_cast<unsigned>(target_worker_count), rc);
                break;
            }
        }

        if (!started) {
            g_share_running = false;
            close(g_share_socket);
            g_share_socket = -1;
            return Result_FsUnknownStdioError;
        }

        g_share_thread_count = started;
        g_share_self_test = TestShareServerLoopback(port);
        log_write("[WEB] listening on port %u with %u worker(s), mode=%s\n", port,
            static_cast<unsigned>(started), App::IsApplet() ? "applet" : "title");
        StartMdnsResponder(g_share_ip);
        R_SUCCEED();
    }

    R_THROW(Result_FsUnknownStdioError);
}



auto CreateQrImage(const std::string& url) -> int {
    const auto qr = QrCode::Encode(url);
    constexpr int border = 4;
    constexpr int scale = 8;
    constexpr int qr_size = QrCode::SIZE + border * 2;
    constexpr int image_size = qr_size * scale;

    std::vector<u8> rgba(image_size * image_size * 4);
    for (int y = 0; y < image_size; y++) {
        for (int x = 0; x < image_size; x++) {
            const auto module_x = x / scale - border;
            const auto module_y = y / scale - border;
            const auto dark = module_x >= 0 && module_y >= 0 && module_x < QrCode::SIZE && module_y < QrCode::SIZE && qr.Get(module_x, module_y);
            const auto off = (y * image_size + x) * 4;
            const u8 value = dark ? 0 : 255;
            rgba[off + 0] = value;
            rgba[off + 1] = value;
            rgba[off + 2] = value;
            rgba[off + 3] = 255;
        }
    }

    return nvgCreateImageRGBA(App::GetVg(), image_size, image_size, 0, rgba.data());
}

} // namespace

auto WebShow(const std::string& url) -> Result {
    WebCommonConfig config{};
    WebCommonReply reply{};
    WebExitReason reason{};
    AccountUid account_uid{};
    char last_url[FS_MAX_PATH]{};
    size_t last_url_len{};

    // WebBackgroundKind_Unknown1 = shows background
    // WebBackgroundKind_Unknown2 = shows background faded

    if (R_FAILED(accountGetPreselectedUser(&account_uid))) {
        log_write("failed: accountGetPreselectedUser\n");
        if (R_FAILED(accountTrySelectUserWithoutInteraction(&account_uid, false))) {
            log_write("failed: accountTrySelectUserWithoutInteraction\n");
            if (R_FAILED(accountGetLastOpenedUser(&account_uid))) {
                log_write("failed: accountGetLastOpenedUser\n");
            }
        }
    }

    if (R_FAILED(webPageCreate(&config, url.c_str()))) { log_write("failed: webPageCreate\n"); }
    if (R_FAILED(webConfigSetWhitelist(&config, ".*"))) { log_write("failed: webConfigSetWhitelist\n"); }
    if (R_FAILED(webConfigSetEcClientCert(&config, true))) { log_write("failed: webConfigSetEcClientCert\n"); }
    if (R_FAILED(webConfigSetScreenShot(&config, true))) { log_write("failed: webConfigSetScreenShot\n"); }
    if (R_FAILED(webConfigSetBootDisplayKind(&config, WebBootDisplayKind_Black))) { log_write("failed: webConfigSetBootDisplayKind\n"); }
    if (R_FAILED(webConfigSetBackgroundKind(&config, WebBackgroundKind_Default))) { log_write("failed: webConfigSetBackgroundKind\n"); }
    if (R_FAILED(webConfigSetPointer(&config, true))) { log_write("failed: webConfigSetPointer\n"); }
    if (R_FAILED(webConfigSetLeftStickMode(&config, WebLeftStickMode_Pointer))) { log_write("failed: webConfigSetLeftStickMode\n"); }
    if (R_FAILED(webConfigSetBootAsMediaPlayer(&config, false))) { log_write("failed: webConfigSetBootAsMediaPlayer\n"); }
    if (R_FAILED(webConfigSetJsExtension(&config, true))) { log_write("failed: webConfigSetJsExtension\n"); }
    if (R_FAILED(webConfigSetMediaPlayerAutoClose(&config, false))) { log_write("failed: webConfigSetMediaPlayerAutoClose\n"); }
    if (R_FAILED(webConfigSetPageCache(&config, true))) { log_write("failed: webConfigSetPageCache\n"); }
    if (R_FAILED(webConfigSetFooterFixedKind(&config, WebFooterFixedKind_Default))) { log_write("failed: webConfigSetFooterFixedKind\n"); }
    if (R_FAILED(webConfigSetPageFade(&config, true))) { log_write("failed: webConfigSetPageFade\n"); }
    if (R_FAILED(webConfigSetPageScrollIndicator(&config, true))) { log_write("failed: webConfigSetPageScrollIndicator\n"); }
    // if (R_FAILED(webConfigSetMediaPlayerSpeedControl(&config, true))) { log_write("failed: webConfigSetMediaPlayerSpeedControl\n"); }
    if (R_FAILED(webConfigSetBootMode(&config, WebSessionBootMode_AllForeground))) { log_write("failed: webConfigSetBootMode\n"); }
    if (R_FAILED(webConfigSetTransferMemory(&config, true))) { log_write("failed: webConfigSetTransferMemory\n"); }
    if (R_FAILED(webConfigSetTouchEnabledOnContents(&config, true))) { log_write("failed: webConfigSetTouchEnabledOnContents\n"); }
    // if (R_FAILED(webConfigSetMediaPlayerUi(&config, true))) { log_write("failed: webConfigSetMediaPlayerUi\n"); }
    if (R_FAILED(webConfigSetWebAudio(&config, false))) { log_write("failed: webConfigSetWebAudio\n"); }
    if (R_FAILED(webConfigSetPageCache(&config, true))) { log_write("failed: webConfigSetPageCache\n"); }
    // if (R_FAILED(webConfigSetBootLoadingIcon(&config, true))) { log_write("failed: webConfigSetBootLoadingIcon\n"); }
    if (R_FAILED(webConfigSetUid(&config, account_uid))) { log_write("failed: webConfigSetUid\n"); }

    if (R_FAILED(webConfigShow(&config, &reply))) { log_write("failed: webConfigShow\n"); }
    if (R_FAILED(webReplyGetExitReason(&reply, &reason))) { log_write("failed: webReplyGetExitReason\n"); }
    if (R_FAILED(webReplyGetLastUrl(&reply, last_url, sizeof(last_url), &last_url_len))) { log_write("failed: webReplyGetLastUrl\n"); }
    log_write("last url: %s\n", last_url);
    R_SUCCEED();
}



auto WebStartServer(const std::string& page_path, WebShareResult& out) -> Result {
    u32 ip{};
    R_TRY(nifmGetCurrentIpAddress(&ip));
    R_UNLESS(ip != 0, Result_FsNotActive);

    // note: this only brings the server up (and is a no-op if it already is).
    // what the root page shows comes from App::GetMountedFolders(), so stopping
    // and starting the server never disturbs a mount, and a mount made while the
    // server runs takes effect on the next request.
    R_TRY(StartShareServer());

    char url[128]{};
    if (IsMdnsActive()) {
        if (g_share_port == 80) {
            std::snprintf(url, sizeof(url), "http://kefir.local");
        } else {
            std::snprintf(url, sizeof(url), "http://kefir.local:%u", g_share_port.load());
        }
    } else {
        if (g_share_port == 80) {
            std::snprintf(url, sizeof(url), "http://%u.%u.%u.%u",
                ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        } else {
            std::snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%u",
                ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, g_share_port.load());
        }
    }

    out.url = std::string{url} + page_path;
    out.qr_image = CreateQrImage(out.url);
    out.listener_self_test = g_share_self_test;

    R_SUCCEED();
}

auto WebShareFolder(const fs::FsPath& path, WebShareResult& out) -> Result {
    R_UNLESS(!path.empty(), Result_FsEmpty);

    fs::FsNativeSd fs;
    R_UNLESS(fs.DirExists(path), Result_FsInvalidType);

    std::string page_path;
    if (path.s[0] != '\0' && !(path.s[0] == '/' && path.s[1] == '\0')) {
        page_path = "/?path=" + web::detail::UrlEncode(path.s);
    }

    R_TRY(WebStartServer(page_path, out));

    R_SUCCEED();
}

void WebShareStop() {
    SCOPED_MUTEX(&g_share_lifecycle_mutex);
    const auto was_running = g_share_running.exchange(false);

    if (was_running) {
        StopMdnsResponder();

        if (g_share_socket >= 0) {
            shutdown(g_share_socket, SHUT_RDWR);
            close(g_share_socket);
            g_share_socket = -1;
        }

        for (size_t i = 0; i < g_share_thread_count; i++) {
            threadWaitForExit(&g_share_threads[i]);
            threadClose(&g_share_threads[i]);
        }
        g_share_thread_count = 0;
        g_share_port = 0;
        g_share_self_test = false;
    }
}

WebUploadState WebGetUploadState() {
    WebUploadState out;
    out.active = g_upload_state.active.load();
    out.bytes = g_upload_state.bytes.load();
    out.total = g_upload_state.total.load();
    std::scoped_lock lock{g_upload_state.name_mutex};
    out.name = g_upload_state.name;
    return out;
}

static std::atomic<ui::ProgressBox*> g_web_pbox = nullptr;

void WebSetProgressBox(ui::ProgressBox* pbox) {
    g_web_pbox.store(pbox);
}

ui::ProgressBox* WebGetProgressBox() {
    return g_web_pbox.load();
}

void WebPushServerProgressBox(const std::string& url, int qr_image, const std::string& title) {
    App::PopToMenu();
    // Route the server box through the detached-transfer path (like MTP) instead
    // of pushing it as a blocking widget. This grants it the same UX as other
    // transfers: R3 minimises it to a corner badge (so the menu stays usable
    // while the server / an install runs) and B / Stop cancels it.
    App::PushTransfer(std::make_unique<ui::ProgressBox>(qr_image, title, url,
        [url](ui::ProgressBox* pbox) -> Result {
            pbox->NewTransferForce(App::IsApplet()
                ? "Applet Mode: keep this screen open; use the same non-guest Wi-Fi. Press B to stop."_i18n
                : "Press B to Stop Server"_i18n);
            WebSetProgressBox(pbox);
            ON_SCOPE_EXIT(WebSetProgressBox(nullptr));
            std::string last_name;
            while (!pbox->ShouldExit() && WebShareIsRunning() && TickShareNetwork()) {
                const auto state = WebGetUploadState();
                if (state.active) {
                    if (state.name != last_name) {
                        last_name = state.name;
                        pbox->NewTransferForce(state.name);
                    }
                    pbox->UpdateTransferForce(state.bytes, state.total);
                } else if (!last_name.empty()) {
                    const std::string completed_name = last_name;
                    last_name.clear();
                    pbox->ResetTransferProgress();
                    pbox->SetTitle(url);
                    if (completed_name.starts_with("Installing:")) {
                        pbox->NewTransferForce("Installation completed"_i18n);
                    } else {
                        pbox->NewTransferForce("Upload completed"_i18n);
                    }
                    for (int i = 0; i < 30 && !WebGetUploadState().active && !pbox->ShouldExit(); ++i) {
                        svcSleepThread(100'000'000LL);
                    }
                    if (!WebGetUploadState().active && !pbox->ShouldExit()) {
                        pbox->NewTransferForce(App::IsApplet()
                            ? "Applet Mode: keep this screen open; use the same non-guest Wi-Fi. Press B to stop."_i18n
                            : "Press B to Stop Server"_i18n);
                    }
                }
                svcSleepThread(100'000'000LL);
            }
            WebShareStop();
            R_SUCCEED();
        },
        [qr_image](Result) {
            nvgDeleteImage(App::GetVg(), qr_image);
        }
    ));
}

bool WebShareIsRunning() {
    return g_share_running.load();
}

} // namespace sphaira
