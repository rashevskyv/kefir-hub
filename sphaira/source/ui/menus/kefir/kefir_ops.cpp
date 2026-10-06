#include "ui/menus/kefir/kefir_internal.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/menus/kefir/kefir_firmware_cleanup.hpp"
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "net.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"
#include "text_helper.hpp"
#include "path_util.hpp"

#include <memory>
#include <string>
#include <utility>

namespace sphaira::ui::menu::kefir {
using namespace detail;

void Menu::OpenSelected() {
    if (m_entries.empty() || m_index >= static_cast<s64>(m_entries.size())) {
        return;
    }

    const auto entry = m_entries[m_index];
    switch (entry.type) {
        case UpdaterEntryType::Network:
            App::Push<ui::menu::gh::Menu>(MenuFlag_None);
            break;
        case UpdaterEntryType::CustomLink:
            ui::menu::gh::DownloadDirectLink();
            break;
        case UpdaterEntryType::Kefir:
            InstallKefir(entry);
            break;
        case UpdaterEntryType::Firmware:
            DownloadFirmware(entry);
            break;
        case UpdaterEntryType::FirmwareManual:
            OpenManualFirmwarePicker();
            break;
        case UpdaterEntryType::Section:
            break;
    }
}

void Menu::OpenManualFirmwarePicker() {
    auto browser = std::make_unique<::sphaira::ui::menu::filebrowser::Menu>(MenuFlag_None);
    browser->SetFolderPicker([this](const fs::FsPath& selected_path) {
        // record the choice; consumed in OnFocusGained once the browser closes,
        // so nothing is pushed over the soon-to-be-popped file browser.
        m_pending_manual_firmware = selected_path;
        m_pending_manual_firmware_is_zip = ::sphaira::path::EqualsIC(text_helper::GetExtension(selected_path.s), "zip");
    });
    App::Push(std::move(browser));
}

void Menu::StartManualZipFirmware(const fs::FsPath& zip_path) {
    std::string name = zip_path.s;
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.empty()) {
        name = "Firmware";
    }

    App::Push<ProgressBox>(0, "Extracting"_i18n, name,
        [zip_path](auto pbox) -> Result {
            return detail::ExtractManualFirmwareZip(pbox, zip_path);
        },
        [this, name, zip_path](Result rc) {
            if (R_FAILED(rc)) {
                detail::CleanupManualFirmwareStaging();
                if (rc == Result_TransferCancelled) {
                    return;
                }
                App::PushErrorBox(rc, "Failed to extract " + name);
                return;
            }

            PromptInstallFirmware(name, detail::MANUAL_FIRMWARE_DEST, std::nullopt, zip_path);
        });
}

void Menu::InstallKefir(const UpdaterEntry& entry, std::function<void()> on_success) {
    App::Push<KefirChangelogBox>(entry,
        [this, entry, on_success = std::move(on_success)]() mutable {
            net::RequireConnection([this, entry, on_success = std::move(on_success)]() mutable {
                App::Push<ProgressBox>(0, "Installing"_i18n, entry.name,
                    [entry](auto pbox) -> Result {
                        return detail::DownloadAndInstallKefir(pbox, entry);
                    },
                    [this, entry, on_success = std::move(on_success)](Result rc) mutable {
                        if (R_FAILED(rc)) {
                            if (rc == Result_TransferCancelled) {
                                return;
                            }
                            if (rc == Result_AppstoreFailedZipDownload) {
                                App::PushErrorBox(rc, "Failed to download " + entry.name);
                            } else {
                                App::PushErrorBox(rc, "Failed to install " + entry.name);
                            }
                            return;
                        }

                        RefreshSystemInfo();
                        if (on_success) {
                            on_success();
                            return;
                        }

                        App::Push<OptionBox>(
                            "Kefir package installed."_i18n + "\n\n" + "Reboot now?"_i18n,
                            "Later"_i18n, "Reboot"_i18n, 1,
                            [](auto op_index) {
                                if (op_index && *op_index == 1) {
                                    utils::requestForcedReboot();
                                }
                            });
                    });
            });
        });
}

void Menu::DownloadFirmware(const UpdaterEntry& entry, bool skip_support_check) {
    if (!skip_support_check && !IsFirmwareSupported(entry.name)) {
        PromptKefirThenFirmware(entry);
        return;
    }

    // a downgrade is fully acknowledged BEFORE anything is downloaded: the
    // warning and the downgrade-fix choice are both answered up front, so
    // nothing is fetched until the user has accepted it.
    const auto downgrade = PromptDowngradeAck(entry.name, "Download"_i18n,
        [this, entry](bool apply_fix) {
            StartFirmwareDownload(entry, apply_fix);
        });

    if (downgrade) {
        return;
    }

    std::string message = "Download firmware " + entry.name + "?\n\n";
    message += "It will be staged at ";
    message += FIRMWARE_ZIP;
    message += " and extracted to /firmware.";

    App::Push<OptionBox>(message, "Cancel"_i18n, "Download"_i18n, 1,
        [this, entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            StartFirmwareDownload(entry, std::nullopt);
        });
}

void Menu::StartFirmwareDownload(const UpdaterEntry& entry, std::optional<bool> acked_downgrade_fix) {
    net::RequireConnection([this, entry, acked_downgrade_fix]() {
        App::Push<ProgressBox>(0, "Downloading"_i18n, entry.name,
            [entry](auto pbox) -> Result {
                return detail::DownloadAndExtractFirmware(pbox, entry);
            },
            [this, entry, acked_downgrade_fix](Result rc) {
                if (R_FAILED(rc)) {
                    if (rc == Result_TransferCancelled) {
                        return;
                    }
                    App::PushErrorBox(rc, "Failed to download " + entry.name);
                    return;
                }

                PromptInstallFirmware(entry.name, "/firmware", acked_downgrade_fix);
            });
    });
}

bool Menu::PromptDowngradeAck(const std::string& target_version, const std::string& confirm_label, std::function<void(bool)> on_ack, std::function<void()> on_cancel) {
    if (!IsDowngrade(target_version)) {
        return false;
    }

    App::Push<DowngradeWarningBox>(
        m_current_firmware,
        target_version,
        confirm_label,
        [this, on_ack = std::move(on_ack), on_cancel = std::move(on_cancel)](auto op_index) {
            if (!op_index || *op_index != 1) {
                if (on_cancel) {
                    on_cancel();
                }
                return;
            }

            // nothing to ask while the fix has no working implementation.
            if (!detail::IsDowngradeFixAvailable()) {
                on_ack(false);
                return;
            }

            // apply the configured downgrade-fix policy.
            switch (m_downgrade_fix_mode.Get()) {
                case DowngradeFixMode_Off:
                    on_ack(false);
                    break;
                case DowngradeFixMode_Automatic:
                    on_ack(true);
                    break;
                case DowngradeFixMode_Optional:
                default: {
                    const std::string msg = "Apply downgrade fix?\n\nThis stages TegraExplorer downgrade fix to delete system save 8000000000000073 after install.\n\nChoose No to install without it."_i18n;
                    App::Push<OptionBox>(msg, "No"_i18n, "Yes"_i18n, 1,
                        [on_ack, on_cancel](auto fix_index) {
                            if (!fix_index) {
                                if (on_cancel) {
                                    on_cancel();
                                }
                                return;
                            }
                            on_ack(*fix_index == 1);
                        });
                    break;
                }
            }
        });

    return true;
}

void Menu::PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path, std::optional<bool> acked_downgrade_fix, std::optional<fs::FsPath> origin_zip, bool is_manual_folder) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, display_name,
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Validating firmware contents..."_i18n);
            return detail::ValidateFirmware(validation.get(), path);
        },
        [this, display_name, path, validation, acked_downgrade_fix, origin_zip, is_manual_folder](Result rc) {
            if (R_FAILED(rc)) {
                if (origin_zip) {
                    detail::CleanupManualFirmwareStaging();
                }
                App::PushErrorBox(rc, "Firmware validation failed"_i18n);
                return;
            }

            const auto version = detail::FormatFirmwareVersion(validation->info.version);
            const bool use_exfat = validation->info.exfat_supported &&
                                   R_SUCCEEDED(validation->validation.exfat_result);

            auto prompt_install_confirm = [this, display_name, path, version, use_exfat, origin_zip, is_manual_folder](bool apply_fix) {
                std::string message = "Install firmware " + version + " on " + detail::GetFirmwareTargetName() + "?\n\n";
                message += use_exfat ? "FAT32 + exFAT support\n" : "FAT32 support only\n";
                message += "Do not power off the console during installation.";

                App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
                    [this, display_name, path, apply_fix, origin_zip, is_manual_folder](auto op_index) {
                        if (!op_index || *op_index != 1) {
                            if (origin_zip) {
                                detail::CleanupManualFirmwareStaging();
                            }
                            return;
                        }

                        InstallFirmware(display_name, path, apply_fix, origin_zip, is_manual_folder);
                    });
            };

            // the downgrade warning/fix is only relevant when installing a LOWER firmware.
            if (!IsDowngrade(version)) {
                prompt_install_confirm(false);
                return;
            }

            // downloaded firmware already warned before fetching it, so
            // the warning is not repeated here.
            if (acked_downgrade_fix.has_value()) {
                prompt_install_confirm(*acked_downgrade_fix);
                return;
            }

            // manual install or initially unknown download: warn immediately after validation.
            if (!PromptDowngradeAck(version, "Continue"_i18n,
                    [prompt_install_confirm](bool apply_fix) {
                        prompt_install_confirm(apply_fix);
                    },
                    [origin_zip]() {
                        if (origin_zip) {
                            detail::CleanupManualFirmwareStaging();
                        }
                    })) {
                prompt_install_confirm(false);
            }
        });
}

void Menu::InstallFirmware(const std::string& display_name, const fs::FsPath& path, bool apply_downgrade_fix, std::optional<fs::FsPath> origin_zip, bool is_manual_folder) {
    auto fix = std::make_shared<DowngradeFixResult>();

    App::Push<ProgressBox>(0, "Updating Firmware"_i18n, display_name,
        [path, apply_downgrade_fix, fix, is_manual_folder](auto pbox) -> Result {
            FirmwareValidation validation{};
            R_TRY(detail::ValidateFirmware(&validation, path));
            const bool use_exfat = validation.info.exfat_supported &&
                                   R_SUCCEEDED(validation.validation.exfat_result);
            return detail::InstallValidatedFirmware(pbox, use_exfat, path, apply_downgrade_fix, fix.get(), is_manual_folder);
        },
        [path, apply_downgrade_fix, fix, origin_zip, is_manual_folder](Result rc) {
            if (R_FAILED(rc)) {
                if (origin_zip) {
                    detail::CleanupManualFirmwareStaging();
                }
                App::PushErrorBox(rc, "Firmware update failed"_i18n);
                return;
            }

            auto prompt_reboot = [apply_downgrade_fix, fix]() {
                if (apply_downgrade_fix && fix->staged) {
                    std::string message = "Firmware downgrade installed successfully."_i18n + "\n\n";
                    if (fix->cleanup_failed) {
                        message += "WARNING: Failed to remove custom themes and translations! Incompatible themes or translations can cause Atmosphere error 2162-0002 on reboot. Remove them manually before booting the new firmware."_i18n + "\n\n";
                    } else {
                        message += "System save 8000000000000073, custom themes, and interface translations will be removed as part of the downgrade process."_i18n + "\n\n";
                    }
                    message += "Console will reboot to TegraExplorer to complete the downgrade fix."_i18n + "\n\n" + "Reboot now?"_i18n;
                    App::Push<OptionBox>(
                        message,
                        "Later"_i18n, "Reboot"_i18n, 1,
                        [](auto op_index) {
                            if (op_index && *op_index == 1) {
                                fs::FsPath te_bin;
                                if (utils::findTegraExplorerPayload(te_bin)) {
                                    utils::rebootToPayload(static_cast<const char*>(te_bin));
                                } else {
                                    utils::requestForcedReboot();
                                }
                            }
                        });
                    return;
                }

                std::string message = "Firmware update applied successfully."_i18n + "\n\n";
                if (fix->cleanup_failed) {
                    message += "WARNING: Failed to remove custom themes and translations! Incompatible themes or translations can cause Atmosphere error 2162-0002 on reboot. Remove them manually before booting the new firmware."_i18n;
                } else {
                    message += "Custom themes and translations were removed to prevent errors on the new firmware version."_i18n;
                }
                const auto fix_note = detail::DescribeDowngradeFix(*fix);
                if (!fix_note.empty()) {
                    message += "\n\n" + fix_note;
                }
                message += "\n\n" + "Reboot now?"_i18n;

                App::Push<OptionBox>(
                    message,
                    "Later"_i18n, "Reboot"_i18n, 1,
                    [](auto op_index) {
                        if (op_index && *op_index == 1) {
                            utils::requestForcedReboot();
                        }
                    });
            };

            if (ShouldPromptManualFolderCleanup(is_manual_folder, R_SUCCEEDED(rc), rc == Result_TransferCancelled)) {
                const auto folder = path;
                App::Push<OptionBox>(
                    "Delete firmware folder?"_i18n,
                    "Keep"_i18n, "Delete"_i18n, 0,
                    [folder, prompt_reboot = std::move(prompt_reboot)](auto op_index) {
                        if (ShouldExecuteFolderDeletion(op_index)) {
                            const auto valid_folder = ValidateManualFirmwareFolder(folder.s);
                            Result del_rc = 0;
                            if (!valid_folder) {
                                del_rc = FsError_PathNotFound;
                            } else {
                                fs::FsNativeSd fs;
                                del_rc = fs.GetFsOpenResult();
                                if (R_SUCCEEDED(del_rc)) {
                                    const fs::FsPath target{*valid_folder};
                                    if (fs.DirExists(target)) {
                                        del_rc = fs.DeleteDirectoryRecursively(target);
                                    } else {
                                        del_rc = FsError_PathNotFound;
                                    }
                                    if (R_SUCCEEDED(del_rc)) {
                                        del_rc = fs.Commit();
                                    }
                                }
                            }

                            prompt_reboot();
                            if (R_FAILED(del_rc)) {
                                App::PushErrorBox(del_rc, "Failed to delete firmware folder"_i18n);
                            }
                            return;
                        }

                        prompt_reboot();
                    });
            } else if (origin_zip) {
                const auto zip = *origin_zip;
                std::string zip_name = zip.s;
                if (const auto slash = zip_name.find_last_of('/'); slash != std::string::npos) {
                    zip_name = zip_name.substr(slash + 1);
                }
                std::string zip_msg = "Delete original firmware archive?\n\n"_i18n + zip_name;
                App::Push<OptionBox>(
                    zip_msg,
                    "Keep"_i18n, "Delete"_i18n, 0,
                    [zip, prompt_reboot = std::move(prompt_reboot)](auto op_index) {
                        if (op_index && *op_index == 1) {
                            fs::FsNativeSd fs;
                            if (R_SUCCEEDED(fs.GetFsOpenResult())) {
                                if (fs.FileExists(zip)) {
                                    fs.DeleteFile(zip);
                                    fs.Commit();
                                }
                            }
                        }
                        prompt_reboot();
                    });
            } else {
                prompt_reboot();
            }
        }, false);
}

void Menu::PromptKefirThenFirmware(const UpdaterEntry& firmware_entry) {
    UpdaterEntry kefir_entry;
    if (!FindKefirUpdate(kefir_entry)) {
        App::Push<ErrorBox>(0x1, "Firmware " + firmware_entry.name + " is not supported by the current Kefir, and no Kefir update entry was found.");
        return;
    }

    App::Push<OptionBox>(
        detail::FirmwareUnsupportedReason(firmware_entry.name, m_supported_firmware),
        "Cancel"_i18n, "Update Kefir"_i18n, 1,
        [this, kefir_entry, firmware_entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            InstallKefir(kefir_entry, [this, firmware_entry]() {
                DownloadFirmware(firmware_entry, true);
            });
        });
}

} // namespace sphaira::ui::menu::kefir
