#include "chromium_nx.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "nro.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"

namespace sphaira::chromium_nx {
namespace {

constexpr const char* NRO_PATH = "/switch/chromium-nx/chromium-nx.nro";
constexpr const char* NRO_PART = "/switch/chromium-nx/chromium-nx_new.nro";
constexpr const char* NRO_URL = "https://github.com/shodowlo/chromium-nx/releases/latest/download/chromium-nx.nro";

void Launch() {
    if (R_FAILED(nro_launch(NRO_PATH))) {
        App::Push<ui::OptionBox>("Chromium NX could not start."_i18n, "OK"_i18n);
    }
}

void DownloadAndLaunch() {
    if (fs::FsNativeSd().FileExists(NRO_PATH)) {
        Launch();
        return;
    }
    App::Push<ui::ProgressBox>(0, "Chromium NX"_i18n, "", [](auto pbox) -> Result {
        using namespace ui::menu::settings::detail;
        R_TRY(DownloadFile(pbox, "Downloading Chromium NX..."_i18n, NRO_URL, NRO_PART));
        return MovePath(NRO_PART, NRO_PATH);
    }, [](Result rc) {
        if (R_SUCCEEDED(rc)) {
            Launch();
        } else if (rc != Result_TransferCancelled) {
            App::PushErrorBox(rc, "Chromium NX download failed!"_i18n);
        }
    });
}

} // namespace

void Open() {
    // Chromium takes about 1.9 GB: in Applet Mode it cannot start at all
    if (App::IsApplet()) {
        App::ShowTitleModeHelp("Chromium NX"_i18n);
        return;
    }
    App::Push<ui::OptionBox>(
        "Chromium NX is experimental: it may be slow or not start at all. It takes the full memory of the console, "
        "so start Kefir Hub from a game (hold R while launching it). On its first start it downloads its own files (about 430 MB)."_i18n,
        "Back"_i18n, "Start"_i18n, 1, [](auto op) {
        if (op && *op == 1) {
            DownloadAndLaunch();
        }
    });
}

} // namespace sphaira::chromium_nx
