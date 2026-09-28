"""Source contract for the empty SPHQ list handshake (runtime needs Switch)."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INTERNAL = ROOT / "sphaira/source/yati/source/usb_internal.hpp"
DBI = ROOT / "sphaira/source/yati/source/usb_dbi.cpp"


def test_empty_sphq_marker_requires_exact_payload():
    header = INTERNAL.read_text(encoding="utf-8")
    source = DBI.read_text(encoding="utf-8")

    assert 'DBI_SPHQ_EMPTY_PAYLOAD = "::SPHQ::\\n"' in header
    assert "std::string_view{names.data(), names.size()} == DBI_SPHQ_EMPTY_PAYLOAD" in source
    assert "if (entry == DBI_SPHQ_EMPTY_MARKER)" in source
    assert "m_dbi_selection_sync = has_sync_data;" in source
    assert "R_UNLESS(!out_names.empty() || m_dbi_selection_sync, Result_UsbBadCount);" in source


if __name__ == "__main__":
    test_empty_sphq_marker_requires_exact_payload()
