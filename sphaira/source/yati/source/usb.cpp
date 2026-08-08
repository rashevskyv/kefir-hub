// Usb install over whichever protocol the pc side app speaks: DBI, Awoo/Tinfoil
// or Goldleaf. Which one is on the far end is worked out on connect rather than
// asked of the user -- see WaitForConnection().
#if ENABLE_NETWORK_INSTALL

#include "yati/source/usb.hpp"
#include "usb/tinfoil.hpp"
#include "log.hpp"
#include <cstdlib>
#include <cstring>
#include <ranges>

namespace sphaira::yati::source {
namespace {

namespace dbi = usb::dbi;
namespace tinfoil = usb::tinfoil;
namespace goldleaf = usb::goldleaf;

// how long one step of detection waits before moving on to the next.
constexpr u64 DETECT_TIMEOUT = 1e+9; // 1 second.

// sphaira's extension to the dbi list request: a backend that recognises
// 'SPHA' in data_size appends "|<size>" to every name it returns.
constexpr u32 DBI_LIST_SIZE_EXT = 0x53504841; // 'SPHA'

using goldleaf::BlockReader;
using goldleaf::BlockWriter;

} // namespace

auto GetUsbProtocolName(UsbProtocol protocol) -> const char* {
    switch (protocol) {
        case UsbProtocol::Dbi: return "DBI";
        case UsbProtocol::Tinfoil: return "Awoo/Tinfoil";
        case UsbProtocol::Goldleaf: return "Goldleaf";
        case UsbProtocol::None: break;
    }
    return "Unknown";
}

Usb::Usb(u64 transfer_timeout) {
    m_usb = std::make_unique<usb::UsbDs>(transfer_timeout);
    m_open_result = m_usb->Init();
}

Usb::~Usb() {
}

Result Usb::WaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    // one detection round per call; the install menu loops until this succeeds
    // or the menu is closed, and that loop is what makes cancelling work.
    m_protocol = UsbProtocol::None;
    m_flags = 0;

    // 1. listen. Awoo is the only host that speaks first -- it pushes the file
    //    list the moment the user starts the upload. A dbi or goldleaf host
    //    sits waiting to be asked, so silence here rules out neither.
    u32 magic{};
    auto rc = m_usb->TransferAll(true, &magic, sizeof(magic), DETECT_TIMEOUT);
    if (R_SUCCEEDED(rc)) {
        // a host caught mid conversation can leave a stray block in the pipe;
        // drop it and let the menu come round again rather than guess.
        R_UNLESS(magic == tinfoil::Magic_List0, Result_UsbBadMagic);
        return TinfoilWaitForConnection(timeout, out_names);
    }

    // anything other than "nobody said anything" -- cancelled, link gone -- is
    // the caller's problem, not a hint about which protocol is on the far end.
    R_UNLESS(rc == KERNELRESULT(TimedOut), rc);

    // 2. ask dbi. This probe goes first because it is the cheap one: sixteen
    //    bytes, which a goldleaf host throws away for not starting with GLCI
    //    and an awoo host throws away for not starting with TUC0.
    //
    //    The timeout on the write is what keeps the probes from talking over
    //    each other: a host that is running but has not been started by its
    //    user yet is not reading, so this never leaves the console and the
    //    round ends here rather than going on to push a goldleaf block at it.
    R_TRY(SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::List, DBI_LIST_SIZE_EXT, DETECT_TIMEOUT));

    dbi::CmdHeader header{};
    u32 transferred{};
    rc = m_usb->TransferOnce(true, &header, sizeof(header), &transferred, DETECT_TIMEOUT);
    if (R_SUCCEEDED(rc)) {
        // the user may have pressed upload on an awoo host while the probe was
        // in flight. Its magic travels as its own transfer, so only the magic
        // landed here and the rest of the header is still queued behind it.
        if (transferred == sizeof(magic) && header.magic == tinfoil::Magic_List0) {
            return TinfoilWaitForConnection(timeout, out_names);
        }

        R_UNLESS(transferred == sizeof(header), Result_UsbBadTransferSize);
        return DbiWaitForConnection(header, timeout, out_names);
    }

    R_UNLESS(rc == KERNELRESULT(TimedOut), rc);

    // 3. ask goldleaf, last because its block is 0x1000 bytes and a host that
    //    is not expecting one has to read through the lot to get past it.
    return GoldleafWaitForConnection(timeout, out_names);
}

// MARK: dbi

Result Usb::SendDbiCmdHeader(dbi::CmdType type, dbi::CmdId id, u32 data_size, u64 timeout) {
    dbi::CmdHeader header{
        .magic = dbi::Magic_Dbi0,
        .type = type,
        .id = id,
        .data_size = data_size,
    };

    return m_usb->TransferAll(false, &header, sizeof(header), timeout);
}

Result Usb::DbiWaitForConnection(const dbi::CmdHeader& header, u64 timeout, std::vector<std::string>& out_names) {
    R_UNLESS(header.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(header.id == dbi::CmdId::List, Result_UsbBadMagic);
    R_UNLESS(header.type == dbi::CmdType::Response, Result_UsbBadMagic);

    const u32 list_len = header.data_size;
    log_write("[USB] dbi host detected, list_len: %u\n", list_len);

    out_names.clear();
    m_file_sizes.clear();

    if (list_len > 0) {
        R_TRY(SendDbiCmdHeader(dbi::CmdType::Ack, dbi::CmdId::List, list_len, timeout));

        std::vector<char> names(list_len);
        R_TRY(m_usb->TransferAll(true, names.data(), names.size(), timeout));

        for (const auto& part : std::views::split(names, '\n')) {
            if (part.empty()) {
                continue;
            }

            std::string entry(part.data(), part.size());
            // backends that understand the 'SPHA' request append "|<size>".
            const auto pipe = entry.find('|');
            if (pipe == std::string::npos) {
                m_file_sizes[entry] = 0;
                out_names.emplace_back(std::move(entry));
                continue;
            }

            auto name = entry.substr(0, pipe);
            m_file_sizes[name] = std::strtoll(entry.c_str() + pipe + 1, nullptr, 10);
            out_names.emplace_back(std::move(name));
        }
    }

    for (const auto& name : out_names) {
        log_write("[USB] got name: %s (size: %lld)\n", name.c_str(), (long long)m_file_sizes[name]);
    }

    R_UNLESS(!out_names.empty(), Result_UsbBadCount);
    m_protocol = UsbProtocol::Dbi;
    log_write("[USB] Connection success (Protocol: DBI)\n");
    R_SUCCEED();
}

Result Usb::DbiRead(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_UNLESS(off >= 0 && size >= 0 && size <= UINT32_MAX, Result_UsbBadTransferSize);

    const auto timeout = m_usb->GetTransferTimeout();
    const u32 name_len = m_transfer_file_name.size();
    const u32 payload_size = sizeof(dbi::FileRangeHeader) + name_len;

    // 1. console sends the command header.
    R_TRY(SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::FileRange, payload_size, timeout));

    // 2. pc acks it.
    dbi::CmdHeader ack{};
    R_TRY(m_usb->TransferAll(true, &ack, sizeof(ack), timeout));
    R_UNLESS(ack.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(ack.id == dbi::CmdId::FileRange, Result_UsbBadMagic);
    R_UNLESS(ack.type == dbi::CmdType::Ack, Result_UsbBadMagic);

    // 3. console writes the range and the name it belongs to.
    const dbi::FileRangeHeader range{
        .range_size = static_cast<u32>(size),
        .range_offset = static_cast<u64>(off),
        .name_len = name_len,
    };
    std::vector<u8> payload(payload_size);
    std::memcpy(payload.data(), &range, sizeof(range));
    std::memcpy(payload.data() + sizeof(range), m_transfer_file_name.data(), name_len);
    R_TRY(m_usb->TransferAll(false, payload.data(), static_cast<u32>(payload.size()), timeout));

    // 4. pc answers with the size it is about to send.
    dbi::CmdHeader response{};
    R_TRY(m_usb->TransferAll(true, &response, sizeof(response), timeout));
    R_UNLESS(response.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(response.id == dbi::CmdId::FileRange, Result_UsbBadMagic);
    R_UNLESS(response.type == dbi::CmdType::Response, Result_UsbBadMagic);
    R_UNLESS(response.data_size == size, Result_UsbBadCount);

    // 5. console acks, 6. pc sends the bytes.
    R_TRY(SendDbiCmdHeader(dbi::CmdType::Ack, dbi::CmdId::FileRange, response.data_size, timeout));
    R_TRY(m_usb->TransferAll(true, buf, response.data_size, timeout));

    *bytes_read = response.data_size;
    R_SUCCEED();
}

// MARK: tinfoil / awoo

Result Usb::TinfoilWaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    // the magic was already taken off the wire by whichever read spotted it.
    tinfoil::TUSHeader header{.magic = tinfoil::Magic_List0};
    constexpr auto skip = sizeof(header.magic);
    R_TRY(m_usb->TransferAll(true, (u8*)&header + skip, sizeof(header) - skip, timeout));

    m_flags = header.flags;
    log_write("[USB] Tinfoil/Awoo header detected, flags: 0x%X\n", m_flags);

    R_UNLESS(header.nspListSize > 0, Result_UsbBadCount);

    std::vector<char> names(header.nspListSize);
    R_TRY(m_usb->TransferAll(true, names.data(), names.size(), timeout));

    out_names.clear();
    m_file_sizes.clear();
    for (const auto& name : std::views::split(names, '\n')) {
        if (!name.empty()) {
            out_names.emplace_back(name.data(), name.size());
        }
    }

    for (auto& name : out_names) {
        log_write("[USB] got name: %s\n", name.c_str());
    }

    R_UNLESS(!out_names.empty(), Result_UsbBadCount);
    m_protocol = UsbProtocol::Tinfoil;
    log_write("[USB] Connection success (Protocol: Awoo/Tinfoil)\n");
    R_SUCCEED();
}

Result Usb::SendTinfoilCmdHeader(u32 cmd_id, size_t data_size, u64 timeout) {
    tinfoil::USBCmdHeader header{
        .magic = tinfoil::Magic_Command0,
        .type = tinfoil::USBCmdType::REQUEST,
        .cmdId = cmd_id,
        .dataSize = data_size,
    };

    return m_usb->TransferAll(false, &header, sizeof(header), timeout);
}

Result Usb::SendFileRangeCmd(u64 off, u64 size, u64 timeout) {
    tinfoil::FileRangeCmdHeader range{};
    range.size = size;
    range.offset = off;
    range.nspNameLen = m_transfer_file_name.size();
    range.padding = 0;

    R_TRY(SendTinfoilCmdHeader(tinfoil::USBCmdId::FILE_RANGE, sizeof(range) + range.nspNameLen, timeout));
    R_TRY(m_usb->TransferAll(false, &range, sizeof(range), timeout));
    R_TRY(m_usb->TransferAll(false, m_transfer_file_name.data(), range.nspNameLen, timeout));

    tinfoil::USBCmdHeader response{};
    R_TRY(m_usb->TransferAll(true, &response, sizeof(response), timeout));

    R_SUCCEED();
}

Result Usb::TinfoilRead(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_TRY(SendFileRangeCmd(off, size, m_usb->GetTransferTimeout()));
    R_TRY(m_usb->TransferAll(true, buf, size));
    *bytes_read = size;
    R_SUCCEED();
}

// MARK: goldleaf

BlockWriter Usb::GlBegin(goldleaf::CmdId cmd) {
    std::memset(m_gl_req.data(), 0, goldleaf::BLOCK_SIZE);
    BlockWriter w{m_gl_req.data()};
    w.U32(goldleaf::Magic_In);
    w.U32(static_cast<u32>(cmd));
    return w;
}

Result Usb::GlSendRecv(const BlockWriter& writer, u64 timeout, u32* out_transferred) {
    R_UNLESS(writer.Ok(), Result_UsbBadTransferSize);

    // no zlt: the host asks for exactly one block, so a terminator would be
    // left in the pipe and come back as an empty reply on its next read.
    R_TRY(m_usb->TransferAll(false, m_gl_req.data(), goldleaf::BLOCK_SIZE, timeout, false));

    // a reply block is always one write on the host side, so one transfer
    // carries the whole of it -- and anything shorter is not one.
    return m_usb->TransferOnce(true, m_gl_res.data(), goldleaf::BLOCK_SIZE, out_transferred, timeout);
}

Result Usb::GlTransact(const BlockWriter& writer, u64 timeout, BlockReader* out_reader) {
    u32 transferred{};
    R_TRY(GlSendRecv(writer, timeout, &transferred));
    R_UNLESS(transferred == goldleaf::BLOCK_SIZE, Result_UsbBadTransferSize);

    *out_reader = BlockReader{m_gl_res.data()};

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

Result Usb::GoldleafWaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    // the drive count doubles as the handshake.
    u32 transferred{};
    R_TRY(GlSendRecv(GlBegin(goldleaf::CmdId::GetDriveCount), DETECT_TIMEOUT, &transferred));

    // same race as the dbi probe: an awoo host can start pushing its list while
    // ours is on the wire, and its magic arrives as its own short transfer.
    u32 reply_magic{};
    std::memcpy(&reply_magic, m_gl_res.data(), sizeof(reply_magic));
    if (transferred == sizeof(reply_magic) && reply_magic == tinfoil::Magic_List0) {
        return TinfoilWaitForConnection(timeout, out_names);
    }

    R_UNLESS(transferred == goldleaf::BLOCK_SIZE, Result_UsbBadTransferSize);
    R_UNLESS(reply_magic == goldleaf::Magic_Out, Result_UsbBadMagic);

    BlockReader r{m_gl_res.data()};
    {
        u32 magic, host_rc, drives;
        R_UNLESS(r.U32(&magic) && r.U32(&host_rc), Result_UsbBadTransferSize);
        R_UNLESS(!host_rc, Result_UsbGoldleafFailed);
        R_UNLESS(r.U32(&drives), Result_UsbBadTransferSize);
        log_write("[USB] Goldleaf host detected, %u drive(s)\n", drives);
        R_UNLESS(drives > 0, Result_UsbBadCount);
    }

    // only the virtual drive is installed from -- it is where ns-usbloader puts
    // the files queued in its window. Walking the pc's real filesystem would
    // need a file browser, which the usb install screen is not.
    u32 count;
    {
        auto w = GlBegin(goldleaf::CmdId::GetFileCount);
        w.Str(goldleaf::VIRTUAL_DRIVE);
        R_TRY(GlTransact(w, timeout, &r));
        R_UNLESS(r.U32(&count), Result_UsbBadTransferSize);
    }

    R_UNLESS(count > 0, Result_UsbBadCount);

    out_names.clear();
    m_file_sizes.clear();
    for (u32 i = 0; i < count; i++) {
        auto w = GlBegin(goldleaf::CmdId::GetFile);
        w.Str(goldleaf::VIRTUAL_DRIVE);
        w.U32(i);
        R_TRY(GlTransact(w, timeout, &r));

        std::string name;
        R_UNLESS(r.Str(&name), Result_UsbBadTransferSize);
        R_UNLESS(!name.empty(), Result_UsbBadCount);
        log_write("[USB] got name: %s\n", name.c_str());
        out_names.emplace_back(std::move(name));
    }

    m_protocol = UsbProtocol::Goldleaf;
    log_write("[USB] Connection success (Protocol: Goldleaf)\n");
    R_SUCCEED();
}

Result Usb::GoldleafRead(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_UNLESS(off >= 0 && size > 0, Result_UsbBadTransferSize);

    const auto timeout = m_usb->GetTransferTimeout();

    auto w = GlBegin(goldleaf::CmdId::ReadFile);
    w.Str(std::string{goldleaf::VIRTUAL_DRIVE} + m_transfer_file_name);
    w.U64(off);
    w.U64(size);

    BlockReader r;
    R_TRY(GlTransact(w, timeout, &r));

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

// MARK: shared

void Usb::SetFileNameForTranfser(const std::string& name) {
    m_transfer_file_name = name;
}

s64 Usb::GetFileSize(const std::string& name) const {
    if (const auto it = m_file_sizes.find(name); it != m_file_sizes.end()) {
        return it->second;
    }
    return 0;
}

Result Usb::Finished(u64 timeout) {
    switch (m_protocol) {
        case UsbProtocol::Dbi:
            log_write("[USB] sending finished command (dbi)\n");
            return SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::Exit, 0, timeout);

        case UsbProtocol::Tinfoil:
            log_write("[USB] sending finished command (tinfoil)\n");
            return SendTinfoilCmdHeader(tinfoil::USBCmdId::EXIT, 0, timeout);

        // goldleaf has no exit command: the host serves requests until the user
        // stops it, so we just stop asking.
        case UsbProtocol::Goldleaf:
        case UsbProtocol::None:
            R_SUCCEED();
    }

    R_SUCCEED();
}

bool Usb::IsStream() const {
    // the stream flag is a tinfoil extension; dbi and goldleaf are both random
    // access by construction and leave m_flags at 0.
    return m_protocol == UsbProtocol::Tinfoil && (m_flags & tinfoil::USBFlag_STREAM);
}

Result Usb::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_TRY(GetOpenResult());

    switch (m_protocol) {
        case UsbProtocol::Dbi: return DbiRead(buf, off, size, bytes_read);
        case UsbProtocol::Tinfoil: return TinfoilRead(buf, off, size, bytes_read);
        case UsbProtocol::Goldleaf: return GoldleafRead(buf, off, size, bytes_read);
        case UsbProtocol::None: break;
    }

    R_THROW(Result_UsbBadMagic);
}

} // namespace sphaira::yati::source

#endif
