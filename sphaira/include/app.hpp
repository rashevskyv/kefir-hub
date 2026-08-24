#pragma once

#include "nanovg.h"
#include "nanovg/dk_renderer.hpp"
#include "ui/widget.hpp"
#include "ui/notification.hpp"
#include "ui/screensaver.hpp"
#include "owo.hpp"
#include "option.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "app_paths.hpp"

#ifdef USE_NVJPG
#include <nvjpg.hpp>
#endif
#include <switch.h>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include <utility>

namespace sphaira {

enum SoundEffect {
    SoundEffect_Focus,
    SoundEffect_Scroll,
    SoundEffect_Limit,
    SoundEffect_Startup,
    SoundEffect_Install,
    SoundEffect_Error,
    SoundEffect_MAX,
};

enum class LaunchType {
    Normal,
    Forwader_Unknown,
    Forwader_Sphaira,
};

struct AmsEmummcPaths {
    char file_based_path[0x80];
    char nintendo[0x80];
};

// todo: why is this global???
void DrawElement(float x, float y, float w, float h, ThemeEntryID id);
void DrawElement(const Vec4&, ThemeEntryID id);
class App {
public:
    App(const char* argv0);
    ~App();
    void Loop();

    static App* GetApp();

    static void Exit();
    static void ExitRestart();
    static auto GetVg() -> NVGcontext*;

    static void Push(std::unique_ptr<ui::Widget>&&);

    template<ui::DerivedFromWidget T, typename... Args>
    static void Push(Args&&... args) {
        Push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // owns a single ProgressBox outside of the widget stack: it keeps running
    // and drawing (as a corner badge or, expanded via L3, a non-blocking
    // dialog) without ever consuming Update() from the active menu. used for
    // transfers triggered from outside the UI (e.g. MTP installs), where the
    // user should be able to keep navigating while it runs in the background.
    // reclaimed automatically once its worker thread finishes.
    // returns false if the slot is already taken (the box is then destroyed);
    // check HasActiveTransfer() first if the caller has state to unwind.
    static auto PushTransfer(std::unique_ptr<ui::ProgressBox>&& pbox) -> bool;
    static auto HasActiveTransfer() -> bool;

    // Drop a click/drag that started before a blocking applet (swkbd) so the
    // same finger-up is not delivered to the menu underneath on return.
    static void ResetTouchAfterApplet();

    // pops all widgets above a menu
    static void PopToMenu();
    static void Pop();

    // true if this widget owns the footer hint row, i.e. it is the top of the
    // widget stack (or the page a top-of-stack container delegates to). Every
    // other widget on the stack is covered and draws no hints. Child objects
    // that aren't on the stack at all are drawn by their parent and keep
    // theirs. See ui/layout.hpp for the chrome rules.
    static auto OwnsFooter(const ui::Widget* widget) -> bool;

    // union of the opaque regions claimed by the widgets stacked above the
    // active menu. The menu skips the header chrome that falls inside it so a
    // side panel owns that corner instead of being ghosted through.
    static auto GetChromeOcclusion() -> Vec4;

    // this is thread safe
    static void Notify(std::string text, ui::NotifEntry::Side side = ui::NotifEntry::Side::RIGHT);
    static void Notify(ui::NotifEntry entry);
    static void NotifyFlashLed();

    // if R_FAILED(rc), pushes error box. returns rc passed in.
    static Result PushErrorBox(Result rc, const std::string& message);

    static auto GetThemeMetaList() -> std::span<ThemeMeta>;
    static void SetTheme(s64 theme_index);
    static auto GetThemeIndex() -> s64;

    static auto GetDefaultImage() -> int;
    static auto GetDefaultImageData() -> std::span<const u8>;

    // returns argv[0]
    static auto GetExePath() -> fs::FsPath;
    // returns true if we are hbmenu.
    static auto IsHbmenu() -> bool;

    static auto GetMtpEnable() -> bool;
    static auto GetMtpShowSd() -> bool;
    static auto GetMtpShowInstall() -> bool;
    static auto GetMtpShowSaves() -> bool;
    static auto GetMtpShowRawSaves() -> bool;
    static auto GetMtpShowRawSystemSaves() -> bool;
    static auto GetMtpShowGames() -> bool;
    static auto GetMtpNameSd() -> std::string;
    static auto GetMtpNameInstall() -> std::string;
    // extra folders exposed as MTP storages (absolute SD paths).
    static auto GetMtpFolders() -> std::vector<std::string>;
    static auto GetFtpEnable() -> bool;
    static auto GetFtpAnon() -> bool;
    static auto GetFtpUser() -> std::string;
    static auto GetFtpPass() -> std::string;
    static auto GetFtpPort() -> long;
    static auto GetNxlinkEnable() -> bool;
    static auto GetNtpEnable() -> bool;
    static auto GetHddEnable() -> bool;
    static auto GetWriteProtect() -> bool;
    static auto GetWebdavUrlName() -> std::string;
    static auto GetWebdavUrl() -> std::string;
    static auto GetWebdavUser() -> std::string;
    static auto GetWebdavPass() -> std::string;
    static auto GetLogEnable() -> bool;
    static auto GetAutoUpdateEnable() -> bool;
    static auto GetAutoUpdateMode() -> long;
    static void SetAutoUpdateMode(long mode);
    static auto GetAutoUpdateSkip() -> std::string;
    static void SetAutoUpdateSkip(std::string version);
    static auto GetReplaceHbmenuEnable() -> bool;
    static auto GetInstallEnable() -> bool;
    static auto GetInstallSysmmcEnable() -> bool;
    static auto GetInstallEmummcEnable() -> bool;
    static auto GetInstallSdEnable() -> bool;
    static auto GetInstallLocation() -> long;
    static auto GetInstallReserveMb() -> long;
    static auto GetInstallReserveSdMb() -> long;
    static auto GetForwarderOptions() -> ForwarderOptions;
    static auto GetForwarderAsk() -> bool;
    static auto GetForwarderAddressSpace() -> long;
    static void SetForwarderAddressSpace(long mode);
    static auto GetAnimatedWavesEnable() -> bool;
    static auto GetWaveColorDark() -> std::string;
    static auto GetWaveColorLight() -> std::string;
    static auto Get12HourTimeEnable() -> bool;
    static auto GetLanguage() -> long;
    static auto GetTextScrollSpeed() -> long;
    static auto GetGodModeEnabled() -> bool;
    static auto GetProgressActive() -> bool;
    static auto GetSaveSettingsGlobally() -> bool;
    static auto GetSaveShowInstalled() -> bool;
    static void SetSaveShowInstalled(bool enable);
    static auto GetSaveShowDeleted() -> bool;
    static void SetSaveShowDeleted(bool enable);
    static auto GetSaveShowBackups() -> bool;
    static void SetSaveShowBackups(bool enable);
    static auto GetSaveAutoBackupOnRestore() -> bool;
    static void SetSaveAutoBackupOnRestore(bool enable);
    static auto GetSaveCompressBackup() -> bool;
    static void SetSaveCompressBackup(bool enable);
    static auto GetSaveAutosync() -> bool;
    static void SetSaveAutosync(bool enable);
    static auto GetSaveRestoreIncludeRemote() -> bool;
    static void SetSaveRestoreIncludeRemote(bool enable);
    static auto GetSaveDefaultLocation() -> std::string;
    static void SetSaveDefaultLocation(std::string key);

    // Minus-key screen blanking during the install queue, see ui/screensaver.hpp.
    static auto GetBlankMode() -> long;
    static auto GetBlankBrightness() -> long;
    static auto GetBlankTimeout() -> long;
    static auto GetSaverOled() -> bool;
    static auto GetSaverFields() -> long;
    static void SetBlankMode(long mode);
    static void SetBlankBrightness(long percent);
    static void SetBlankTimeout(long timeout_sec);
    static void SetSaverOled(bool enable);
    static void SetSaverField(long field, bool enable);

    // the folders the file browser has mounted, exposed by *every* network share
    // next to the microSD card: as root devices over FTP, as entries of the web
    // server's root page. one set shared by all transports -- picking FTP in the
    // "Mount over" popup chooses which server to bring up, not which server gets
    // to see them. empty when nothing is mounted.
    //
    // deliberately not persisted: a mount lasts for the session, so a reboot
    // never leaves a share pointing at a folder the user has forgotten about.
    static auto GetMountedFolders() -> std::vector<fs::FsPath>;
    static void SetMountedFolders(std::vector<fs::FsPath> paths);

    static void SetMtpEnable(bool enable);
    static void SetMtpShowSd(bool enable);
    static void SetMtpShowInstall(bool enable);
    static void SetMtpShowSaves(bool enable);
    static void SetMtpShowRawSaves(bool enable);
    static void SetMtpShowRawSystemSaves(bool enable);
    static void SetMtpShowGames(bool enable);
    static void SetMtpNameSd(std::string value);
    static void SetMtpNameInstall(std::string value);
    static void SetMtpFolders(const std::vector<std::string>& folders);
    static void AddMtpFolder(const std::string& path);
    static void RemoveMtpFolder(const std::string& path);
    static void SetFtpEnable(bool enable);
    static void SetFtpAnon(bool enable);
    static void SetFtpUser(std::string value);
    static void SetFtpPass(std::string value);
    static void SetFtpPort(long port);
    static void SetNxlinkEnable(bool enable);
    static void SetNtpEnable(bool enable);
    static void SetHddEnable(bool enable);
    static void SetWriteProtect(bool enable);
    static void SetWebdavUrl(std::string value);
    static void SetLogEnable(bool enable);
    static void SetAutoUpdateEnable(bool enable);
    static void SetReplaceHbmenuEnable(bool enable);
    static void SetInstallLocation(long location);
    static void SetInstallReserveMb(long reserve_mb);
    static void SetInstallReserveSdMb(long reserve_mb);
    static void SetAnimatedWavesEnable(bool enable);
    static void Set12HourTimeEnable(bool enable);
    static void SetLanguage(long index, bool prompt_restart = true);
    static void SetTextScrollSpeed(long index);
    static void SetProgressActive(bool active);

    static auto Install(OwoConfig& config) -> Result;
    static auto Install(ui::ProgressBox* pbox, OwoConfig& config) -> Result;

    static void PlaySoundEffect(SoundEffect effect);

    static void DisplayAdvancedOptions(bool left_side = true);
    static void DisplayInstallOptions(bool left_side = true);
    static void DisplayForwarderOptions(bool left_side = true);
    static void DisplayDumpOptions(bool left_side = true);

    // helper for sidebar options to toggle install on/off
    static void ShowEnableInstallPromptOption(option::OptionBool& option, bool& enable);
    // displays an option box to enable installing, shows warning.
    static void ShowEnableInstallPrompt();

    void Draw();
    // per frame timing summary + stall reporting into the log.
    void LogFrame(double delta_ms);
    void Update();
    void Poll();

    // void DrawElement(float x, float y, float w, float h, ui::ThemeEntryID id);
    auto LoadElementImage(std::string_view value) -> ElementEntry;
    auto LoadElementColour(std::string_view value) -> ElementEntry;
    auto LoadElement(std::string_view data, ElementType type) -> ElementEntry;

    void LoadTheme(const ThemeMeta& meta);
    void CloseTheme();
    void ScanThemes(const std::string& path);
    void ScanThemeEntries();
    void InitDefaultImage();

    // helper that converts 1.2.3 to a u32 used for comparisons.
    static auto GetVersionFromString(const char* str) -> u32;
    static auto IsVersionNewer(const char* current, const char* new_version) -> u32;

    static auto IsApplication() -> bool {
        const auto type = appletGetAppletType();
        return type == AppletType_Application || type == AppletType_SystemApplication;
    }

    static auto IsApplet() -> bool {
        return !IsApplication();
    }

    static void ShowTitleModeHelp(const std::string& feature = {});

    static auto IsOledModel() -> bool;
    static auto IsEmummc() -> bool;
    static auto IsParitionBaseEmummc() -> bool;
    static auto IsFileBaseEmummc() -> bool;
    // absolute sd path of the nand folder of the booted emummc, empty if not emummc.
    static auto GetEmummcNintendoPath() -> std::string;

    static void SetAutoSleepDisabled(bool enable) {
        static Mutex mutex{};
        static int ref_count{};
        static bool media_playback_active{};

        mutexLock(&mutex);
        ON_SCOPE_EXIT(mutexUnlock(&mutex));

        if (enable) {
            if (ref_count++ != 0) {
                return;
            }

            const auto set_rc = appletSetAutoSleepDisabled(true);
            bool disabled{};
            const auto check_rc = appletIsAutoSleepDisabled(&disabled);
            if (R_SUCCEEDED(set_rc) && R_SUCCEEDED(check_rc) && disabled) {
                log_write("[power] auto sleep disabled and verified\n");
            } else {
                log_write("[power] auto sleep set failed (set=0x%X check=0x%X value=%u)\n",
                    set_rc, check_rc, disabled);
            }

            const auto media_rc = appletSetMediaPlaybackState(true);
            media_playback_active = R_SUCCEEDED(media_rc);
            if (!media_playback_active) {
                log_write("[power] appletSetMediaPlaybackState(true) failed: 0x%X\n", media_rc);
            }
        } else {
            if (!ref_count || --ref_count != 0) {
                return;
            }

            const auto set_rc = appletSetAutoSleepDisabled(false);
            if (R_FAILED(set_rc)) {
                log_write("[power] failed to restore auto sleep: 0x%X\n", set_rc);
            }
            if (media_playback_active) {
                const auto media_rc = appletSetMediaPlaybackState(false);
                if (R_FAILED(media_rc)) {
                    log_write("[power] failed to release media playback state: 0x%X\n", media_rc);
                }
                media_playback_active = false;
            }
        }
    }

    static void SetBoostMode(bool enable, bool force = false) {
        static Mutex mutex{};
        static int ref_count{};

        mutexLock(&mutex);
        ON_SCOPE_EXIT(mutexUnlock(&mutex));

        if (enable) {
            ref_count++;
            appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
        } else {
            if (ref_count) {
                ref_count--;
            }
        }

        if (!ref_count || force) {
            ref_count = 0;
            appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        }
    }

    static auto GetAccountList() -> std::vector<AccountProfileBase> {
        std::vector<AccountProfileBase> out;

        AccountUid uids[ACC_USER_LIST_SIZE];
        s32 account_count;
        if (R_SUCCEEDED(accountListAllUsers(uids, std::size(uids), &account_count))) {
            for (s32 i = 0; i < account_count; i++) {
                AccountProfile profile;
                if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
                    ON_SCOPE_EXIT(accountProfileClose(&profile));

                    AccountProfileBase base;
                    if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
                        // sometimes the uid for the acc can differ to the base.
                        base.uid = uids[i];
                        log_write("[ACC] found uid: 0x%016lX%016lX\n", uids[i].uid[0], uids[i].uid[1]);
                        log_write("[ACC] base  uid: 0x%016lX%016lX\n", base.uid.uid[0], base.uid.uid[1]);
                        out.emplace_back(base);
                    }
                }
            }
        }

        return out;
    }

// private:
    static inline const auto CONFIG_PATH = paths::CONFIG.c_str();
    static inline const auto PLAYLOG_PATH = paths::PLAYLOG.c_str();
    static constexpr inline auto INI_SECTION = "config";
    static constexpr inline auto DEFAULT_THEME_PATH = "romfs:/themes/default_theme.ini";

    fs::FsPath m_app_path;
    u64 m_start_timestamp{};
    int m_default_image{};

    bool m_is_launched_via_sphaira_forwader{};

    NVGcontext* vg{};
    PadState m_pad{};
    TouchInfo m_touch_info{};
    Controller m_controller{};
    std::vector<ThemeMeta> m_theme_meta_entries;

    Vec2 m_scale{1, 1};

    std::vector<std::unique_ptr<ui::Widget>> m_widgets;
    u32 m_pop_count{};
    ui::NotifMananger m_notif_manager{};
    std::unique_ptr<ui::ProgressBox> m_active_transfer_pbox{};

    AppletHookCookie m_appletHookCookie{};

    // usb host storage hotplug watch: polled once a second while HDD is on, so
    // plugging or pulling a drive raises a notification and refreshes the file
    // browser root without the user having to do anything.
    void PollUsbStorage();
    TimeStamp m_usb_poll_ts{};
    u32 m_usb_device_count{};
    bool m_usb_poll_primed{};

    Theme m_theme{};
    fs::FsPath theme_path{};
    s64 m_theme_index{};

    AmsEmummcPaths m_emummc_paths{};
    bool m_quit{};

    // network
    option::OptionBool m_nxlink_enabled{INI_SECTION, "nxlink_enabled", true};
    option::OptionBool m_ntp_enabled{INI_SECTION, "ntp_enabled", true};
    option::OptionBool m_mtp_enabled{INI_SECTION, "mtp_enabled", false};
    option::OptionBool m_mtp_show_sd{INI_SECTION, "mtp_show_sd", true};
    option::OptionBool m_mtp_show_install{INI_SECTION, "mtp_show_install", true};
    option::OptionBool m_mtp_show_saves{INI_SECTION, "mtp_show_saves", false};
    option::OptionBool m_mtp_show_raw_saves{INI_SECTION, "mtp_show_raw_saves", false};
    option::OptionBool m_mtp_show_raw_system_saves{INI_SECTION, "mtp_show_raw_system_saves", false};
    option::OptionBool m_mtp_show_games{INI_SECTION, "mtp_show_games", false};
    option::OptionString m_mtp_name_sd{INI_SECTION, "mtp_name_sd", ""};
    option::OptionString m_mtp_name_install{INI_SECTION, "mtp_name_install", ""};
    // extra MTP storage folders, '|'-separated absolute paths ('|' is illegal in FAT names).
    option::OptionString m_mtp_folders{INI_SECTION, "mtp_folders", ""};
    option::OptionBool m_ftp_enabled{INI_SECTION, "ftp_enabled", false};
    option::OptionBool m_ftp_anon{INI_SECTION, "ftp_anon", true};
    option::OptionString m_ftp_user{INI_SECTION, "ftp_user", ""};
    option::OptionString m_ftp_pass{INI_SECTION, "ftp_pass", ""};
    option::OptionLong m_ftp_port{INI_SECTION, "ftp_port", 5000};
    option::OptionBool m_hdd_enabled{INI_SECTION, "hdd_enabled", true};
    option::OptionBool m_hdd_write_protect{INI_SECTION, "hdd_write_protect", false};
    option::OptionString m_webdav_url{INI_SECTION, "webdav_url", ""};
    option::OptionString m_webdav_user{INI_SECTION, "webdav_user", ""};
    option::OptionString m_webdav_pass{INI_SECTION, "webdav_pass", ""};

    option::OptionBool m_log_enabled{INI_SECTION, "log_enabled", false};
    option::OptionLong m_auto_update{INI_SECTION, "auto_update", 1}; // Silent; 0=Off matches old false
    option::OptionString m_auto_update_skip{INI_SECTION, "auto_update_skip", ""};
    option::OptionBool m_replace_hbmenu{INI_SECTION, "replace_hbmenu", false};
    option::OptionString m_theme_path{INI_SECTION, "theme", DEFAULT_THEME_PATH};
    option::OptionBool m_animated_waves{INI_SECTION, "animated_waves", true};
    option::OptionString m_wave_color_dark{INI_SECTION, "wave_color_dark", ""};
    option::OptionString m_wave_color_light{INI_SECTION, "wave_color_light", ""};
    option::OptionBool m_12hour_time{INI_SECTION, "12hour_time", false};
    option::OptionLong m_language{INI_SECTION, "language", 0}; // auto
    option::OptionString m_left_menu{INI_SECTION, "left_side_menu", "FileBrowser"};
    option::OptionString m_right_menu{INI_SECTION, "right_side_menu", "Appstore"};
    option::OptionBool m_progress_boost_mode{INI_SECTION, "progress_boost_mode", true};
    option::OptionBool m_god_mode{INI_SECTION, "god_mode", false};

    // what Minus does while the install queue runs, and how the screensaver
    // page it can raise is laid out.
    option::OptionLong m_blank_mode{INI_SECTION, "blank_mode", (long)ui::BlankMode::Screensaver};
    option::OptionLong m_blank_brightness{INI_SECTION, "blank_brightness", 10}; // percent
    option::OptionLong m_blank_timeout{INI_SECTION, "blank_timeout", 0}; // seconds (0 = off)
    option::OptionBool m_saver_oled{INI_SECTION, "saver_oled", true};
    option::OptionLong m_saver_fields{INI_SECTION, "saver_fields", ui::SaverField_ALL};

    // install options
    option::OptionBool m_install_sysmmc{INI_SECTION, "install_sysmmc", false};
    option::OptionBool m_install_emummc{INI_SECTION, "install_emummc", false};
    option::OptionLong m_install_location{INI_SECTION, "install_location", 4};
    // forwarder npdm address space: 0 = auto, 1 = 36-bit, 2 = 39-bit. see owo.cpp.
    option::OptionLong m_forwarder_address_space{INI_SECTION, "forwarder_address_space", 0};
    option::OptionBool m_forwarder_profile_select{INI_SECTION, "forwarder_profile_select", false};
    option::OptionBool m_forwarder_screenshot{INI_SECTION, "forwarder_screenshot", true};
    option::OptionBool m_forwarder_video_capture{INI_SECTION, "forwarder_video_capture", true};
    // svcDebug kac bit: 0 = auto (follow the ams version), 1 = on, 2 = off.
    option::OptionLong m_forwarder_svc_debug{INI_SECTION, "forwarder_svc_debug", 0};
    // when set, forwarder creation opens the editor instead of using the defaults above.
    option::OptionBool m_forwarder_ask{INI_SECTION, "forwarder_ask", false};
    // free space kept back on each target; NAND and SD are set separately.
    option::OptionLong m_install_reserve_mb{INI_SECTION, "install_reserve_mb", 500};
    option::OptionLong m_install_reserve_sd_mb{INI_SECTION, "install_reserve_sd_mb", 500};
    option::OptionBool m_allow_downgrade{INI_SECTION, "allow_downgrade", false};
    option::OptionLong m_skip_if_already_installed{INI_SECTION, "skip_if_already_installed", 1};
    option::OptionBool m_save_settings_globally{INI_SECTION, "save_settings_globally", true};
    // Saves menu defaults. Same [saves] keys the save menu used to own, so
    // existing configs keep their values.
    option::OptionBool m_save_show_installed{"saves", "show_installed", true};
    option::OptionBool m_save_show_deleted{"saves", "show_deleted", true};
    option::OptionBool m_save_show_backups{"saves", "show_backups", false};
    option::OptionBool m_save_auto_backup_on_restore{"saves", "auto_backup_on_restore", true};
    option::OptionBool m_save_compress_backup{"saves", "compress_save_backup", true};
    option::OptionBool m_save_autosync{"saves", "save_autosync", true};
    option::OptionBool m_save_restore_include_remote{"saves", "restore_include_remote", false};
    option::OptionString m_save_default_location{"saves", "default_backup_location", ""};
    option::OptionBool m_ticket_only{INI_SECTION, "ticket_only", false};
    option::OptionBool m_skip_base{INI_SECTION, "skip_base", false};
    option::OptionBool m_skip_patch{INI_SECTION, "skip_patch", false};
    option::OptionBool m_skip_addon{INI_SECTION, "skip_addon", false};
    option::OptionBool m_skip_data_patch{INI_SECTION, "skip_data_patch", false};
    option::OptionBool m_skip_ticket{INI_SECTION, "skip_ticket", false};
    option::OptionBool m_skip_nca_hash_verify{INI_SECTION, "skip_nca_hash_verify", true};
    option::OptionBool m_skip_rsa_header_fixed_key_verify{INI_SECTION, "skip_rsa_header_fixed_key_verify", true};
    option::OptionBool m_skip_rsa_npdm_fixed_key_verify{INI_SECTION, "skip_rsa_npdm_fixed_key_verify", true};
    option::OptionBool m_ignore_distribution_bit{INI_SECTION, "ignore_distribution_bit", false};
    option::OptionBool m_convert_to_common_ticket{INI_SECTION, "convert_to_common_ticket", true};
    option::OptionBool m_convert_to_standard_crypto{INI_SECTION, "convert_to_standard_crypto", false};
    option::OptionBool m_lower_master_key{INI_SECTION, "lower_master_key", false};
    option::OptionBool m_lower_system_version{INI_SECTION, "lower_system_version", true};

    // dump options
    option::OptionBool m_dump_app_folder{"dump", "app_folder", true};
    option::OptionBool m_dump_append_folder_with_xci{"dump", "append_folder_with_xci", true};
    option::OptionBool m_dump_trim_xci{"dump", "trim_xci", false};
    option::OptionBool m_dump_label_trim_xci{"dump", "label_trim_xci", false};
    option::OptionBool m_dump_usb_transfer_stream{"dump", "usb_transfer_stream", true, false};
    option::OptionBool m_dump_convert_to_common_ticket{"dump", "convert_to_common_ticket", true};

    // todo: move this into it's own menu
    option::OptionLong m_text_scroll_speed{"accessibility", "text_scroll_speed", 1}; // normal

#ifdef USE_NVJPG
    nj::Decoder m_decoder;
#endif

    double m_delta_time{};

    // frame accounting, see App::LogFrame().
    double m_frame_accum_ms{};
    double m_frame_worst_ms{};
    u32 m_frame_count{};
    // time Draw() spent blocked in acquireImage, ie waiting on the display
    // rather than doing work. Without it every frame reads as one vsync period
    // and there is no telling headroom from a cpu bound frame.
    double m_frame_wait_accum_ms{};

    static constexpr const char* INSTALL_DEPENDS_STR =
        "Installing is disabled.\n\n"
        "Enable in the options by selecting Menu (Y) -> Advanced -> Install options -> Enable.";

private: // from nanovg decko3d example by adubbz
    // triple buffered: with two, acquireImage blocks at the top of Draw() until
    // the display releases the other buffer, so the cpu can never run ahead and
    // a frame that overruns by a hair costs a whole vsync period. The third
    // image is ~8MB at 1080p, which applet mode can spare.
    static constexpr unsigned NumFramebuffers = 3;
    static constexpr unsigned StaticCmdSize = 0x1000;
    unsigned s_width{1280};
    unsigned s_height{720};
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;
    dk::UniqueCmdBuf cmdbuf;
    CMemPool::Handle depthBuffer_mem;
    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image depthBuffer;
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;
    DkCmdList render_cmdlist;
    std::optional<nvg::DkRenderer> renderer;
    void createFramebufferResources();
    void destroyFramebufferResources();
    void recordStaticCommands();
};

} // namespace sphaira
