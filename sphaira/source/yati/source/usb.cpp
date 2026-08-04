// Awoo/Tinfoil and Goldleaf usb install, auto-detected on connect.
#if ENABLE_NETWORK_INSTALL

#include "yati/source/usb.hpp"
#include "usb/tinfoil.hpp"
#include "usb/goldleaf.hpp"
#include "log.hpp"
#include <cstring>
#include <ranges>

namespace sphaira::yati::source {
namespace {

namespace tinfoil = usb::tinfoil;
namespace goldleaf = usb::goldleaf;

// how long one detection attempt listens before knocking the other way round.
// An awoo host pushes its file list the moment the transfer starts, a goldleaf
// host sits waiting to be asked, so listening first and only then sending a
// probe is the only thing that separates them. The order matters: probing
// first would leave our block in the pipe for an awoo host to pick up as its
// first command.
constexpr u64 DETECT_TIMEOUT = 1e+9; // 1 second.

using goldleaf::BlockReader;
using goldleaf::BlockWriter;

// starts a request block: magic and command id, payload appended by the caller.
BlockWriter GlBegin(u8* buf, goldleaf::CmdId cmd) {
    std::memset(buf, 0, goldleaf::BLOCK_SIZE);
    BlockWriter w{buf};
    w.U32(goldleaf::Magic_In);
    w.U32(static_cast<u32>(cmd));
    return w;
}

// sends the request block and reads the reply back into res. On success
// out_reader is positioned at the reply payload, past magic and result code.
Result GlTransact(usb::UsbDs* usb, u8* req, const BlockWriter& writer, u8* res, u64 timeout, BlockReader* out_reader) {
    R_UNLESS(writer.Ok(), Result_UsbBadTransferSize);

    R_TRY(usb->TransferAll(false, req, goldleaf::BLOCK_SIZE, timeout, false));
    R_TRY(usb->TransferAll(true, res, goldleaf::BLOCK_SIZE, timeout));

    *out_reader = BlockReader{res};

    u32 magic, rc;
    R_UNLESS(out_reader->U32(&magic), Result_UsbBadTransferSize);
    R_UNLESS(out_reader->U32(&rc), Result_UsbBadTransferSize);
    R_UNLESS(magic == goldleaf::Magic_Out, Result_UsbBadMagic);

    // the host reports its own failures in here (no such path, bad index, ...).
    // None of them map onto a switch result, so they all surface as one code
    // with the raw value in the log.
    if (rc) {
        log_write("[USB] goldleaf command failed, host rc: 0x%08X\n", rc);
        R_THROW(Result_UsbGoldleafFailed);
    }

    R_SUCCEED();
}

} // namespace

Usb::Usb(u64 transfer_timeout) {
    m_usb = std::make_unique<usb::UsbDs>(transfer_timeout);
    m_open_result = m_usb->Init();
}

Usb::~Usb() {
}

Result Usb::WaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    // one attempt per call; the usb menu already loops until this succeeds or
    // the menu is closed, and that loop is what makes cancelling work.
    u32 magic{};
    const auto rc = m_usb->TransferAll(true, &magic, sizeof(magic), DETECT_TIMEOUT);

    if (R_SUCCEEDED(rc)) {
        R_UNLESS(magic == tinfoil::Magic_List0, Result_UsbBadMagic);
        m_protocol = UsbProtocol::Tinfoil;
        return TinfoilWaitForConnection(timeout, out_names);
    }

    // anything other than "nobody said anything" -- cancelled, link gone -- is
    // the caller's problem, not a hint about which protocol is on the far end.
    R_UNLESS(rc == KERNELRESULT(TimedOut), rc);

    m_protocol = UsbProtocol::Goldleaf;
    return GoldleafWaitForConnection(timeout, out_names);
}

Result Usb::TinfoilWaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    // the magic was already taken off the wire by the detection read.
    tinfoil::TUSHeader header{.magic = tinfoil::Magic_List0};
    constexpr auto skip = sizeof(header.magic);
    R_TRY(m_usb->TransferAll(true, (u8*)&header + skip, sizeof(header) - skip, timeout));

    m_flags = header.flags;
    log_write("[USB] Tinfoil/Awoo header detected, flags: 0x%X\n", m_flags);

    R_UNLESS(header.nspListSize > 0, Result_UsbBadCount);

    std::vector<char> names(header.nspListSize);
    R_TRY(m_usb->TransferAll(true, names.data(), names.size(), timeout));

    out_names.clear();
    for (const auto& name : std::views::split(names, '\n')) {
        if (!name.empty()) {
            out_names.emplace_back(name.data(), name.size());
        }
    }

    for (auto& name : out_names) {
        log_write("[USB] got name: %s\n", name.c_str());
    }

    R_UNLESS(!out_names.empty(), Result_UsbBadCount);
    log_write("[USB] Connection success (Protocol: Awoo/Tinfoil)\n");
    R_SUCCEED();
}

Result Usb::GoldleafWaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    BlockReader r;

    // the drive count doubles as the handshake. A host that is not speaking
    // goldleaf never reads this block, so the write times out and is cancelled
    // before anything lands on the wire.
    {
        const auto w = GlBegin(m_gl_req.data(), goldleaf::CmdId::GetDriveCount);
        R_TRY(GlTransact(m_usb.get(), m_gl_req.data(), w, m_gl_res.data(), DETECT_TIMEOUT, &r));

        u32 drives;
        R_UNLESS(r.U32(&drives), Result_UsbBadTransferSize);
        log_write("[USB] Goldleaf host detected, %u drive(s)\n", drives);
        R_UNLESS(drives > 0, Result_UsbBadCount);
    }

    // only the virtual drive is installed from -- it is where ns-usbloader puts
    // the files queued in its window. Walking the pc's real filesystem would
    // need a file browser, which the usb install screen is not.
    u32 count;
    {
        auto w = GlBegin(m_gl_req.data(), goldleaf::CmdId::GetFileCount);
        w.Str(goldleaf::VIRTUAL_DRIVE);
        R_TRY(GlTransact(m_usb.get(), m_gl_req.data(), w, m_gl_res.data(), timeout, &r));
        R_UNLESS(r.U32(&count), Result_UsbBadTransferSize);
    }

    R_UNLESS(count > 0, Result_UsbBadCount);

    out_names.clear();
    for (u32 i = 0; i < count; i++) {
        auto w = GlBegin(m_gl_req.data(), goldleaf::CmdId::GetFile);
        w.Str(goldleaf::VIRTUAL_DRIVE);
        w.U32(i);
        R_TRY(GlTransact(m_usb.get(), m_gl_req.data(), w, m_gl_res.data(), timeout, &r));

        std::string name;
        R_UNLESS(r.Str(&name), Result_UsbBadTransferSize);
        R_UNLESS(!name.empty(), Result_UsbBadCount);
        log_write("[USB] got name: %s\n", name.c_str());
        out_names.emplace_back(std::move(name));
    }

    log_write("[USB] Connection success (Protocol: Goldleaf)\n");
    R_SUCCEED();
}

Result Usb::GoldleafRead(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_UNLESS(off >= 0 && size > 0, Result_UsbBadTransferSize);

    const auto timeout = m_usb->GetTransferTimeout();

    auto w = GlBegin(m_gl_req.data(), goldleaf::CmdId::ReadFile);
    w.Str(std::string{goldleaf::VIRTUAL_DRIVE} + m_transfer_file_name);
    w.U64(off);
    w.U64(size);

    BlockReader r;
    R_TRY(GlTransact(m_usb.get(), m_gl_req.data(), w, m_gl_res.data(), timeout, &r));

    // the host reports a short read by failing the command rather than by
    // answering with a smaller count, so anything but an exact match here is a
    // desync and the bulk transfer that follows would not line up.
    u64 host_size;
    R_UNLESS(r.U64(&host_size), Result_UsbBadTransferSize);
    R_UNLESS(host_size == (u64)size, Result_UsbBadCount);

    // the file data follows the reply block as its own transfer.
    R_TRY(m_usb->TransferAll(true, buf, size, timeout));

    *bytes_read = size;
    R_SUCCEED();
}

void Usb::SetFileNameForTranfser(const std::string& name) {
    m_transfer_file_name = name;
}

Result Usb::SendCmdHeader(u32 cmdId, size_t dataSize, u64 timeout) {
    tinfoil::USBCmdHeader header{
        .magic = tinfoil::Magic_Command0,
        .type = tinfoil::USBCmdType::REQUEST,
        .cmdId = cmdId,
        .dataSize = dataSize,
    };

    return m_usb->TransferAll(false, &header, sizeof(header), timeout);
}

Result Usb::SendFileRangeCmd(u64 off, u64 size, u64 timeout) {
    tinfoil::FileRangeCmdHeader fRangeHeader{};
    fRangeHeader.size = size;
    fRangeHeader.offset = off;
    fRangeHeader.nspNameLen = m_transfer_file_name.size();
    fRangeHeader.padding = 0;

    R_TRY(SendCmdHeader(tinfoil::USBCmdId::FILE_RANGE, sizeof(fRangeHeader) + fRangeHeader.nspNameLen, timeout));
    R_TRY(m_usb->TransferAll(false, &fRangeHeader, sizeof(fRangeHeader), timeout));
    R_TRY(m_usb->TransferAll(false, m_transfer_file_name.data(), fRangeHeader.nspNameLen, timeout));

    tinfoil::USBCmdHeader responseHeader{};
    R_TRY(m_usb->TransferAll(true, &responseHeader, sizeof(responseHeader), timeout));

    R_SUCCEED();
}

Result Usb::Finished(u64 timeout) {
    // goldleaf has no exit command: the host serves requests until the user
    // stops it, so we just stop asking.
    if (m_protocol == UsbProtocol::Goldleaf) {
        R_SUCCEED();
    }

    log_write("[USB] sending finished command\n");
    return SendCmdHeader(tinfoil::USBCmdId::EXIT, 0, timeout);
}

bool Usb::IsStream() const {
    // goldleaf is random access by construction and leaves m_flags at 0.
    return (m_flags & tinfoil::USBFlag_STREAM);
}

Result Usb::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_TRY(GetOpenResult());

    if (m_protocol == UsbProtocol::Goldleaf) {
        return GoldleafRead(buf, off, size, bytes_read);
    }

    R_TRY(SendFileRangeCmd(off, size, m_usb->GetTransferTimeout()));
    R_TRY(m_usb->TransferAll(true, buf, size));
    *bytes_read = size;
    R_SUCCEED();
}

} // namespace sphaira::yati::source

#endif
