#include "parental_controls.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/ghdl_internal.hpp"
#include "ui/menus/settings/settings_translations.hpp"

#include <switch.h>
#include <algorithm>

namespace sphaira::parental {
namespace {

constexpr const char* SYSMODULE_PATH = "/atmosphere/contents/4200000000003103/exefs.nsp";
constexpr const char* OVERLAY_PATH = "/switch/.overlays/parental_control.ovl";
constexpr const char* RELEASE_API = "https://api.github.com/repos/TristanIsrael/NSParentalControl/releases/latest";
constexpr const char* ZIP_PART = "/config/kefir/nsparentalcontrol.zip";

// IParentalControlService commands that libnx has no wrapper for (switchbrew Parental_Control_services).
constexpr u32 CMD_DELETE_SETTINGS = 1043;
constexpr u32 CMD_DELETE_PAIRING = 1941;

auto Installed() -> bool {
    fs::FsNativeSd fs;
    return fs.FileExists(SYSMODULE_PATH) && fs.FileExists(OVERLAY_PATH);
}

void ShowHowTo(bool just_installed) {
    std::string text;
    if (just_installed) {
        text = "NS Parental Control is installed. Restart the console to start it."_i18n + "\n\n";
    }
    text += "Set the limits in the NS Parental Control overlay: open the Ultrahand overlay menu and pick Parental Control. "
        "Its administrator PIN is A A A A until you change it. It has play-time limits, bed time and blocked games for each user. "
        "It does not use the console's own parental-control PIN."_i18n;
    App::Push<ui::OptionBox>(text, "OK"_i18n);
}

auto Install(ui::ProgressBox* pbox) -> Result {
    std::vector<ui::menu::gh::GhApiEntry> releases;
    R_TRY(ui::menu::gh::DownloadReleaseJsonJson(pbox, RELEASE_API, releases));
    const auto& assets = releases[0].assets;
    const auto zip = std::find_if(assets.begin(), assets.end(), [](const auto& a) {
        return a.name.ends_with(".zip");
    });
    R_UNLESS(zip != assets.end(), Result_GhdlEmptyAsset);

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    ON_SCOPE_EXIT(fs.DeleteFile(ZIP_PART));
    R_TRY(ui::menu::settings::detail::DownloadFile(pbox, "Downloading NS Parental Control..."_i18n, zip->browser_download_url, ZIP_PART));

    pbox->NewTransfer("Extracting..."_i18n);
    return thread::TransferUnzipAll(pbox, ZIP_PART, &fs, "/", [](const fs::FsPath& name, fs::FsPath& path) {
        const auto mapped = MapZipEntry(name.s);
        if (!mapped) {
            return false;
        }
        path = mapped->c_str();
        return true;
    });
}

} // namespace

void Remove() {
    Result rc = pctlInitialize();
    bool still_on = false;
    if (R_SUCCEEDED(rc)) {
        Service* srv = pctlGetServiceSession_Service();
        // unlinks the phone app, so it does not set them again; fails when the console is not linked.
        serviceAssumeDomain(srv);
        const Result pairing_rc = serviceDispatch(srv, CMD_DELETE_PAIRING);
        serviceAssumeDomain(srv);
        rc = serviceDispatch(srv, CMD_DELETE_SETTINGS);
        log_write("[PCTL] delete pairing 0x%X, delete settings 0x%X\n", pairing_rc, rc);
        if (R_SUCCEEDED(rc) && R_SUCCEEDED(pctlIsRestrictionEnabled(&still_on)) && still_on) {
            log_write("[PCTL] restrictions still on after delete\n");
        }
        pctlExit();
    }

    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Parental controls could not be removed."_i18n);
    } else if (still_on) {
        App::Push<ui::OptionBox>("The console still has parental controls. Restart the console and try again."_i18n, "OK"_i18n);
    } else {
        App::Push<ui::OptionBox>("Parental controls are removed: no PIN, no restrictions and no link to the phone app."_i18n, "OK"_i18n);
    }
}

void OpenManager() {
    if (Installed()) {
        ShowHowTo(false);
        return;
    }
    App::Push<ui::OptionBox>(
        "Kefir Hub manages parental controls with NS Parental Control, a free sysmodule and overlay "
        "(github.com/TristanIsrael/NSParentalControl, about 1 MB). It sets play-time limits, bed time and blocked games "
        "for each user. Kefir Hub downloads it from GitHub and installs it."_i18n,
        "Back"_i18n, "Download"_i18n, 1, [](auto op) {
        if (!op || *op != 1) {
            return;
        }
        App::Push<ui::ProgressBox>(0, "NS Parental Control"_i18n, "", [](auto pbox) -> Result {
            return Install(pbox);
        }, [](Result rc) {
            if (R_SUCCEEDED(rc)) {
                ShowHowTo(true);
            } else if (rc != Result_TransferCancelled) {
                App::PushErrorBox(rc, "NS Parental Control download failed!"_i18n);
            }
        });
    });
}

} // namespace sphaira::parental
