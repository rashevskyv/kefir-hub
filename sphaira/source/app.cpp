#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include "ui/menus/main_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include "ui/menus/dbi_menu.hpp"

#include "app.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "nacp_util.hpp"
#include "net.hpp"
#include "nro.hpp"
#include "ntp.hpp"
#include "forwarder_auto_install.hpp"
#include "location.hpp"
#include "evman.hpp"
#include "owo.hpp"
#include "image.hpp"
#include "nxlink.h"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "version_compare.hpp"
#include "ftpsrv_helper.hpp"
#include "haze_helper.hpp"
#include "web.hpp"
#include "swkbd.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include "utils/devoptab_mtp.hpp"
#include <sys/statvfs.h>

#include <nanovg_dk.h>
#include <minIni.h>
#include <algorithm>
#include <atomic>
#include <ranges>
#include <cassert>
#include <cstring>
#include <ctime>
#include <span>
#include <dirent.h>
#include <usbhsfs.h>

extern "C" {
    u32 __nx_applet_exit_mode = 0;
} // extern "C"

namespace sphaira {
constinit App* g_app{};
bool LoadThemeMeta(const fs::FsPath& path, ThemeMeta& meta);


bool IsKefirHubNacp(const NacpStruct& nacp) {
    const auto name = nacp_util::GetName(nacp);
    return !std::strcmp(name, "Kefir Hub") || !std::strcmp(name, "sphaira");
}


void on_i18n_change() {
    i18n::exit();
    i18n::init(App::GetLanguage());
}

auto App::HasActiveTransfer() -> bool {
    if (!g_app) return false;
    if (g_app->m_active_transfer_pbox != nullptr) return true;
    SCOPED_MUTEX(&g_app->m_install_session_mutex);
    return g_app->m_active_install_session != nullptr;
}

auto App::PushInstallSession(std::shared_ptr<ui::menu::dbi::InstallSession> session) -> bool {
    if (!g_app || !session) return false;
    SCOPED_MUTEX(&g_app->m_install_session_mutex);
    if (g_app->m_install_sessions_closed || g_app->m_active_install_session) {
        log_write("[App] PushInstallSession called while install session active or shutting down, refusing\n");
        return false;
    }
    g_app->m_active_install_session = std::move(session);
    return true;
}

auto App::GetActiveInstallSession() -> std::shared_ptr<ui::menu::dbi::InstallSession> {
    if (!g_app) return nullptr;
    SCOPED_MUTEX(&g_app->m_install_session_mutex);
    return g_app->m_active_install_session;
}

auto App::HasActiveInstallSession() -> bool {
    if (!g_app) return false;
    SCOPED_MUTEX(&g_app->m_install_session_mutex);
    return g_app->m_active_install_session != nullptr;
}

void App::CloseActiveInstallSession() {
    if (g_app) {
        SCOPED_MUTEX(&g_app->m_install_session_mutex);
        g_app->m_active_install_session.reset();
    }
}

auto App::CloseInstallAdmissionAndGetSession() -> std::shared_ptr<ui::menu::dbi::InstallSession> {
    if (!g_app) return nullptr;
    SCOPED_MUTEX(&g_app->m_install_session_mutex);
    g_app->m_install_sessions_closed = true;
    return g_app->m_active_install_session;
}

void App::ResetTouchAfterApplet() {
    if (!g_app) {
        return;
    }
    g_app->m_touch_info.is_clicked = false;
    g_app->m_touch_info.is_tap = false;
    g_app->m_touch_info.is_touching = false;
    g_app->m_touch_info.is_scroll = false;
    g_app->m_touch_info.is_end = false;
}

auto App::GetExePath() -> fs::FsPath {
    return g_app->m_app_path;
}

auto App::GetApp() -> App* {
    return g_app;
}

auto App::IsExiting() -> bool {
    return !g_app || g_app->m_quit;
}

auto App::GetVg() -> NVGcontext* {
    return g_app->vg;
}

void DrawElement(float x, float y, float w, float h, ThemeEntryID id) {
    DrawElement({x, y, w, h}, id);
}

void DrawElement(const Vec4& v, ThemeEntryID id) {
    const auto& e = g_app->m_theme.elements[id];

    switch (e.type) {
        case ElementType::None: {
        } break;
        case ElementType::Texture: {
            auto paint = nvgImagePattern(g_app->vg, v.x, v.y, v.w, v.h, 0, e.texture, 1.f);
            // override the icon colours if set
            if (id > ThemeEntryID_ICON_COLOUR && id < ThemeEntryID_MAX) {
                if (g_app->m_theme.elements[ThemeEntryID_ICON_COLOUR].type != ElementType::None) {
                    paint.innerColor = g_app->m_theme.GetColour(ThemeEntryID_ICON_COLOUR);
                }
            }
            ui::gfx::drawRect(g_app->vg, v, paint);
        } break;
        case ElementType::Colour: {
            ui::gfx::drawRect(g_app->vg, v, e.colour);
        } break;
    }
}

auto GetThemeContainRect(const Vec4& dest, ThemeEntryID id) -> Vec4 {
    if (!g_app || dest.w <= 0.f || dest.h <= 0.f) {
        return dest;
    }
    const auto& e = g_app->m_theme.elements[id];
    if (e.type != ElementType::Texture) {
        return dest;
    }
    int iw{}, ih{};
    nvgImageSize(g_app->vg, e.texture, &iw, &ih);
    if (iw <= 0 || ih <= 0) {
        return dest;
    }
    const float scale = std::min(dest.w / static_cast<float>(iw), dest.h / static_cast<float>(ih));
    const float dw = static_cast<float>(iw) * scale;
    const float dh = static_cast<float>(ih) * scale;
    return {dest.x + (dest.w - dw) / 2.f, dest.y + (dest.h - dh) / 2.f, dw, dh};
}

void DrawElementContain(const Vec4& dest, ThemeEntryID id) {
    DrawElement(GetThemeContainRect(dest, id), id);
}

void App::PlaySoundEffect(SoundEffect) {}

App::~App() {
    m_quit = true;

    // boost mode is disabled in userAppExit().
    App::SetBoostMode(true);

    log_write_error("[SHUTDOWN] begin app exit v%s", APP_VERSION);
    log_write("starting to exit\n");
    TimeStamp ts;

    // Shutdown admission must close at the beginning of the destructor under the mutex:
    // Every session published before admission closure is captured in this snapshot and cancelled;
    // every publication attempt after admission closure is refused by PushInstallSession.
    auto cancel_session = CloseInstallAdmissionAndGetSession();
    if (cancel_session) {
        cancel_session->CancelSession();
    }

    // same phase timing as the constructor: shutdown blocks on the same
    // servers and mounts, and "sometimes it is slow to close" needs a number
    // per step, not a total.
    TimeStamp phase;
    const auto mark = [&phase](const char* what) {
        const auto ms = phase.GetMs();
        if (ms >= 1) {
            log_write("[exit] %-22s %4zu ms\n", what, ms);
        }
        phase.Update();
    };

    log_write_error("[SHUTDOWN] begin background services");
    TimeStamp bg_phase;

    // Wake any remote filesystem reads before widget destructors wait for
    // their worker threads. The applet keeps exit locked until this finishes.
    devoptab::common::RequestCurlShutdown();
    curl::RequestShutdown();

    mark("curl shutdown");

    appletUnhook(&m_appletHookCookie);

    mark("applet unhook");

    ntp::Stop();
    forwarder_auto::StopCheck();

    mark("ntp + forwarder_auto");

    if (App::GetFtpEnable()) {
        log_write("closing ftp\n");
        ftpsrv::Exit();
    }

    mark("ftp");

    if (App::GetNxlinkEnable()) {
        log_write("closing nxlink\n");
        nxlinkExit();
    }

    mark("nxlink");
    log_write_error("[SHUTDOWN] end background services (%zu ms)", bg_phase.GetMs());

    log_write_error("[SHUTDOWN] begin mtp");
    TimeStamp mtp_phase;
    if (haze::IsRunning()) {
        log_write("closing mtp\n");
        haze::Exit(false);
    }
    sphaira::devoptab::mtp::CloseMtpSession();

    mark("mtp");
    log_write_error("[SHUTDOWN] end mtp (%zu ms)", mtp_phase.GetMs());

    log_write_error("[SHUTDOWN] begin web");
    TimeStamp web_phase;
    if (cancel_session) {
        cancel_session->CancelSession();
    }
    if (auto pbox = WebGetProgressBox()) {
        pbox->RequestExit();
    }
    if (m_active_transfer_pbox) {
        m_active_transfer_pbox->RequestExit();
    }
    if (sphaira::WebShareIsRunning()) {
        log_write("closing web\n");
    }
    sphaira::WebShareStop();

    mark("web");
    log_write_error("[SHUTDOWN] end web (%zu ms)", web_phase.GetMs());

    log_write_error("[SHUTDOWN] begin usb host");
    TimeStamp usb_phase;
    if (usbHsFsGetStatusChangeUserEvent()) {
        log_write("closing hdd\n");
        usbHsFsExit();
    }

    mark("usb mass storage");
    log_write_error("[SHUTDOWN] end usb host (%zu ms)", usb_phase.GetMs());

    log_write_error("[SHUTDOWN] begin widgets/gpu");
    TimeStamp gpu_phase;

    // GPU must be idle before widgets free nvg images, but the swapchain and
    // nvg context have to stay alive for those deletes (file viewer, games
    // list, appstore changelog). The old "destroy framebuffer first" order
    // avoided a stale in-flight texture on appstore exit; waitIdle is the
    // actual requirement.
    //
    // Pop from the top. vector::clear() destroys front-to-back, so a file
    // viewer still holding a raw Fs* into the file browser under it would
    // call IsNative() on a freed object (2168-0001 at a null vtable slot)
    // when nxlink (or SELECT) exits with the viewer open.
    if (this->swapchain) {
        this->queue.waitIdle();
    }

    m_active_transfer_pbox.reset();
    {
        SCOPED_MUTEX(&m_install_session_mutex);
        m_active_install_session.reset();
    }
    while (!m_widgets.empty()) {
        m_widgets.pop_back();
    }
    nvgDeleteImage(vg, m_default_image);

    this->destroyFramebufferResources();

    mark("widgets + framebuffer");

    i18n::exit();
    curl::Exit();

    ini_puts("config", "theme", m_theme.meta.ini_path, CONFIG_PATH);
    CloseTheme();

    mark("i18n + curl + theme");

    nvgDeleteDk(this->vg);
    this->renderer.reset();

#ifdef USE_NVJPG
    m_decoder.finalize();
    nj::finalize();
#endif

    mark("graphics teardown");
    log_write_error("[SHUTDOWN] end widgets/gpu (%zu ms)", gpu_phase.GetMs());

    // backup hbmenu if it is not sphaira
    if (App::GetReplaceHbmenuEnable() && !IsHbmenu()) {
        NacpStruct hbmenu_nacp;
        fs::FsNativeSd fs;
        Result rc;

        if (R_SUCCEEDED(rc = nro_get_nacp("/hbmenu.nro", hbmenu_nacp)) && !IsKefirHubNacp(hbmenu_nacp)) {
            log_write("backing up hbmenu.nro\n");
            if (R_FAILED(rc = fs.copy_entire_file("/switch/hbmenu.nro", "/hbmenu.nro"))) {
                log_write("failed to backup  hbmenu.nro\n");
            }
        } else {
            log_write("not backing up\n");
        }

        if (R_FAILED(rc = fs.copy_entire_file("/hbmenu.nro", GetExePath()))) {
            log_write("failed to copy entire file: %s 0x%X module: %u desc: %u\n", GetExePath().s, rc, R_MODULE(rc), R_DESCRIPTION(rc));
        } else {
            log_write("success with copying over root file!\n");
        }
    } else if (IsHbmenu()) {
        // check we have a version that's newer than current.
        NacpStruct hbmenu_nacp;
        fs::FsNativeSd fs;
        Result rc;

        // ensure that are still sphaira
        if (R_SUCCEEDED(rc = nro_get_nacp("/hbmenu.nro", hbmenu_nacp)) && IsKefirHubNacp(hbmenu_nacp)) {
            NacpStruct sphaira_nacp;
            fs::FsPath sphaira_path = "/switch/kefir-hub/kefir-hub.nro";

            rc = nro_get_nacp(sphaira_path, sphaira_nacp);
            if (R_FAILED(rc) || !IsKefirHubNacp(sphaira_nacp)) {
                sphaira_path = "/switch/kefir-hub.nro";
                rc = nro_get_nacp(sphaira_path, sphaira_nacp);
            }

            // found sphaira, now lets get compare version
            if (R_SUCCEEDED(rc) && IsKefirHubNacp(sphaira_nacp)) {
                if (IsVersionNewer(hbmenu_nacp.display_version, sphaira_nacp.display_version)) {
                    if (R_FAILED(rc = fs.copy_entire_file(GetExePath(), sphaira_path))) {
                        log_write("failed to copy entire file: %s 0x%X module: %u desc: %u\n", sphaira_path.s, rc, R_MODULE(rc), R_DESCRIPTION(rc));
                    } else {
                        log_write("success with updating hbmenu!\n");
                    }
                }
            }
        } else {
            log_write("no longer hbmenu!\n");
        }
    }

    mark("hbmenu replace");

    log_write("\t[EXIT] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    log_write_error("[SHUTDOWN] end app exit (%zu ms)", ts.GetMs());

    if (App::GetLogEnable()) {
        log_write("closing log\n");
    }

    log_nxlink_exit();

    if (App::GetLogEnable()) {
        log_file_exit();
    }
}

auto App::GetVersionFromString(const char* str) -> u32 {
    if (!str) {
        return 0;
    }
    while (*str == 'v' || *str == 'V' || *str == ' ') {
        ++str;
    }
    u32 major{}, minor{}, macro{};
    std::sscanf(str, "%u.%u.%u", &major, &minor, &macro);
    return MAKEHOSVERSION(major, minor, macro);
}

auto App::IsVersionNewer(const char* current, const char* new_version) -> u32 {
    if (!current || !new_version || *current == '\0' || *new_version == '\0') {
        return 0;
    }
    return version::IsNewer(current, new_version) ? 1 : 0;
}

void App::createFramebufferResources() {
    this->swapchain = nullptr;

    // Create layout for the depth buffer
    dk::ImageLayout layout_depthbuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(s_width, s_height)
        .initialize(layout_depthbuffer);

    // Create the depth buffer
    this->depthBuffer_mem = this->pool_images->allocate(layout_depthbuffer.getSize(), layout_depthbuffer.getAlignment());
    this->depthBuffer.initialize(layout_depthbuffer, this->depthBuffer_mem.getMemBlock(), this->depthBuffer_mem.getOffset());

    // Create layout for the framebuffers
    dk::ImageLayout layout_framebuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(s_width, s_height)
        .initialize(layout_framebuffer);

    // Create the framebuffers
    std::array<DkImage const*, NumFramebuffers> fb_array;
    const u64 fb_size  = layout_framebuffer.getSize();
    const uint32_t fb_align = layout_framebuffer.getAlignment();
    for (unsigned i = 0; i < fb_array.size(); i++) {
        // Allocate a framebuffer
        this->framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
        this->framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

        // Generate a command list that binds it
        dk::ImageView colorTarget{ framebuffers[i] }, depthTarget{ depthBuffer };
        this->cmdbuf.bindRenderTargets(&colorTarget, &depthTarget);
        this->framebuffer_cmdlists[i] = cmdbuf.finishList();

        // Fill in the array for use later by the swapchain creation code
        fb_array[i] = &framebuffers[i];
    }

    // Create the swapchain using the framebuffers
    this->swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), fb_array}.create();

    // Generate the main rendering cmdlist
    this->recordStaticCommands();
}

void App::destroyFramebufferResources() {
    // Return early if we have nothing to destroy
    if (!this->swapchain) {
        return;
    }

    this->queue.waitIdle();
    this->cmdbuf.clear();
    swapchain.destroy();

    // Destroy the framebuffers
    for (unsigned i = 0; i < NumFramebuffers; i++) {
        framebuffers_mem[i].destroy();
    }

    // Destroy the depth buffer
    this->depthBuffer_mem.destroy();
}

void App::recordStaticCommands() {
    // Initialize state structs with deko3d defaults
    dk::RasterizerState rasterizerState;
    dk::ColorState colorState;
    dk::ColorWriteState colorWriteState;
    dk::BlendState blendState;

    // Configure the viewport and scissor
    this->cmdbuf.setViewports(0, { { 0.0f, 0.0f, (float)s_width, (float)s_height, 0.0f, 1.0f } });
    this->cmdbuf.setScissors(0, { { 0, 0, (u32)s_width, (u32)s_height } });

    // Clear the color and depth buffers
    this->cmdbuf.clearColor(0, DkColorMask_RGBA, 0.2f, 0.3f, 0.3f, 1.0f);
    this->cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

    // Bind required state
    this->cmdbuf.bindRasterizerState(rasterizerState);
    this->cmdbuf.bindColorState(colorState);
    this->cmdbuf.bindColorWriteState(colorWriteState);

    this->render_cmdlist = this->cmdbuf.finishList();
}

} // namespace sphaira
