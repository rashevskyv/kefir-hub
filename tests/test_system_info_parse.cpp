// Tools → System information: the pure decoders behind the SD card and serial rows.
#include "system_info_parse.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sphaira::system_info;

namespace {

// one SanDisk card as the spec draws its CID (big-endian): MID 0x03, OID "SD",
// PNM "SL32G", PRV 8.0, PSN 0x16DC6D12, MDT 2015-02, crc.
constexpr std::uint8_t CID_BE[16] = {
    0x03, 0x53, 0x44, 0x53, 0x4C, 0x33, 0x32, 0x47, 0x80, 0x16, 0xDC, 0x6D, 0x12, 0x00, 0xF2, 0xD1,
};

void CheckCard(const SdCid& cid) {
    assert(cid.valid);
    assert(cid.mid == 0x03);
    assert(std::strcmp(cid.oid, "SD") == 0);
    assert(std::strcmp(cid.pnm, "SL32G") == 0);
    assert(cid.prv_major == 8 && cid.prv_minor == 0);
    assert(cid.psn == 0x16DC6D12);
    assert(cid.year == 2015 && cid.month == 2);
    assert(std::strcmp(SdManufacturerName(cid.mid), "SanDisk") == 0);
}

void TestCidLayouts() {
    // C: big-endian as given.
    CheckCard(ParseSdCid(std::span<const std::uint8_t, 16>{CID_BE}));

    // A: the whole register little-endian.
    std::uint8_t le[16];
    for (int i = 0; i < 16; i++) {
        le[i] = CID_BE[15 - i];
    }
    CheckCard(ParseSdCid(std::span<const std::uint8_t, 16>{le}));

    // B: crc byte dropped, little-endian, top byte zero.
    std::uint8_t shifted[16]{};
    for (int i = 0; i < 15; i++) {
        shifted[i] = CID_BE[14 - i];
    }
    CheckCard(ParseSdCid(std::span<const std::uint8_t, 16>{shifted}));

    // garbage: nothing printable in any orientation.
    const std::uint8_t junk[16] = {0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
    assert(!ParseSdCid(std::span<const std::uint8_t, 16>{junk}).valid);

    assert(std::strcmp(SdManufacturerName(0x1B), "Samsung") == 0);
    assert(SdManufacturerName(0xEE)[0] == '\0');
}

void TestSerial() {
    assert(IsRealSerial("XAW10012345678"));
    assert(IsRealSerial("XAJ70098765432\0\0\0"));
    assert(IsRealSerial(std::string_view{"XKW40011122233\0\0\0\0", 18}));
    // what blanking and Incognito leave behind.
    assert(!IsRealSerial("XAW00000000000"));
    assert(!IsRealSerial("00000000000000"));
    assert(!IsRealSerial(""));
    assert(!IsRealSerial(std::string(14, '\0')));
    // the settings struct read as text when nothing is set.
    assert(!IsRealSerial("XAW 0000000000"));
    assert(!IsRealSerial("short"));
}

void TestProdinfoSerial() {
    std::vector<std::uint8_t> cal0(0x4000, 0);
    std::memcpy(cal0.data(), "CAL0", 4);
    std::memcpy(cal0.data() + PRODINFO_SERIAL_OFFSET, "XAW10012345678", 14);
    assert(SerialFromProdinfo(cal0) == "XAW10012345678");

    // only the head of the file was read: still enough.
    assert(SerialFromProdinfo(std::span<const std::uint8_t>{cal0.data(), PRODINFO_MIN_READ}) == "XAW10012345678");
    assert(SerialFromProdinfo(std::span<const std::uint8_t>{cal0.data(), PRODINFO_MIN_READ - 1}).empty());

    // a blanked backup names no serial.
    std::memcpy(cal0.data() + PRODINFO_SERIAL_OFFSET, "XAW00000000000", 14);
    assert(SerialFromProdinfo(cal0).empty());

    // not a CAL0 image at all (a BOOT0 dump, a zip).
    std::memcpy(cal0.data(), "PK\x03\x04", 4);
    std::memcpy(cal0.data() + PRODINFO_SERIAL_OFFSET, "XAW10012345678", 14);
    assert(SerialFromProdinfo(cal0).empty());
}

} // namespace

int main() {
    TestCidLayouts();
    TestSerial();
    TestProdinfoSerial();
    std::puts("test_system_info_parse: ok");
    return 0;
}
