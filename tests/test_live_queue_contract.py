"""Source contract for the end-to-end live USB queue in Sphaira."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INTERNAL = ROOT / "sphaira/source/yati/source/usb_internal.hpp"
USB_HPP = ROOT / "sphaira/include/yati/source/usb.hpp"
USB_DBI = ROOT / "sphaira/source/yati/source/usb_dbi.cpp"
DBI_MENU_HPP = ROOT / "sphaira/include/ui/menus/dbi_menu.hpp"
DBI_PLAN = ROOT / "sphaira/source/ui/menus/dbi/dbi_plan.cpp"
DBI_USB = ROOT / "sphaira/source/ui/menus/dbi/dbi_usb.cpp"


def test_live_queue_protocol_constants_and_types():
    internal_txt = INTERNAL.read_text(encoding="utf-8")
    usb_hpp_txt = USB_HPP.read_text(encoding="utf-8")

    assert 'DBI_SPHQ_REV_PREFIX = "::SPHQ_REV::|"' in internal_txt
    assert "struct LiveQueueItem" in usb_hpp_txt
    assert "std::string name;" in usb_hpp_txt
    assert "s64 size{0};" in usb_hpp_txt
    assert "bool selected{false};" in usb_hpp_txt
    assert "int target{0};" in usb_hpp_txt
    assert "FetchLiveQueue(std::vector<LiveQueueItem>& out_items, u32& out_revision" in usb_hpp_txt
    assert "SendQueueAck(u32 revision" in usb_hpp_txt


def test_live_queue_transport_parsing_and_ack():
    source = USB_DBI.read_text(encoding="utf-8")

    # FetchLiveQueue parses revision and entries
    assert "if (entry.starts_with(DBI_SPHQ_REV_PREFIX))" in source
    assert "out_revision = std::strtoul(entry.c_str() + DBI_SPHQ_REV_PREFIX.size(), nullptr, 10);" in source
    assert "out_items.push_back({" in source

    # SendQueueAck sends CmdType::Ack for CmdId::List
    assert "Result Usb::SendQueueAck(u32 revision, u64 timeout)" in source
    assert "SendDbiCmdHeader(dbi::CmdType::Ack, dbi::CmdId::List, revision, timeout)" in source

    # DbiWaitForConnection parses rev prefix and supports empty queue with revision
    assert "entry.starts_with(DBI_SPHQ_REV_PREFIX)" in source
    assert "if (has_rev_header && has_empty_marker && out_names.empty()) {" in source
    assert "has_sync_data = true;" in source


def test_live_queue_plan_reconciliation_and_immutability():
    menu_hpp = DBI_MENU_HPP.read_text(encoding="utf-8")
    plan_cpp = DBI_PLAN.read_text(encoding="utf-8")

    assert "ApplyLiveQueue(const std::vector<yati::source::Usb::LiveQueueItem>& items, size_t active_index = 0, bool is_installing = false);" in menu_hpp
    assert "u32 m_last_acked_revision" in menu_hpp

    # ReviewQueue mode (!is_installing): does NOT clear queue before analysis; applies atomically
    assert "if (!is_installing)" in plan_cpp
    assert "existing_map" in plan_cpp
    # Verify m_queue.clear() is NOT in review section before analysis
    review_section = plan_cpp.split("if (!is_installing)", 1)[1].split("new_queue.reserve", 1)[0]
    assert "m_queue.clear()" not in review_section
    assert "m_queue = std::move(new_queue);" in plan_cpp

    # Installing mode (is_installing): preserves prefix 0..active_index as immutable; does not truncate early
    install_setup = plan_cpp.split("// Installing mode", 1)[1].split("std::vector<QueueEntry> new_future;", 1)[0]
    assert "m_queue.resize" not in install_setup
    assert "for (size_t k = 0; k <= active_index; k++)" in plan_cpp
    assert "frozen_names.insert(m_queue[k].file_name);" in plan_cpp
    assert "for (size_t k = active_index + 1; k < m_queue.size(); k++)" in plan_cpp


def test_live_queue_dbi_usb_delegation_and_size_limit():
    usb_cpp = DBI_USB.read_text(encoding="utf-8")

    # Verifies reset of acked revision upon connection
    assert "m_last_acked_revision = 0;" in usb_cpp

    # Verifies both ReviewQueue and Installing loops use FetchLiveQueue, ApplyLiveQueue, and SendQueueAck
    assert "m_usb_source->FetchLiveQueue(items, revision)" in usb_cpp
    assert "ApplyLiveQueue(items, 0, false)" in usb_cpp
    assert "ApplyLiveQueue(items, i, true)" in usb_cpp
    assert "m_usb_source->SendQueueAck(revision)" in usb_cpp

    # ACK state is only updated on successful send (SendQueueAck checked with R_SUCCEEDED)
    assert "if (R_SUCCEEDED(m_usb_source->SendQueueAck(revision)))" in usb_cpp

    # Ensure file size constraint (<600 lines) is respected
    line_count = len(usb_cpp.splitlines())
    assert line_count < 600, f"dbi_usb.cpp has {line_count} lines, which exceeds the 600 line limit!"


if __name__ == "__main__":
    test_live_queue_protocol_constants_and_types()
    test_live_queue_transport_parsing_and_ack()
    test_live_queue_plan_reconciliation_and_immutability()
    test_live_queue_dbi_usb_delegation_and_size_limit()
    print("All live queue contracts verified successfully.")
