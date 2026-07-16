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
    // raw source-read and storage-write offsets of the current transfer,
    // used for the R/W speed graph. Offsets reset per file.
    virtual void UpdateInstallReadWrite(s64 read_offset, s64 write_offset) {}
    virtual void InstallYield() = 0;
};

} // namespace sphaira::ui
