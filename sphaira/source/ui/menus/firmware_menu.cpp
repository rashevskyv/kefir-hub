#include "ui/menus/firmware_menu.hpp"

#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include "ams_su.h"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "hats_version.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace sphaira::ui::menu::firmware {
namespace {

constexpr const char* NXLINKS_URL = "https://raw.githubusercontent.com/rashevskyv/nx-links/master/nx-links.json";
constexpr const char* CACHE_DIR = "/config/kefir-updater";
constexpr const char* NXLINKS_CACHE = "/config/kefir-updater/nx-links.json";
constexpr const char* FIRMWARE_ZIP = "/config/kefir-updater/firmware.zip";
constexpr const char* FIRMWARE_DEST = "/firmware";
constexpr size_t UPDATE_TASK_BUFFER_SIZE = 0x100000;

struct FirmwareValidation {
    AmsSuUpdateInformation info{};
    AmsSuUpdateValidationInfo validation{};
};

auto ParseFirmwareLinks(const fs::FsPath& path, std::vector<FirmwareEntry>& out) -> bool {
    out.clear();

    auto doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto root = yyjson_doc_get_root(doc);
    auto firmwares = root ? yyjson_obj_get(root, "firmwares") : nullptr;
    if (!firmwares || !yyjson_is_obj(firmwares)) {
        return false;
    }

    yyjson_obj_iter iter;
    yyjson_obj_iter_init(firmwares, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        auto value = yyjson_obj_iter_get_val(key);
        const auto name = yyjson_get_str(key);
        const auto url = yyjson_get_str(value);
        if (!name || !url || !*url) {
            continue;
        }

        out.push_back({ .name = name, .url = url });
    }

    return true;
}

auto FormatFirmwareVersion(u32 version) -> std::string {
    return std::to_string((version >> 26) & 0x1f) + "." +
           std::to_string((version >> 20) & 0x1f) + "." +
           std::to_string((version >> 16) & 0xf);
}

auto BuildFirmwareServicePath(const fs::FsPath& path) -> std::string {
    std::string service_path = path.s;
    if (service_path.empty()) {
        service_path = FIRMWARE_DEST;
    }
    if (service_path.back() != '/') {
        service_path += '/';
    }
    return service_path;
}

auto ValidateFirmware(FirmwareValidation* out, const fs::FsPath& path) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    R_TRY(amssuGetUpdateInformation(&out->info, service_path.c_str()));
    R_TRY(amssuValidateUpdate(&out->validation, service_path.c_str()));
    return out->validation.result;
}

auto InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    pbox->NewTransfer("Setting up system update...");
    R_TRY(amssuSetupUpdate(nullptr, UPDATE_TASK_BUFFER_SIZE, service_path.c_str(), use_exfat));

    AsyncResult prepare{};
    R_TRY(amssuRequestPrepareUpdate(&prepare));
    ON_SCOPE_EXIT(asyncResultClose(&prepare));
    pbox->NewTransfer("Preparing system update...");

    while (true) {
        rc = asyncResultWait(&prepare, 0);
        if (R_FAILED(rc) && rc != 0xea01) {
            return rc;
        }
        if (R_SUCCEEDED(rc)) {
            R_TRY(asyncResultGet(&prepare));
        }

        bool prepared = false;
        R_TRY(amssuHasPreparedUpdate(&prepared));
        if (prepared) {
            break;
        }

        NsSystemUpdateProgress progress{};
        R_TRY(amssuGetPrepareUpdateProgress(&progress));
        pbox->UpdateTransfer(progress.current_size, progress.total_size);
        svcSleepThread(50'000'000);
    }

    pbox->NewTransfer("Applying system update...");
    return amssuApplyPreparedUpdate();
}

auto GetFirmwareTargetName() -> std::string {
    bool emummc = false;
    Result rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 value{};
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65007, &value))) {
            emummc = value != 0;
        }
        splExit();
    }
    return emummc ? "emuMMC" : "sysMMC";
}

auto ParseVersion(const std::string& version) -> std::vector<int> {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) {
            continue;
        }

        char* end = nullptr;
        const auto part = std::strtol(segment.c_str(), &end, 10);
        if (end == segment.c_str()) {
            break;
        }
        parts.push_back(static_cast<int>(part));
    }

    return parts;
}

auto IsVersionLower(const std::string& target, const std::string& current) -> bool {
    const auto target_parts = ParseVersion(target);
    const auto current_parts = ParseVersion(current);
    const auto max_len = std::max(target_parts.size(), current_parts.size());

    for (size_t i = 0; i < max_len; i++) {
        const auto t = i < target_parts.size() ? target_parts[i] : 0;
        const auto c = i < current_parts.size() ? current_parts[i] : 0;
        if (t < c) {
            return true;
        }
        if (t > c) {
            return false;
        }
    }

    return false;
}

auto DownloadAndExtract(ProgressBox* pbox, const FirmwareEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }

    pbox->NewTransfer("Downloading " + entry.name);
    const auto result = curl::Api().ToFile(
        curl::Url{entry.url},
        curl::Path{FIRMWARE_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, 0x1);

    if (fs.DirExists(FIRMWARE_DEST)) {
        R_TRY(fs.DeleteDirectoryRecursively(FIRMWARE_DEST));
    }
    R_TRY(fs.CreateDirectoryRecursively(FIRMWARE_DEST));

    pbox->NewTransfer("Extracting to /firmware...");
    R_TRY(thread::TransferUnzipAll(pbox, FIRMWARE_ZIP, &fs, FIRMWARE_DEST));
    R_TRY(fs.Commit());

    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

} // namespace

Menu::Menu() : MenuBase{"Firmware", MenuFlag_None} {
    m_current_firmware = hats::getSystemFirmware();

    this->SetActions(
        std::make_pair(Button::A, Action{"Download"_i18n, [this](){
            if (!m_entries.empty() && !m_loading) {
                DownloadSelected();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            FetchLinks();
        }})
    );

    const Vec4 v{75.f, GetY() + 1.f + 72.f, 1220.f - 150.f, 74.f};
    m_list = std::make_unique<List>(1, 6, m_pos, v, Vec2{0.f, 6.f});
    m_list->SetLayout(List::Layout::GRID);
}

Menu::~Menu() = default;

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_entries.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 18.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "Current Firmware: %s", m_current_firmware.c_str());

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading firmware links...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
        return;
    }

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No firmware downloads found");
        return;
    }

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& entry = m_entries[i];
        const auto selected = m_index == i;
        const auto downgrade = IsDowngrade(entry.name);
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto name_id = downgrade ? ThemeEntryID_ERROR : text_id;

        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_entries.size() - 1) {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        std::string name = entry.name;
        if (downgrade) {
            name += " [DOWNGRADE]";
        }

        gfx::drawTextArgs(vg, v.x + 15.f, v.y + v.h / 2.f - 8.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(name_id),
            "%s", name.c_str());

        gfx::drawTextBox(vg, v.x + 15.f, v.y + v.h / 2.f + 8.f, 14.f, v.w - 30.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), entry.url.c_str());
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded && !m_loading) {
        FetchLinks();
    }
}

void Menu::FetchLinks() {
    m_loading = true;
    m_error_message.clear();
    m_entries.clear();

    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_DIR);

    curl::Api().ToFileAsync(
        curl::Url{NXLINKS_URL},
        curl::Path{NXLINKS_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            if (!result.success || !ParseFirmwareLinks(result.path, m_entries)) {
                m_error_message = "Failed to load firmware list.";
                return false;
            }

            if (m_entries.empty()) {
                m_error_message = "No firmware downloads found.";
            } else {
                SetIndex(0);
            }

            return true;
        }}
    );
}

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
    UpdateSubheading();
}

void Menu::DownloadSelected() {
    if (m_entries.empty() || m_index >= (s64)m_entries.size()) {
        return;
    }

    const auto entry = m_entries[m_index];
    const auto downgrade = IsDowngrade(entry.name);

    std::string message = "Download firmware " + entry.name + "?\n\n";
    message += "It will be staged at ";
    message += FIRMWARE_ZIP;
    message += " and extracted to /firmware.";

    if (downgrade) {
        message = "WARNING: This looks like a firmware downgrade.\n\n";
        message += "Current: " + m_current_firmware + "\n";
        message += "Target: " + entry.name + "\n\n";
        message += "Download and continue anyway?";
    }

    App::Push<OptionBox>(message, "Cancel"_i18n, downgrade ? "Download" : "Download", 1,
        [this, entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Downloading"_i18n, entry.name,
                [entry](auto pbox) -> Result {
                    return DownloadAndExtract(pbox, entry);
                },
                [this, entry](Result rc) {
                    if (R_FAILED(rc)) {
                        App::Push<ErrorBox>(rc, "Failed to download " + entry.name);
                        return;
                    }

                    PromptInstallFirmware(entry.name);
                });
        });
}

void Menu::PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, display_name,
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Validating firmware contents...");
            return ValidateFirmware(validation.get(), path);
        },
        [this, display_name, path, validation](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware validation failed");
                return;
            }

            const auto version = FormatFirmwareVersion(validation->info.version);
            const bool use_exfat = validation->info.exfat_supported &&
                                   R_SUCCEEDED(validation->validation.exfat_result);
            std::string message = "Install firmware " + version + " on " + GetFirmwareTargetName() + "?\n\n";
            message += use_exfat ? "FAT32 + exFAT support\n" : "FAT32 support only\n";
            message += "Do not power off the console during installation.";

            App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
                [this, display_name, path](auto op_index) {
                    if (op_index && *op_index == 1) {
                        InstallFirmware(display_name, path);
                    }
                });
        });
}

void Menu::InstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    App::Push<ProgressBox>(0, "Updating Firmware"_i18n, display_name,
        [path](auto pbox) -> Result {
            FirmwareValidation validation{};
            R_TRY(ValidateFirmware(&validation, path));
            const bool use_exfat = validation.info.exfat_supported &&
                                   R_SUCCEEDED(validation.validation.exfat_result);
            return InstallValidatedFirmware(pbox, use_exfat, path);
        },
        [](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware update failed");
                return;
            }

            App::Push<OptionBox>(
                "Firmware update applied successfully.\n\nReboot now?",
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        utils::requestForcedReboot();
                    }
                });
        }, false);
}

void Menu::UpdateSubheading() {
    const auto index = m_entries.empty() ? 0 : m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_entries.size()));
}

bool Menu::IsDowngrade(const std::string& target_version) const {
    return IsVersionLower(target_version, m_current_firmware);
}

} // namespace sphaira::ui::menu::firmware
