#pragma once

#include "base.hpp"
#include "fs.hpp"
#include "usb/dbi.hpp"
#include "usb/goldleaf.hpp"
#include "usb/usbds.hpp"

#include <array>
#include <string>
#include <memory>
#include <unordered_map>
#include <switch.h>

namespace sphaira::yati::source {

// which pc side app is on the other end of the cable. Worked out on connect,
// see Usb::WaitForConnection() for how the three are told apart.
enum class UsbProtocol {
    None,
    // DBI0, as spoken by dbibackend.py and dbibackend-qt.
    Dbi,
    // TUL0/TUC0, as spoken by ns-usbloader in "Tinfoil/Awoo" mode, fluffy,
    // and Awoo Installer's own host tools.
    Tinfoil,
    // GLCI/GLCO, as spoken by ns-usbloader in "GoldLeaf v0.10+" mode.
    Goldleaf,
};

// what to call the protocol in the ui and the log.
auto GetUsbProtocolName(UsbProtocol protocol) -> const char*;

struct Usb final : Base {
    Usb(u64 transfer_timeout);
    ~Usb();

    bool IsStream() const override;
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    Result Finished(u64 timeout);

    Result IsUsbConnected(u64 timeout) {
        return m_usb->IsUsbConnected(timeout);
    }

    // runs one detection round and, if a host answered, retrieves its file
    // list. Fails without side effects when nobody is speaking yet, so callers
    // are expected to loop.
    Result WaitForConnection(u64 timeout, std::vector<std::string>& out_names);
    void SetFileNameForTranfser(const std::string& name);

    // size the host reported for a listed file, or 0 when it did not say.
    // Only dbi backends that understand the 'SPHA' list request report sizes.
    s64 GetFileSize(const std::string& name) const;

    auto GetProtocol() const {
        return m_protocol;
    }

    void SignalCancel() override {
        m_usb->Cancel();
    }

private:
    Result DbiWaitForConnection(const usb::dbi::CmdHeader& header, u64 timeout, std::vector<std::string>& out_names);
    Result DbiRead(void* buf, s64 off, s64 size, u64* bytes_read);
    Result SendDbiCmdHeader(usb::dbi::CmdType type, usb::dbi::CmdId id, u32 data_size, u64 timeout);

    Result TinfoilWaitForConnection(u64 timeout, std::vector<std::string>& out_names);
    Result TinfoilRead(void* buf, s64 off, s64 size, u64* bytes_read);
    Result SendTinfoilCmdHeader(u32 cmd_id, size_t data_size, u64 timeout);
    Result SendFileRangeCmd(u64 offset, u64 size, u64 timeout);

    Result GoldleafWaitForConnection(u64 timeout, std::vector<std::string>& out_names);
    Result GoldleafRead(void* buf, s64 off, s64 size, u64* bytes_read);
    // builds a request block in m_gl_req; the caller appends the payload.
    usb::goldleaf::BlockWriter GlBegin(usb::goldleaf::CmdId cmd);
    // sends m_gl_req and reads back the one transfer that answers it.
    Result GlSendRecv(const usb::goldleaf::BlockWriter& writer, u64 timeout, u32* out_transferred);
    // the same round trip with the reply validated, out_reader positioned at
    // the payload past the magic and the host result code.
    Result GlTransact(const usb::goldleaf::BlockWriter& writer, u64 timeout, usb::goldleaf::BlockReader* out_reader);

private:
    std::unique_ptr<usb::UsbDs> m_usb;
    std::string m_transfer_file_name{};
    std::unordered_map<std::string, s64> m_file_sizes{};
    u8 m_flags{};
    UsbProtocol m_protocol{UsbProtocol::None};

    // goldleaf request blocks are built in m_gl_req and replies land in
    // m_gl_res. Both live here rather than on the stack: Read() runs on
    // whichever thread yati is installing from, and 0x2000 bytes of locals is
    // more than those get.
    std::array<u8, usb::goldleaf::BLOCK_SIZE> m_gl_req{};
    std::array<u8, usb::goldleaf::BLOCK_SIZE> m_gl_res{};
};

} // namespace sphaira::yati::source
