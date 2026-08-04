#pragma once

#include "base.hpp"
#include "fs.hpp"
#include "usb/goldleaf.hpp"
#include "usb/usbds.hpp"

#include <array>
#include <string>
#include <memory>
#include <switch.h>

namespace sphaira::yati::source {

enum class UsbProtocol {
    Tinfoil,
    Goldleaf,
};

struct Usb final : Base {
    Usb(u64 transfer_timeout);
    ~Usb();

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

private:
    Result SendCmdHeader(u32 cmdId, size_t dataSize, u64 timeout);
    Result SendFileRangeCmd(u64 offset, u64 size, u64 timeout);
    Result TinfoilWaitForConnection(u64 timeout, std::vector<std::string>& out_names);

    Result GoldleafWaitForConnection(u64 timeout, std::vector<std::string>& out_names);
    Result GoldleafRead(void* buf, s64 off, s64 size, u64* bytes_read);

private:
    std::unique_ptr<usb::UsbDs> m_usb;
    std::string m_transfer_file_name{};
    u8 m_flags{};
    UsbProtocol m_protocol{UsbProtocol::Tinfoil};

    // goldleaf request blocks are built in m_gl_req and replies land in
    // m_gl_res. Both live here rather than on the stack: Read() runs on
    // whichever thread yati is installing from, and 0x2000 bytes of locals is
    // more than those get.
    std::array<u8, usb::goldleaf::BLOCK_SIZE> m_gl_req{};
    std::array<u8, usb::goldleaf::BLOCK_SIZE> m_gl_res{};
};

} // namespace sphaira::yati::source
