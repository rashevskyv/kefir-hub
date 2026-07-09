#if ENABLE_NETWORK_INSTALL

#include "yati/source/usb_dbi.hpp"
#include "usb/dbi.hpp"
#include "log.hpp"
#include <ranges>

namespace sphaira::yati::source {
namespace {

namespace dbi = usb::dbi;

} // namespace

DbiUsb::DbiUsb(u64 transfer_timeout) {
    m_usb = std::make_unique<usb::UsbDs>(transfer_timeout);
    m_open_result = m_usb->Init();
}

DbiUsb::~DbiUsb() {
}

Result DbiUsb::WaitForConnection(u64 timeout, std::vector<std::string>& out_names) {
    log_write("[DBI USB] Waiting for connection / list request...\n");
    // Send list request: magic 'DBI0', type REQUEST, id LIST, data_size 0
    R_TRY(SendCmdHeader(dbi::CmdType::Request, dbi::CmdId::List, 0, timeout));

    // Wait for response header: magic 'DBI0', type RESPONSE, id LIST, data_size list_len
    dbi::CmdHeader responseHeader{};
    R_TRY(m_usb->TransferAll(true, &responseHeader, sizeof(responseHeader), timeout));
    R_UNLESS(responseHeader.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(responseHeader.id == dbi::CmdId::List, Result_UsbBadMagic);
    R_UNLESS(responseHeader.type == dbi::CmdType::Response, Result_UsbBadMagic);

    u32 list_len = responseHeader.data_size;
    log_write("[DBI USB] got response, list_len: %u\n", list_len);

    out_names.clear();
    if (list_len > 0) {
        // Send ACK: magic 'DBI0', type ACK, id LIST, data_size list_len
        R_TRY(SendCmdHeader(dbi::CmdType::Ack, dbi::CmdId::List, list_len, timeout));

        // Read the file list block
        std::vector<char> names(list_len);
        R_TRY(m_usb->TransferAll(true, names.data(), names.size(), timeout));

        for (const auto& name : std::views::split(names, '\n')) {
            if (!name.empty()) {
                out_names.emplace_back(name.data(), name.size());
            }
        }
    }

    for (auto& name : out_names) {
        log_write("[DBI USB] got name: %s\n", name.c_str());
    }

    R_UNLESS(!out_names.empty(), Result_UsbBadCount);
    log_write("[DBI USB] connection and list retrieval success!\n");
    R_SUCCEED();
}

void DbiUsb::SetFileNameForTranfser(const std::string& name) {
    m_transfer_file_name = name;
}

Result DbiUsb::SendCmdHeader(dbi::CmdType type, dbi::CmdId id, u32 dataSize, u64 timeout) {
    dbi::CmdHeader header{
        .magic = dbi::Magic_Dbi0,
        .type = type,
        .id = id,
        .data_size = dataSize,
    };

    return m_usb->TransferAll(false, &header, sizeof(header), timeout);
}

Result DbiUsb::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    R_TRY(GetOpenResult());

    u64 timeout = m_usb->GetTransferTimeout();
    u32 nsp_name_len = m_transfer_file_name.size();
    u32 payload_size = sizeof(dbi::FileRangeHeader) + nsp_name_len;

    // 1. Console sends command header: REQUEST, FILE_RANGE, payload_size
    R_TRY(SendCmdHeader(dbi::CmdType::Request, dbi::CmdId::FileRange, payload_size, timeout));

    // 2. PC sends ACK: magic 'DBI0', type ACK, id FILE_RANGE, payload_size. Read it:
    dbi::CmdHeader ackHeader{};
    R_TRY(m_usb->TransferAll(true, &ackHeader, sizeof(ackHeader), timeout));
    R_UNLESS(ackHeader.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(ackHeader.id == dbi::CmdId::FileRange, Result_UsbBadMagic);
    R_UNLESS(ackHeader.type == dbi::CmdType::Ack, Result_UsbBadMagic);

    // 3. Console writes the payload: range_size, range_offset, nsp_name_len, name
    dbi::FileRangeHeader payload{
        .range_size = static_cast<u32>(size),
        .range_offset = static_cast<u64>(off),
        .name_len = nsp_name_len
    };
    R_TRY(m_usb->TransferAll(false, &payload, sizeof(payload), timeout));
    R_TRY(m_usb->TransferAll(false, m_transfer_file_name.data(), nsp_name_len, timeout));

    // 4. PC sends RESPONSE header: RESPONSE, FILE_RANGE, range_size. Read it:
    dbi::CmdHeader responseHeader{};
    R_TRY(m_usb->TransferAll(true, &responseHeader, sizeof(responseHeader), timeout));
    R_UNLESS(responseHeader.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(responseHeader.id == dbi::CmdId::FileRange, Result_UsbBadMagic);
    R_UNLESS(responseHeader.type == dbi::CmdType::Response, Result_UsbBadMagic);
    R_UNLESS(responseHeader.data_size == size, Result_UsbBadCount);

    // 5. Console sends ACK to PC: ACK, FILE_RANGE, range_size
    R_TRY(SendCmdHeader(dbi::CmdType::Ack, dbi::CmdId::FileRange, responseHeader.data_size, timeout));

    // 6. PC sends range_size bytes of data. Read it:
    R_TRY(m_usb->TransferAll(true, buf, responseHeader.data_size, timeout));

    *bytes_read = responseHeader.data_size;
    R_SUCCEED();
}

Result DbiUsb::Finished(u64 timeout) {
    log_write("[DBI USB] sending finished command\n");
    return SendCmdHeader(dbi::CmdType::Request, dbi::CmdId::Exit, 0, timeout);
}

bool DbiUsb::IsStream() const {
    return false;
}

} // namespace sphaira::yati::source

#endif
