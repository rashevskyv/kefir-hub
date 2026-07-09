#pragma once

#include "base.hpp"
#include "fs.hpp"
#include "usb/usbds.hpp"
#include "usb/dbi.hpp"

#include <string>
#include <memory>
#include <switch.h>

namespace sphaira::yati::source {

struct DbiUsb final : Base {
    DbiUsb(u64 transfer_timeout);
    ~DbiUsb();

    bool IsStream() const override;
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    Result Finished(u64 timeout);

    Result IsUsbConnected(u64 timeout) {
        return m_usb->IsUsbConnected(timeout);
    }

    Result WaitForConnection(u64 timeout, std::vector<std::string>& out_names);
    void SetFileNameForTranfser(const std::string& name);

    void SignalCancel() override {
        m_usb->Cancel();
    }

    Result GetOpenResult() const {
        return m_open_result;
    }

private:
    Result SendCmdHeader(usb::dbi::CmdType type, usb::dbi::CmdId id, u32 dataSize, u64 timeout);

private:
    std::unique_ptr<usb::UsbDs> m_usb;
    std::string m_transfer_file_name{};
};

} // namespace sphaira::yati::source
