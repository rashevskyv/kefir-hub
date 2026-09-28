"""Guard the console-first DBI probe in USB protocol detection."""

from pathlib import Path


def test_dbi_probe_precedes_response_read():
    root = Path(__file__).resolve().parents[1]
    source = (root / "sphaira/source/yati/source/usb.cpp").read_text(encoding="utf-8")
    detection = source.split("Result Usb::WaitForConnection(", 1)[1].split("// MARK: dbi", 1)[0]

    awoo = detection.index("m_usb->TransferAll(true, &magic, sizeof(magic), DETECT_TIMEOUT)")
    probe = detection.index(
        "SendDbiCmdHeader(dbi::CmdType::Request, dbi::CmdId::List, DBI_LIST_QUEUE_EXT, DETECT_TIMEOUT)"
    )
    reply = detection.index("m_usb->TransferOnce(true, &header, sizeof(header), &transferred, DETECT_TIMEOUT)")
    fallback = detection.index("return GoldleafWaitForConnection(timeout, out_names)")

    assert awoo < probe < reply < fallback


if __name__ == "__main__":
    test_dbi_probe_precedes_response_read()
