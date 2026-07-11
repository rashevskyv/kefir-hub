#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui {

struct InstallProgress {
    virtual ~InstallProgress() = default;
    virtual Result CheckCancelled() = 0;
    virtual UEvent* GetInstallCancelEvent() = 0;
    virtual void SetInstallTitle(const std::string& title) = 0;
    virtual void SetInstallImage(std::vector<u8>& image) = 0;
    virtual void SetInstallTransfer(const std::string& transfer) = 0;
    virtual void UpdateInstallTransfer(s64 offset, s64 size) = 0;
    virtual void InstallYield() = 0;
};

} // namespace sphaira::ui
