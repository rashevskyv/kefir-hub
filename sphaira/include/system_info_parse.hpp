#pragma once

// libnx-free helpers behind Tools → System information: decoding what the
// console hands back as raw bytes (SD card CID, PRODINFO) into rows a person
// can read. Host-tested in tests/test_system_info_parse.cpp.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace sphaira::system_info {

struct SdCid {
    bool valid{};
    std::uint8_t mid{};        // manufacturer id
    char oid[3]{};             // OEM / application id, 2 ascii chars
    char pnm[6]{};             // product name, 5 ascii chars
    std::uint8_t prv_major{};  // product revision n.m
    std::uint8_t prv_minor{};
    std::uint32_t psn{};       // product serial number
    std::uint16_t year{};      // manufacturing date
    std::uint8_t month{};
};

namespace detail {

inline auto IsAsciiPrintable(const std::uint8_t* p, std::size_t n) -> bool {
    for (std::size_t i = 0; i < n; i++) {
        if (p[i] < 0x20 || p[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

// decodes the 16-byte CID register laid out big-endian (bit 127 first), as
// the SD spec draws it. be[15] is the crc byte and is not used.
inline auto DecodeSdCidBe(const std::uint8_t be[16]) -> SdCid {
    SdCid cid{};
    cid.mid = be[0];
    cid.oid[0] = static_cast<char>(be[1]);
    cid.oid[1] = static_cast<char>(be[2]);
    for (int i = 0; i < 5; i++) {
        cid.pnm[i] = static_cast<char>(be[3 + i]);
    }
    cid.prv_major = be[8] >> 4;
    cid.prv_minor = be[8] & 0xF;
    cid.psn = (std::uint32_t(be[9]) << 24) | (std::uint32_t(be[10]) << 16) | (std::uint32_t(be[11]) << 8) | be[12];
    const auto mdt = ((be[13] & 0x0F) << 8) | be[14];
    cid.year = static_cast<std::uint16_t>(2000 + (mdt >> 4));
    cid.month = mdt & 0xF;
    cid.valid = cid.mid != 0 && cid.month >= 1 && cid.month <= 12 && IsAsciiPrintable(be + 3, 5);
    return cid;
}

} // namespace detail

// fs hands the CID back as 16 raw bytes without saying which end is which, and
// the sdmmc driver may or may not keep the crc byte. Three layouts are tried,
// the one that yields a printable product name wins:
//   A: the full 128-bit register, little-endian (byte 15 = manufacturer id);
//   B: the register shifted down by the crc byte, little-endian (byte 15 = 0);
//   C: big-endian, as the spec draws it.
inline auto ParseSdCid(std::span<const std::uint8_t, 16> raw) -> SdCid {
    std::uint8_t be[16]{};

    if (raw[15] == 0) {
        for (int i = 0; i < 15; i++) {
            be[i] = raw[14 - i];
        }
        if (const auto cid = detail::DecodeSdCidBe(be); cid.valid) {
            return cid;
        }
    }

    for (int i = 0; i < 16; i++) {
        be[i] = raw[15 - i];
    }
    if (const auto cid = detail::DecodeSdCidBe(be); cid.valid) {
        return cid;
    }

    for (int i = 0; i < 16; i++) {
        be[i] = raw[i];
    }
    if (const auto cid = detail::DecodeSdCidBe(be); cid.valid) {
        return cid;
    }

    return {};
}

// the registered SD manufacturer ids seen on cards people put in a Switch.
// "" when unknown; the caller shows the raw id then.
inline auto SdManufacturerName(std::uint8_t mid) -> const char* {
    switch (mid) {
        case 0x01: return "Panasonic";
        case 0x02: return "Toshiba / Kioxia";
        case 0x03: return "SanDisk";
        case 0x09: return "ATP";
        case 0x13: return "KingMax";
        case 0x1B: return "Samsung";
        case 0x1D: return "ADATA";
        case 0x27: return "Phison";
        case 0x28: return "Lexar";
        case 0x31: return "Silicon Power";
        case 0x41: return "Kingston";
        case 0x6F: return "STMicroelectronics";
        case 0x74: return "Transcend";
        case 0x76: return "Patriot";
        case 0x82: return "Sony / Gobe";
        case 0x9C: return "Angelbird / Hoodman";
        case 0x9F: return "Kingston";
        default: return "";
    }
}

inline auto TrimSerial(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) {
        s.remove_suffix(1);
    }
    while (!s.empty() && s.front() == ' ') {
        s.remove_prefix(1);
    }
    return s;
}

// a serial the console really has, as opposed to what a blanked PRODINFO
// (exosphere blank_prodinfo, Incognito: "XAW00000000000"), zeros or garbage
// give back: letters and digits only, and at least one digit that is not 0.
inline auto IsRealSerial(std::string_view s) -> bool {
    s = TrimSerial(s);
    if (s.size() < 8 || s.size() > 24) {
        return false;
    }
    bool nonzero_digit = false;
    for (const unsigned char c : s) {
        const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (!alpha && !digit) {
            return false;
        }
        nonzero_digit |= digit && c != '0';
    }
    return nonzero_digit;
}

// PRODINFO (CAL0) keeps the serial at 0x250, up to 0x18 bytes, nul padded.
inline constexpr std::size_t PRODINFO_SERIAL_OFFSET = 0x250;
inline constexpr std::size_t PRODINFO_SERIAL_SIZE = 0x18;
inline constexpr std::size_t PRODINFO_MIN_READ = PRODINFO_SERIAL_OFFSET + PRODINFO_SERIAL_SIZE;

// the serial out of a PRODINFO image (partition or backup file), or "" when
// the data is not a CAL0 image or its serial is not a real one.
inline auto SerialFromProdinfo(std::span<const std::uint8_t> data) -> std::string {
    if (data.size() < PRODINFO_MIN_READ || std::memcmp(data.data(), "CAL0", 4) != 0) {
        return {};
    }
    const auto* p = reinterpret_cast<const char*>(data.data() + PRODINFO_SERIAL_OFFSET);
    const std::string_view raw{p, strnlen(p, PRODINFO_SERIAL_SIZE)};
    const auto s = TrimSerial(raw);
    return IsRealSerial(s) ? std::string{s} : std::string{};
}

} // namespace sphaira::system_info
