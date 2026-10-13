#include "app_frame_buffer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "evman.hpp"
#include "fs.hpp"
#include "ftpsrv_helper.hpp"
#include "haze_helper.hpp"
#include "image.hpp"
#include "nacp_util.hpp"
#include "net.hpp"
#include "ntp.hpp"
#include "nxlink.h"
#include "forwarder_auto_install.hpp"
#include "ui/first_start.hpp"
#include "ui/menus/main_menu.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include "utils/devoptab_common.hpp"
#include "utils/devoptab_curl_thread.hpp"

extern "C" NVGcontext* nvgCreateDk(nvg::DkRenderer* renderer, int flags);
#include <minIni.h>
#if DOCS_DEMO
#include "demo/demo_scene.hpp"
#endif
#include <usbhsfs.h>
#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
    extern u32 __nx_applet_exit_mode;
}

namespace sphaira {

extern App* g_app;
bool LoadThemeMeta(const fs::FsPath& path, ThemeMeta& meta);

namespace {

void deko3d_error_cb(void* userData, const char* context, DkResult result, const char* message) {
    switch (result) {
        case DkResult_Success:
            break;

        case DkResult_Fail:
            log_write("[DkResult_Fail] %s\n", message);
            App::Notify("DkResult_Fail");
            break;

        case DkResult_Timeout:
            log_write("[DkResult_Timeout] %s\n", message);
            App::Notify("DkResult_Timeout");
            break;

        case DkResult_OutOfMemory:
            log_write("[DkResult_OutOfMemory] %s\n", message);
            App::Notify("DkResult_OutOfMemory");
            break;

        case DkResult_NotImplemented:
            log_write("[DkResult_NotImplemented] %s\n", message);
            App::Notify("DkResult_NotImplemented");
            break;

        case DkResult_MisalignedSize:
            log_write("[DkResult_MisalignedSize] %s\n", message);
            App::Notify("DkResult_MisalignedSize");
            break;

        case DkResult_MisalignedData:
            log_write("[DkResult_MisalignedData] %s\n", message);
            App::Notify("DkResult_MisalignedData");
            break;

        case DkResult_BadInput:
            log_write("[DkResult_BadInput] %s\n", message);
            App::Notify("DkResult_BadInput");
            break;

        case DkResult_BadFlags:
            log_write("[DkResult_BadFlags] %s\n", message);
            App::Notify("DkResult_BadFlags");
            break;

        case DkResult_BadState:
            log_write("[DkResult_BadState] %s\n", message);
            App::Notify("DkResult_BadState");
            break;
    }
}

void on_applet_focus_state(App* app) {
    switch (appletGetFocusState()) {
        case AppletFocusState_InFocus:
            log_write("[APPLET] AppletFocusState_InFocus\n");
            // App::Notify("AppletFocusState_InFocus");
            // coming back from sleep (or from another applet) leaves a server's
            // listening socket detached from the interface, see net::NotifyResume.
            net::NotifyResume();
            break;

        case AppletFocusState_OutOfFocus:
            log_write("[APPLET] AppletFocusState_OutOfFocus\n");
            // App::Notify("AppletFocusState_OutOfFocus");
            break;

        case AppletFocusState_Background:
            log_write("[APPLET] AppletFocusState_Background\n");
            // App::Notify("AppletFocusState_Background");
            break;
    }

    if (!app->m_widgets.empty()) {
        app->m_widgets.back()->OnFocusGained();
    }
}

void on_applet_operation_mode(App* app) {
    switch (appletGetOperationMode()) {
        case AppletOperationMode_Handheld:
            log_write("[APPLET] AppletOperationMode_Handheld\n");
            App::Notify("Switch-Handheld!"_i18n);
            break;

        case AppletOperationMode_Console:
            log_write("[APPLET] AppletOperationMode_Console\n");
            App::Notify("Switch-Docked!"_i18n);
            break;
    }
}

void applet_on_performance_mode(App* app) {
    switch (appletGetPerformanceMode()) {
        case ApmPerformanceMode_Invalid:
            log_write("[APPLET] ApmPerformanceMode_Invalid\n");
            App::Notify("ApmPerformanceMode_Invalid");
            break;

        case ApmPerformanceMode_Normal:
            log_write("[APPLET] ApmPerformanceMode_Normal\n");
            App::Notify("ApmPerformanceMode_Normal");
            break;

        case ApmPerformanceMode_Boost:
            log_write("[APPLET] ApmPerformanceMode_Boost\n");
            App::Notify("ApmPerformanceMode_Boost");
            break;
    }
}

void appplet_hook_calback(AppletHookType type, void *param) {
    auto app = static_cast<App*>(param);
    switch (type) {
        case AppletHookType_OnFocusState:
            // App::Notify("AppletHookType_OnFocusState");
            on_applet_focus_state(app);
            break;

        case AppletHookType_OnOperationMode:
            // App::Notify("AppletHookType_OnOperationMode");
            on_applet_operation_mode(app);
            break;

        case AppletHookType_OnPerformanceMode:
            // App::Notify("AppletHookType_OnPerformanceMode");
            applet_on_performance_mode(app);
            break;

        case AppletHookType_OnExitRequest:
            // App::Notify("AppletHookType_OnExitRequest");
            devoptab::common::RequestCurlShutdown();
            curl::RequestShutdown();
            App::Exit();
            break;

        case AppletHookType_OnResume:
            // App::Notify("AppletHookType_OnResume");
            net::NotifyResume();
            break;

        case AppletHookType_OnCaptureButtonShortPressed:
            // App::Notify("AppletHookType_OnCaptureButtonShortPressed");
            break;

        case AppletHookType_OnAlbumScreenShotTaken:
            // App::Notify("AppletHookType_OnAlbumScreenShotTaken");
            break;

        case AppletHookType_RequestToDisplay:
            // App::Notify("AppletHookType_RequestToDisplay");
            break;

        case AppletHookType_Max:
            assert(!"AppletHookType_Max hit");
            break;
    }
}

} // namespace

auto GetFrameBufferSize() -> FrameBufferSize {
    FrameBufferSize fb{};

    switch (appletGetOperationMode()) {
        case AppletOperationMode_Handheld:
            fb.size.x = 1280;
            fb.size.y = 720;
            break;

        case AppletOperationMode_Console:
            fb.size.x = 1920;
            fb.size.y = 1080;
            break;
    }

    fb.scale.x = fb.size.x / SCREEN_WIDTH;
    fb.scale.y = fb.size.y / SCREEN_HEIGHT;
    return fb;
}

void nxlink_callback(const NxlinkCallbackData *data) {
    App::NotifyFlashLed();
    evman::push(*data, false);
}

App::App(const char* argv0) {
    TimeStamp ts;

    // Startup has a lot of blocking steps (usb mass storage enumeration, ftp,
    // the sd card scan) and "it takes ten seconds" is not something to guess
    // at. Each phase reports how long it took, so a slow launch can be read
    // off the log instead of theorised about. No-ops when logging is off.
    TimeStamp phase;
    const auto mark = [&phase](const char* what) {
        const auto ms = phase.GetMs();
        if (ms >= 1) {
            log_write("[boot] %-22s %4zu ms\n", what, ms);
        }
        phase.Update();
    };

    // boost mode is enabled in userAppInit().
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    g_app = this;
    mutexInit(&m_install_session_mutex);
    log_nxlink_init();
    m_start_timestamp = armGetSystemTick();
    m_app_path = {};
    if (!std::strncmp(argv0, "sdmc:/", 6)) {
        // memmove(path, path + 5, strlen(path)-5);
        std::strncpy(m_app_path, argv0 + 5, sizeof(m_app_path) - 1);
        m_app_path.s[sizeof(m_app_path) - 1] = '\0';
    } else {
        m_app_path = argv0;
    }

    // set if we are hbmenu
    if (IsHbmenu()) {
        __nx_applet_exit_mode = 1;
    }

    // migrate legacy config dir if present; if both exist (e.g. a stray
    // /config/sphaira left by running upstream sphaira once), the kefir dir
    // wins and the legacy one is ignored. never abort here: a leftover
    // folder on the sd card must not brick the app at boot.
    fs::FsNativeSd fs;
    const bool has_legacy_data = fs.DirExists(paths::LEGACY_DATA_ROOT);
    const bool has_kefir_data = fs.DirExists(paths::DATA_ROOT);
    if (has_legacy_data && !has_kefir_data) {
        fs.RenameDirectory(paths::LEGACY_DATA_ROOT, paths::DATA_ROOT);
    }

    fs.CreateDirectoryRecursively(paths::DATA_ROOT);
    fs.CreateDirectory(paths::DATA_ROOT + "/assoc");
    fs.CreateDirectory(paths::DATA_ROOT + "/themes");
    fs.CreateDirectory(paths::DATA_ROOT + "/github");
    fs.CreateDirectory(paths::DATA_ROOT + "/i18n");

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto app = static_cast<App*>(UserData);

        if (!std::strcmp(Section, INI_SECTION)) {
            if (app->m_nxlink_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_mtp_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_ftp_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_write_protect.LoadFrom(Key, Value)) {}
            else if (app->m_log_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_account_link_prompt_skip.LoadFrom(Key, Value)) {}
            else if (app->m_replace_hbmenu.LoadFrom(Key, Value)) {}
            else if (app->m_theme_path.LoadFrom(Key, Value)) {}
            else if (app->m_12hour_time.LoadFrom(Key, Value)) {}
            else if (app->m_language.LoadFrom(Key, Value)) {}
            else if (app->m_left_menu.LoadFrom(Key, Value)) {}
            else if (app->m_right_menu.LoadFrom(Key, Value)) {}
            else if (app->m_god_mode.LoadFrom(Key, Value)) {}
            else if (app->m_install_sysmmc.LoadFrom(Key, Value)) {}
            else if (app->m_install_emummc.LoadFrom(Key, Value)) {}
            else if (app->m_install_location.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_address_space.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_profile_select.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_screenshot.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_video_capture.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_svc_debug.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_cpu_cores.LoadFrom(Key, Value)) {}
            else if (app->m_forwarder_ask.LoadFrom(Key, Value)) {}
            else if (app->m_install_reserve_mb.LoadFrom(Key, Value)) {}
            else if (app->m_install_reserve_sd_mb.LoadFrom(Key, Value)) {}
            else if (app->m_progress_boost_mode.LoadFrom(Key, Value)) {}
            else if (app->m_blank_mode.LoadFrom(Key, Value)) {}
            else if (app->m_blank_brightness.LoadFrom(Key, Value)) {}
            else if (app->m_blank_timeout.LoadFrom(Key, Value)) {}
            else if (app->m_saver_oled.LoadFrom(Key, Value)) {}
            else if (app->m_saver_fields.LoadFrom(Key, Value)) {}
            else if (app->m_allow_downgrade.LoadFrom(Key, Value)) {}
            else if (app->m_skip_if_already_installed.LoadFrom(Key, Value)) {}
            else if (app->m_ticket_only.LoadFrom(Key, Value)) {}
            else if (app->m_skip_base.LoadFrom(Key, Value)) {}
            else if (app->m_skip_patch.LoadFrom(Key, Value)) {}
            else if (app->m_skip_addon.LoadFrom(Key, Value)) {}
            else if (app->m_skip_data_patch.LoadFrom(Key, Value)) {}
            else if (app->m_skip_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_skip_nca_hash_verify.LoadFrom(Key, Value)) {}
            else if (app->m_skip_rsa_header_fixed_key_verify.LoadFrom(Key, Value)) {}
            else if (app->m_skip_rsa_npdm_fixed_key_verify.LoadFrom(Key, Value)) {}
            else if (app->m_ignore_distribution_bit.LoadFrom(Key, Value)) {}
            else if (app->m_convert_to_common_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_convert_to_standard_crypto.LoadFrom(Key, Value)) {}
            else if (app->m_lower_master_key.LoadFrom(Key, Value)) {}
            else if (app->m_lower_system_version.LoadFrom(Key, Value)) {}
        } else if (!std::strcmp(Section, "accessibility")) {
            if (app->m_text_scroll_speed.LoadFrom(Key, Value)) {}
        }

        return 1;
    };

    // load all configs ahead of time, as this is actually faster than
    // loading each config one by one as it avoids re-opening the file multiple times.
    ini_browse(cb, this, CONFIG_PATH);
    m_blank_timeout.Get();

    // Migration: if install_location is not in ini, but install_sd is, migrate it.
    if (ini_getl(INI_SECTION, "install_location", -1, CONFIG_PATH) == -1) {
        char buf[8]{};
        if (ini_gets(INI_SECTION, "install_sd", "", buf, sizeof(buf), CONFIG_PATH) > 0) {
            m_install_location.Set(ini_getbool(INI_SECTION, "install_sd", true, CONFIG_PATH) ? 0 : 1);
        }
    }

    i18n::ScanAvailableLanguages();

    const bool has_language_in_ini = ini_haskey(INI_SECTION, "language", CONFIG_PATH) != 0;
    std::string validated_code;
    if (has_language_in_ini) {
        const std::string saved_lang = m_language.Get();
        validated_code = i18n::MigrateLegacyLanguage(saved_lang, true);
    }

    if (!validated_code.empty()) {
        m_language_chosen = true;
        if (m_language.Get() != validated_code) {
            m_language.Set(validated_code);
        }
    } else {
        m_language_chosen = false;
    }

    // no choice yet: the first-start page and the language list speak the
    // console's language, so the page is readable before any choice is made.
    std::string startup_language = m_language_chosen ? m_language.Get() : i18n::MatchSystemLanguage();
    if (startup_language.empty()) {
        startup_language = "en";
    }
    i18n::init(startup_language);

    if (App::GetLogEnable()) {
        log_file_init();
        log_write("hello world v%s\n", APP_VERSION_HASH);
        App::Notify("Warning! Logs are enabled, Kefir Hub will run slowly!"_i18n);
    }

    if (log_is_init()) {
        SetSysFirmwareVersion fw_version{};
        setsysInitialize();
        ON_SCOPE_EXIT(setsysExit());
        setsysGetFirmwareVersion(&fw_version);

        log_write("[version] platform: %s\n", fw_version.platform);
        log_write("[version] version_hash: %s\n", fw_version.version_hash);
        log_write("[version] display_version: %s\n", fw_version.display_version);
        log_write("[version] display_title: %s\n", fw_version.display_title);

        splInitialize();
        ON_SCOPE_EXIT(splExit());

        u64 out{};
        splGetConfig((SplConfigItem)65000, &out);
        log_write("[ams] version: %lu.%lu.%lu\n", (out >> 56) & 0xFF, (out >> 48) & 0xFF, (out >> 40) & 0xFF);
        log_write("[ams] target version: %lu.%lu.%lu\n", (out >> 24) & 0xFF, (out >> 16) & 0xFF, (out >> 8) & 0xFF);
        log_write("[ams] key gen: %lu\n", (out >> 32) & 0xFF);

        splGetConfig((SplConfigItem)65003, &out);
        log_write("[ams] hash: %lx\n", out);

        splGetConfig((SplConfigItem)65010, &out);
        log_write("[ams] usb 3.0 enabled: %lu\n", out);
    }

    // get emummc config.
    alignas(0x1000) AmsEmummcPaths paths{};
    SecmonArgs args{};
    args.X[0] = 0xF0000404; /* smcAmsGetEmunandConfig */
    args.X[1] = 0; /* EXO_EMUMMC_MMC_NAND*/
    args.X[2] = (u64)&paths; /* out path */
    svcCallSecureMonitor(&args);
    const Result smc_rc = static_cast<Result>(args.X[0]);
    m_emummc_paths = paths;

    constexpr u32 StorageMagic = 0x30534645; // 'EFS0'
    const u32 magic = static_cast<u32>(args.X[1] & 0xFFFFFFFF);
    const u32 type = static_cast<u32>((args.X[1] >> 32) & 0xFFFFFFFF);

    if (R_SUCCEEDED(smc_rc) && magic == StorageMagic) {
        m_emummc_type = type;
    }

    m_is_emummc = false;
    u64 spl_val = 0;
    if (R_SUCCEEDED(splInitialize())) {
        if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65007), &spl_val))) {
            m_is_emummc = (spl_val != 0);
        }
        splExit();
    }

    log_write("[emummc] enabled: %u (type: %u)\n", App::IsEmummc(), m_emummc_type);
    if (App::IsEmummc()) {
        log_write("[emummc] file based path: %s\n", m_emummc_paths.file_based_path);
        log_write("[emummc] nintendo path: %s\n", m_emummc_paths.nintendo);
    }



    mark("config + emummc");

    if (App::GetFtpEnable()) {
        ftpsrv::Init();
        // enables background install for files dropped into the FTP "install" folder.
        ui::menu::stream::BackgroundInstaller::RegisterMtpCallbacks();
    }

    mark("ftp");

    if (App::GetNxlinkEnable()) {
        nxlinkInitialize(nxlink_callback);
    }

    if (App::GetWriteProtect()) {
        usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
    }

    // MTP and USB storage may both be on (both are by default): the port goes to
    // whatever is plugged in. A PC (low-power charger) starts MTP in the main loop,
    // and haze::Init drops the host stack; haze::Exit gives it back for a drive.

    mark("nxlink");

    if (App::GetHddEnable()) {
        usbHsFsInitialize(1);
    }

    mark("usb mass storage");

    curl::Init();

#ifdef USE_NVJPG
    // this has to be init before deko3d.
    nj::initialize();
    m_decoder.initialize();
#endif

    mark("curl");

    // get current size of the framebuffer
    const auto fb = GetFrameBufferSize();
    s_width = fb.size.x;
    s_height = fb.size.y;
    m_scale = fb.scale;

    // Create the deko3d device
    this->device = dk::DeviceMaker{}
        .setCbDebug(deko3d_error_cb)
        .create();

    // Create the main queue
    this->queue = dk::QueueMaker{this->device}
        .setFlags(DkQueueFlags_Graphics)
        .create();

    // Create the memory pools
    this->pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
    this->pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
    this->pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);

    // Create the static command buffer and feed it freshly allocated memory
    this->cmdbuf = dk::CmdBufMaker{this->device}.create();
    const CMemPool::Handle cmdmem = this->pool_data->allocate(this->StaticCmdSize);
    this->cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

    // Create the framebuffer resources
    this->createFramebufferResources();

    this->renderer.emplace(s_width, s_height, this->device, this->queue, *this->pool_images, *this->pool_code, *this->pool_data);
    this->vg = nvgCreateDk(&*this->renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);

    // not sure if these are meant to be deleted or not...
    // A Chinese UI language takes the Chinese shared font as the main face so that
    // shared CJK code points get Chinese glyph shapes; Standard then serves as fallback.
    const std::string ui_language{i18n::GetCurrentLanguageCode()};
    PlSharedFontType standard_type = PlSharedFontType_Standard;
    if (ui_language == "zh") {
        standard_type = PlSharedFontType_ChineseSimplified;
    } else if (ui_language == "zhtw") {
        standard_type = PlSharedFontType_ChineseTraditional;
    }

    PlFontData font_standard, font_extended, font_lang;
    plGetSharedFontByType(&font_standard, standard_type);
    plGetSharedFontByType(&font_extended, PlSharedFontType_NintendoExt);

    auto standard_font = nvgCreateFontMem(this->vg, "Standard", (unsigned char*)font_standard.address, font_standard.size, 0);
    auto extended_font = nvgCreateFontMem(this->vg, "Extended", (unsigned char*)font_extended.address, font_extended.size, 0);
    nvgAddFallbackFontId(this->vg, standard_font, extended_font);

    const PlSharedFontType lang_font[] = {
        PlSharedFontType_Standard,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
        PlSharedFontType_KO,
    };

    for (auto type : lang_font) {
        if (type == standard_type) {
            continue;
        }
        if (R_SUCCEEDED(plGetSharedFontByType(&font_lang, type))) {
            char name[32];
            snprintf(name, sizeof(name), "Lang_%u", font_lang.type);
            auto lang_font = nvgCreateFontMem(this->vg, name, (unsigned char*)font_lang.address, font_lang.size, 0);
            nvgAddFallbackFontId(this->vg, standard_font, lang_font);
        } else {
            log_write("failed plGetSharedFontByType(%d)\n", type);
        }
    }

    mark("deko3d + fonts");

    ScanThemeEntries();

    // try and load previous theme, default to previous version otherwise.
    fs::FsPath theme_path = m_theme_path.Get();
    ThemeMeta theme_meta;
    if (R_SUCCEEDED(romfsInit())) {
        ON_SCOPE_EXIT(romfsExit());
        if (!LoadThemeMeta(theme_path, theme_meta)) {
            log_write("failed to load meta using default\n");
            theme_path = DEFAULT_THEME_PATH;
            LoadThemeMeta(theme_path, theme_meta);
        }
    }
    log_write("loading theme from: %s\n", theme_meta.ini_path.s);
    LoadTheme(theme_meta);

    // find theme index using the path of the theme.ini
    for (u64 i = 0; i < m_theme_meta_entries.size(); i++) {
        if (m_theme.meta.ini_path == m_theme_meta_entries[i].ini_path) {
            m_theme_index = i;
            break;
        }
    }

    mark("theme");

    appletHook(&m_appletHookCookie, appplet_hook_calback, this);

    hidInitializeTouchScreen();
    hidInitializeGesture();
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    // padInitializeDefault(&m_pad);
    padInitializeAny(&m_pad);


    const auto loader_info_size = envGetLoaderInfoSize();
    if (loader_info_size) {
        if (loader_info_size >= 8 && !std::memcmp(envGetLoaderInfo(), "sphaira", 7)) {
            log_write("launching from sphaira created forwarder\n");
            m_is_launched_via_sphaira_forwader = true;
        } else {
            log_write("launching from unknown forwader: %.*s size: %zu\n", (int)loader_info_size, envGetLoaderInfo(), loader_info_size);
        }
    } else {
        log_write("not launching from forwarder\n");
    }

    ini_putl(GetExePath(), "timestamp", m_start_timestamp, App::PLAYLOG_PATH);

    // background clock sync: idles until there is a connection, corrects the
    // rtc, then goes back to sleep. Never prompts, never blocks startup.
    mark("hid + loader info");

    // load default image
    InitDefaultImage();

    mark("default image");

    // background clock sync & forwarder check: start after graphics initialization
    ntp::Start();
    forwarder_auto::StartCheck();

    App::Push<ui::menu::main::MainMenu>();
    if (App::NeedsLanguageSelection()) {
        // a full page over the menu (which is not drawn under it), not a list
        // dropped onto a screen the user has not been introduced to.
        App::Push<ui::FirstStart>();
    }
#if DOCS_DEMO
    demo::StartScene();
#endif
    log_write("\n\tfinished app constructor, time taken: %.2fs %zums\n\n", ts.GetSecondsD(), ts.GetMs());
}

} // namespace sphaira
