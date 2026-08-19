#pragma once

#include "i18n.hpp"
#include <cstdio>
#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui {

struct CompatibilityWarning {
    std::string title_name;
    u64 title_id{};
    std::string required_hos;
    std::string installed_hos;
    std::string title_sdk;
};

inline auto FormatCompatibilityWarning(const CompatibilityWarning& warning) -> std::string {
    char buf[1024]{};
    const auto title_line = warning.title_name.empty() ? "" : (warning.title_name + "\n");
    if (!warning.title_sdk.empty()) {
        std::snprintf(buf, sizeof(buf),
            "The title was installed successfully, but cannot run on this console yet.\n\n%sRequired system firmware: %s\nInstalled system firmware: %s\nTitle SDK version: %s\n\nPlease update Kefir/CFW and the console system firmware following the Kefir update instructions, then launch the game again. Reinstalling the game is not necessary after the update."_i18n.c_str(),
            title_line.c_str(),
            warning.required_hos.c_str(),
            warning.installed_hos.c_str(),
            warning.title_sdk.c_str());
    } else {
        std::snprintf(buf, sizeof(buf),
            "The title was installed successfully, but cannot run on this console yet.\n\n%sRequired system firmware: %s\nInstalled system firmware: %s\n\nPlease update Kefir/CFW and the console system firmware following the Kefir update instructions, then launch the game again. Reinstalling the game is not necessary after the update."_i18n.c_str(),
            title_line.c_str(),
            warning.required_hos.c_str(),
            warning.installed_hos.c_str());
    }
    return std::string{buf};
}

inline auto FormatCompatibilityLog(const CompatibilityWarning& warning) -> std::string {
    char buf[512]{};
    const auto log_title = warning.title_name.empty() ? "" : (warning.title_name + " ");
    if (!warning.title_sdk.empty()) {
        std::snprintf(buf, sizeof(buf),
            "Compatibility warning: %srequires system firmware %s (installed: %s, SDK: %s). Update firmware before launching; reinstalling is not needed."_i18n.c_str(),
            log_title.c_str(),
            warning.required_hos.c_str(),
            warning.installed_hos.c_str(),
            warning.title_sdk.c_str());
    } else {
        std::snprintf(buf, sizeof(buf),
            "Compatibility warning: %srequires system firmware %s (installed: %s). Update firmware before launching; reinstalling is not needed."_i18n.c_str(),
            log_title.c_str(),
            warning.required_hos.c_str(),
            warning.installed_hos.c_str());
    }
    return std::string{buf};
}

struct InstallProgress {
    virtual ~InstallProgress() = default;
    virtual Result CheckCancelled() = 0;
    virtual UEvent* GetInstallCancelEvent() = 0;
    virtual void SetInstallTitle(const std::string& title) = 0;
    virtual void SetInstallImage(std::vector<u8>& image) = 0;
    virtual void SetInstallTransfer(const std::string& transfer) = 0;
    virtual void UpdateInstallTransfer(s64 offset, s64 size) = 0;
    // raw source-read and storage-write offsets of the current transfer,
    // used for the R/W speed graph. Offsets reset per file.
    virtual void UpdateInstallReadWrite(s64 read_offset, s64 write_offset) {}
    virtual void InstallYield() = 0;
    virtual bool PromptReinstall(const std::string& title_name) { return false; }
    // called when the current title/content is skipped because it is already
    // installed (skip_if_already_installed). Lets the UI report it distinctly.
    virtual void OnInstallSkipped() {}
    // called when a successfully registered title requires a newer system firmware
    virtual void OnCompatibilityWarning(const CompatibilityWarning& warning) {}
    // called when a title is successfully registered with its application record
    virtual void OnTitleInstalled(u64 title_id) {}
};

} // namespace sphaira::ui
