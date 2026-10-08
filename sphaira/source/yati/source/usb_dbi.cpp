#if ENABLE_NETWORK_INSTALL

#include "yati/source/usb.hpp"
#include "yati/source/usb_internal.hpp"
#include "log.hpp"
#include <cstdlib>
#include <cstring>
#include <ranges>

namespace sphaira::yati::source {

namespace dbi = usb::dbi;

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
    m_dbi_selection_sync = false;

    if (list_len > 0) {
        R_TRY(SendDbiCmdHeader(dbi::CmdType::Ack, dbi::CmdId::List, list_len, timeout));

        std::vector<char> names(list_len);
        R_TRY(m_usb->TransferAll(true, names.data(), names.size(), timeout));

        // An empty SPHQ queue has a distinct whole-payload marker. A marker
        // mixed into a normal list must not grant selection-sync capability.
        bool has_sync_data = std::string_view{names.data(), names.size()} == DBI_SPHQ_EMPTY_PAYLOAD;
        bool has_empty_marker = false;
        bool has_rev_header = false;
        for (const auto& part : std::views::split(names, '\n')) {
            if (part.empty()) {
                continue;
            }

            std::string entry(part.data(), part.size());
            if (!entry.empty() && entry.back() == '\r') {
                entry.pop_back();
            }
            if (entry.empty()) {
                continue;
            }
            if (entry == DBI_SPHQ_EMPTY_MARKER) {
                has_empty_marker = true;
                continue;
            }
            if (entry.starts_with(DBI_SPHQ_REV_PREFIX)) {
                has_rev_header = true;
                continue;
            }

            // backends that understand the 'SPHA' or 'SPHQ' request append "|<size>" and optionally "|<selected>".
            const auto pipe1 = entry.find('|');
            if (pipe1 == std::string::npos) {
                m_file_sizes[entry] = 0;
                out_names.emplace_back(std::move(entry));
                continue;
            }

            const auto pipe2 = entry.find('|', pipe1 + 1);
            auto name = entry.substr(0, pipe1);
            m_file_sizes[name] = std::strtoll(entry.c_str() + pipe1 + 1, nullptr, 10);
            if (pipe2 != std::string::npos) {
                has_sync_data = true;
                bool selected = (std::strtol(entry.c_str() + pipe2 + 1, nullptr, 10) != 0);
                const auto pipe3 = entry.find('|', pipe2 + 1);
                if (pipe3 != std::string::npos) {
                    m_file_targets[name] = std::strtol(entry.c_str() + pipe3 + 1, nullptr, 10);
                } else {
                    m_file_targets[name] = 0;
                }
                if (selected) {
                    out_names.emplace_back(std::move(name));
                }
            } else {
                m_file_targets[name] = 0;
                out_names.emplace_back(std::move(name));
            }
        }
        if (has_rev_header && has_empty_marker && out_names.empty()) {
            has_sync_data = true;
        }
        m_dbi_selection_sync = has_sync_data;
    }

    for (const auto& name : out_names) {
        log_write("[USB] got name: %s (size: %lld)\n", name.c_str(), (long long)m_file_sizes[name]);
    }

    R_UNLESS(!out_names.empty() || m_dbi_selection_sync, Result_UsbBadCount);
    m_protocol = UsbProtocol::Dbi;
    log_write("[USB] Connection success (Protocol: DBI, selection sync: %s)\n", m_dbi_selection_sync ? "enabled" : "disabled");
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
    if (m_dbi_selection_sync && m_post_read_hook) {
        m_post_read_hook();
    }
    R_SUCCEED();
}

int Usb::GetFileTarget(const std::string& name) const {
    auto it = m_file_targets.find(name);
    if (it != m_file_targets.end()) {
        return it->second;
    }
    return 0;
}

Result Usb::FetchLiveQueue(std::vector<LiveQueueItem>& out_items, u32& out_revision, u64 timeout) {
    R_UNLESS(m_protocol == UsbProtocol::Dbi && m_dbi_selection_sync, Result_UsbBadMagic);

    R_TRY(SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::List, DBI_LIST_QUEUE_EXT, timeout));

    dbi::CmdHeader response{};
    R_TRY(m_usb->TransferAll(true, &response, sizeof(response), timeout));
    R_UNLESS(response.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(response.id == dbi::CmdId::List, Result_UsbBadMagic);
    R_UNLESS(response.type == dbi::CmdType::Response, Result_UsbBadMagic);

    const u32 list_len = response.data_size;
    out_items.clear();
    out_revision = 0;

    if (list_len > 0) {
        R_TRY(SendDbiCmdHeader(dbi::CmdType::Ack, dbi::CmdId::List, list_len, timeout));

        std::vector<char> names(list_len);
        R_TRY(m_usb->TransferAll(true, names.data(), names.size(), timeout));

        for (const auto& part : std::views::split(names, '\n')) {
            if (part.empty()) {
                continue;
            }

            std::string entry(part.data(), part.size());
            if (!entry.empty() && entry.back() == '\r') {
                entry.pop_back();
            }
            if (entry.empty() || entry == DBI_SPHQ_EMPTY_MARKER) {
                continue;
            }
            if (entry.starts_with(DBI_SPHQ_REV_PREFIX)) {
                out_revision = std::strtoul(entry.c_str() + DBI_SPHQ_REV_PREFIX.size(), nullptr, 10);
                continue;
            }

            const auto pipe1 = entry.find('|');
            if (pipe1 == std::string::npos) {
                continue;
            }
            const auto pipe2 = entry.find('|', pipe1 + 1);
            if (pipe2 == std::string::npos) {
                continue;
            }

            auto name = entry.substr(0, pipe1);
            s64 size = std::strtoll(entry.c_str() + pipe1 + 1, nullptr, 10);
            m_file_sizes[name] = size;
            bool selected = (std::strtol(entry.c_str() + pipe2 + 1, nullptr, 10) != 0);

            int target = 0;
            const auto pipe3 = entry.find('|', pipe2 + 1);
            if (pipe3 != std::string::npos) {
                target = std::strtol(entry.c_str() + pipe3 + 1, nullptr, 10);
            }
            m_file_targets[name] = target;

            out_items.push_back({
                .name = std::move(name),
                .size = size,
                .selected = selected,
                .target = target,
            });
        }
    }

    R_SUCCEED();
}

Result Usb::SendQueueAck(u32 revision, u64 timeout) {
    R_UNLESS(m_protocol == UsbProtocol::Dbi && m_dbi_selection_sync, Result_UsbBadMagic);
    return SendDbiCmdHeader(dbi::CmdType::Ack, dbi::CmdId::List, revision, timeout);
}

Result Usb::FetchLiveSelection(std::unordered_map<std::string, bool>& out_selections, std::unordered_map<std::string, int>& out_targets, u64 timeout) {
    std::vector<LiveQueueItem> items;
    u32 rev = 0;
    R_TRY(FetchLiveQueue(items, rev, timeout));
    out_selections.clear();
    out_targets.clear();
    for (const auto& it : items) {
        out_selections[it.name] = it.selected;
        out_targets[it.name] = it.target;
    }
    R_SUCCEED();
}

Result Usb::FetchLiveSelection(std::unordered_map<std::string, bool>& out_selections, u64 timeout) {
    std::unordered_map<std::string, int> dummy_targets;
    return FetchLiveSelection(out_selections, dummy_targets, timeout);
}

Result Usb::SendPackageStatus(const std::string& name, u32 status, Result rc, u64 timeout) {
    R_UNLESS(m_protocol == UsbProtocol::Dbi && m_dbi_selection_sync, Result_UsbBadMagic);

    const u32 name_len = name.size();
    const u32 payload_size = sizeof(dbi::PackageStatusHeader) + name_len;

    R_TRY(SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::PackageStatus, payload_size, timeout));

    dbi::CmdHeader ack{};
    R_TRY(m_usb->TransferAll(true, &ack, sizeof(ack), timeout));
    R_UNLESS(ack.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(ack.id == dbi::CmdId::PackageStatus, Result_UsbBadMagic);
    R_UNLESS(ack.type == dbi::CmdType::Ack, Result_UsbBadMagic);

    const dbi::PackageStatusHeader header{
        .status = status,
        .result_code = static_cast<u32>(rc),
        .name_len = name_len,
    };
    std::vector<u8> payload(payload_size);
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), name.data(), name_len);
    R_TRY(m_usb->TransferAll(false, payload.data(), static_cast<u32>(payload.size()), timeout));

    dbi::CmdHeader response{};
    R_TRY(m_usb->TransferAll(true, &response, sizeof(response), timeout));
    R_UNLESS(response.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(response.id == dbi::CmdId::PackageStatus, Result_UsbBadMagic);
    R_UNLESS(response.type == dbi::CmdType::Response, Result_UsbBadMagic);

    R_SUCCEED();
}

Result Usb::SendQueuePlan(const std::vector<QueuePlanItem>& items, u32 revision, u64 timeout) {
    R_UNLESS(m_protocol == UsbProtocol::Dbi && m_dbi_selection_sync, Result_UsbBadMagic);

    std::vector<u8> payload(sizeof(dbi::QueuePlanHeader));
    const dbi::QueuePlanHeader header{.count = static_cast<u32>(items.size()), .revision = revision};
    std::memcpy(payload.data(), &header, sizeof(header));
    for (const auto& it : items) {
        const dbi::QueuePlanRecord rec{
            .selected = static_cast<u8>(it.selected ? 1 : 0),
            .target = static_cast<u8>(it.target),
            .planned = static_cast<u8>(it.planned_sd ? 1 : 2),
            .flags = static_cast<u8>((it.analysis_ok ? dbi::QueuePlanFlag_AnalysisOk : 0) | (it.already_installed ? dbi::QueuePlanFlag_AlreadyInstalled : 0)),
            .install_size = it.install_size,
            .name_len = static_cast<u32>(it.name.size()),
        };
        const auto off = payload.size();
        payload.resize(off + sizeof(rec) + it.name.size());
        std::memcpy(payload.data() + off, &rec, sizeof(rec));
        std::memcpy(payload.data() + off + sizeof(rec), it.name.data(), it.name.size());
    }

    R_TRY(SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::QueuePlan, static_cast<u32>(payload.size()), timeout));

    dbi::CmdHeader ack{};
    R_TRY(m_usb->TransferAll(true, &ack, sizeof(ack), timeout));
    R_UNLESS(ack.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(ack.id == dbi::CmdId::QueuePlan, Result_UsbBadMagic);
    R_UNLESS(ack.type == dbi::CmdType::Ack, Result_UsbBadMagic);

    R_TRY(m_usb->TransferAll(false, payload.data(), static_cast<u32>(payload.size()), timeout));

    dbi::CmdHeader response{};
    R_TRY(m_usb->TransferAll(true, &response, sizeof(response), timeout));
    R_UNLESS(response.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(response.id == dbi::CmdId::QueuePlan, Result_UsbBadMagic);
    R_UNLESS(response.type == dbi::CmdType::Response, Result_UsbBadMagic);

    R_SUCCEED();
}

Result Usb::SendStorageInfo(u64 nand_free, u64 nand_total, u64 sd_free, u64 sd_total, u64 timeout) {
    R_UNLESS(m_protocol == UsbProtocol::Dbi && m_dbi_selection_sync, Result_UsbBadMagic);

    constexpr u32 payload_size = sizeof(dbi::StorageInfoHeader);

    R_TRY(SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::StorageInfo, payload_size, timeout));

    dbi::CmdHeader ack{};
    R_TRY(m_usb->TransferAll(true, &ack, sizeof(ack), timeout));
    R_UNLESS(ack.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(ack.id == dbi::CmdId::StorageInfo, Result_UsbBadMagic);
    R_UNLESS(ack.type == dbi::CmdType::Ack, Result_UsbBadMagic);

    dbi::StorageInfoHeader header{
        .nand_free = nand_free,
        .nand_total = nand_total,
        .sd_free = sd_free,
        .sd_total = sd_total,
    };
    R_TRY(m_usb->TransferAll(false, &header, sizeof(header), timeout));

    dbi::CmdHeader response{};
    R_TRY(m_usb->TransferAll(true, &response, sizeof(response), timeout));
    R_UNLESS(response.magic == dbi::Magic_Dbi0, Result_UsbBadMagic);
    R_UNLESS(response.id == dbi::CmdId::StorageInfo, Result_UsbBadMagic);
    R_UNLESS(response.type == dbi::CmdType::Response, Result_UsbBadMagic);

    R_SUCCEED();
}


} // namespace sphaira::yati::source

#endif // ENABLE_NETWORK_INSTALL
