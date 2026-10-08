#pragma once

#include "base.hpp"
#include "fs.hpp"
#include "usb/dbi.hpp"
#include "usb/goldleaf.hpp"
#include "usb/usbds.hpp"

#include <array>
#include <functional>
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
    // Only dbi backends that understand the 'SPHA' or 'SPHQ' list request report sizes.
    s64 GetFileSize(const std::string& name) const;
    int GetFileTarget(const std::string& name) const;

    bool HasSelectionSync() const {
        return m_dbi_selection_sync;
    }

    using PostReadHook = std::function<void()>;
    void SetPostReadHook(PostReadHook hook) {
        m_post_read_hook = std::move(hook);
    }

    struct LiveQueueItem {
        std::string name;
        s64 size{0};
        bool selected{false};
        int target{0};
    };

    Result FetchLiveQueue(std::vector<LiveQueueItem>& out_items, u32& out_revision, u64 timeout = 1e+9);
    Result SendQueueAck(u32 revision, u64 timeout = 1e+9);

    Result FetchLiveSelection(std::unordered_map<std::string, bool>& out_selections, std::unordered_map<std::string, int>& out_targets, u64 timeout = 1e+9);
    Result FetchLiveSelection(std::unordered_map<std::string, bool>& out_selections, u64 timeout = 1e+9);

    Result SendPackageStatus(const std::string& name, u32 status, Result rc, u64 timeout = 1e+9);
    Result SendStorageInfo(u64 nand_free, u64 nand_total, u64 sd_free, u64 sd_total, u64 timeout = 1e+9);

    // one package of the console's queue plan, see usb::dbi::QueuePlanRecord.
    struct QueuePlanItem {
        std::string name;
        bool selected{};
        int target{};
        bool planned_sd{};
        bool analysis_ok{};
        bool already_installed{};
        u64 install_size{};
    };
    Result SendQueuePlan(const std::vector<QueuePlanItem>& items, u32 revision, u64 timeout = 1e+9);

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
    std::unordered_map<std::string, int> m_file_targets{};
    u8 m_flags{};
    UsbProtocol m_protocol{UsbProtocol::None};
    bool m_dbi_selection_sync{false};
    PostReadHook m_post_read_hook{};

    // goldleaf request blocks are built in m_gl_req and replies land in
    // m_gl_res. Both live here rather than on the stack: Read() runs on
    // whichever thread yati is installing from, and 0x2000 bytes of locals is
    // more than those get.
    std::array<u8, usb::goldleaf::BLOCK_SIZE> m_gl_req{};
    std::array<u8, usb::goldleaf::BLOCK_SIZE> m_gl_res{};
};

} // namespace sphaira::yati::source
