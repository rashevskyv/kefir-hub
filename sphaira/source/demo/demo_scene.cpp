// DOCS_DEMO builds only: `[demo] scene=<name>` in config.ini opens one screen in a frozen state right after the
// main menu, for docs screenshots of screens that need real hardware, a real download or typed input.
// Buttons on these screens do nothing. tools/docs/shoot.ps1 sets the key from a recipe's "scene".

#include "demo/demo_scene.hpp"
#include "demo/demo_data.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/zip_extract_box.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"

#include <minIni.h>
#include <functional>
#include <string_view>

namespace sphaira::demo {
namespace {

// Software -> Custom Link -> a downloaded .zip (ghdl_api.cpp PromptExtractPath).
void ExtractOptions() {
    App::Push<ui::ZipExtractBox>("demo-mod-pack.zip",
        std::vector<std::string>{
            "atmosphere/", "atmosphere/contents/", "atmosphere/contents/0100DE0000030000/",
            "atmosphere/contents/0100DE0000030000/romfs/", "atmosphere/contents/0100DE0000030000/romfs/bowl.bntx",
            "atmosphere/contents/0100DE0000030000/romfs/spoon.bntx", "switch/", "switch/borshch-tools/",
            "switch/borshch-tools/borshch-tools.nro", "README.txt",
        },
        [](fs::FsPath, std::string, std::vector<std::string>) {},
        [](std::vector<std::string>, std::function<void()>) {});
}

// Updater -> firmware -> after the download validated (kefir_ops.cpp PromptInstallFirmware).
void FirmwareConfirm() {
    // the demo console is on emuMMC (header EmuNAND), whatever Eden reports.
    std::string message = std::string{"Install firmware 22.5.0 on "} + (EmuNand() ? "emuMMC" : ui::menu::kefir::detail::GetFirmwareTargetName()) + "?\n\n";
    message += "FAT32 + exFAT support\n";
    message += "Do not power off the console during installation.";
    App::Push<ui::OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1, [](auto) {});
}

// Console Transfer -> restore profiles & play hours, once staged (users_nand.cpp).
void TegraConfirm() {
    std::string msg =
        "Ready to restore profiles & play hours through TegraExplorer.\n\n"
        "The console will reboot into TegraExplorer, write the pack, then return to hekate.\n"
        "Open Kefir Hub again afterward to confirm the result.\n\n"_i18n;
    msg += "A raw undo snapshot was verified and saved on SD. If the console will not boot: hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te\n\n"_i18n;
    msg += "If TegraExplorer does not finish and the console will not boot: restore SYSTEM in hekate, or use Undo if a snapshot exists."_i18n;
    App::Push<ui::OptionBox>(msg, "Cancel"_i18n, "Launch TegraExplorer"_i18n, 1, [](auto) {});
}

struct Scene {
    std::string_view name;
    void (*open)();
};

constexpr Scene SCENES[] = {
    {"extract-options", ExtractOptions},
    {"firmware-confirm", FirmwareConfirm},
    {"te-confirm", TegraConfirm},
    {"install-review", SceneInstallReview},
    {"install-usb-queue", SceneInstallUsbQueue},
    {"install-progress", SceneInstallProgress},
    {"install-minimized", SceneInstallMinimized},
    {"install-screensaver", SceneInstallScreensaver},
    {"install-summary", SceneInstallSummary},
    {"install-mtp", [] { SceneInstallStream(true); }},
    {"install-ftp", [] { SceneInstallStream(false); }},
};

} // namespace

void StartScene() {
    char name[64]{};
    ini_gets("demo", "scene", "", name, sizeof(name), App::CONFIG_PATH);
    if (!name[0]) {
        return;
    }
    for (const auto& s : SCENES) {
        if (s.name == name) {
            log_write("[demo] scene %s\n", name);
            s.open();
            return;
        }
    }
    log_write("[demo] unknown scene %s\n", name);
}

} // namespace sphaira::demo
