#pragma once

// The battery-version field of PRODINFO (CAL0): where it is, how its CRC16 and the body hash are made, and the patch.
// Layout as used by the community BatteryVersionEditor (lulle2007200, V0.1) and read back from Atmosphère's own checks;
// written fresh here. No libnx: the hash function is passed in, so the host test can run it.
//
//   0x0008  u32  size of the calibration body
//   0x0020  32 B SHA-256 of the body [0x40, 0x40 + size)
//   0x4310  16 B battery version block: byte 0 = version (0 LiPo OLED/v1, 1, 2 = Lite = LiHV 4.32 V),
//                bytes 14..15 = CRC16 of bytes 0..13 (init 0x55AA, the 16-entry nibble table below)
//   0x0AE0  client certificate, its size at 0x0AD0 and its SHA-256 at 0x12E0: only checked, never rewritten

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace sphaira::cal0 {

inline constexpr std::size_t IMAGE_SIZE = 0x8000;
inline constexpr std::size_t BODY_SIZE_OFF = 0x08;
inline constexpr std::size_t BODY_HASH_OFF = 0x20;
inline constexpr std::size_t BODY_OFF = 0x40;
inline constexpr std::size_t CERT_SIZE_OFF = 0x0AD0;
inline constexpr std::size_t CERT_OFF = 0x0AE0;
inline constexpr std::size_t CERT_HASH_OFF = 0x12E0;
inline constexpr std::size_t BATTERY_OFF = 0x4310;
inline constexpr std::size_t BATTERY_SIZE = 0x10;
inline constexpr std::size_t HASH_SIZE = 0x20;

// out = SHA-256 of [data, data + size)
using HashFn = std::function<void(std::uint8_t* out, const std::uint8_t* data, std::size_t size)>;

inline auto Crc16(const std::uint8_t* data, std::size_t size) -> std::uint16_t {
    static constexpr std::uint16_t table[16] = {
        0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400};
    std::uint16_t crc = 0x55AA;
    for (std::size_t i = 0; i < size; i++) {
        crc = static_cast<std::uint16_t>((crc >> 4) ^ table[crc & 0xF] ^ table[(data[i] >> 0) & 0xF]);
        crc = static_cast<std::uint16_t>((crc >> 4) ^ table[crc & 0xF] ^ table[(data[i] >> 4) & 0xF]);
    }
    return crc;
}

inline auto ReadU32(const std::vector<std::uint8_t>& img, std::size_t off) -> std::uint32_t {
    return static_cast<std::uint32_t>(img[off]) | (static_cast<std::uint32_t>(img[off + 1]) << 8) |
           (static_cast<std::uint32_t>(img[off + 2]) << 16) | (static_cast<std::uint32_t>(img[off + 3]) << 24);
}

// a whole image of the right size whose body and certificate ranges lie inside it
inline auto IsUsable(const std::vector<std::uint8_t>& img) -> bool {
    if (img.size() != IMAGE_SIZE) {
        return false;
    }
    const auto body = ReadU32(img, BODY_SIZE_OFF);
    const auto cert = ReadU32(img, CERT_SIZE_OFF);
    return BODY_OFF + static_cast<std::size_t>(body) <= img.size() && CERT_OFF + static_cast<std::size_t>(cert) <= img.size();
}

inline auto BatteryVersion(const std::vector<std::uint8_t>& img) -> int {
    return img.size() > BATTERY_OFF ? img[BATTERY_OFF] : -1;
}

inline auto BatteryCrcOk(const std::vector<std::uint8_t>& img) -> bool {
    const auto* b = img.data() + BATTERY_OFF;
    const auto crc = Crc16(b, BATTERY_SIZE - 2);
    return b[BATTERY_SIZE - 2] == (crc & 0xFF) && b[BATTERY_SIZE - 1] == (crc >> 8);
}

inline auto BodyHashOk(const std::vector<std::uint8_t>& img, const HashFn& hash) -> bool {
    std::uint8_t h[HASH_SIZE]{};
    hash(h, img.data() + BODY_OFF, ReadU32(img, BODY_SIZE_OFF));
    for (std::size_t i = 0; i < HASH_SIZE; i++) {
        if (h[i] != img[BODY_HASH_OFF + i]) {
            return false;
        }
    }
    return true;
}

inline auto CertHashOk(const std::vector<std::uint8_t>& img, const HashFn& hash) -> bool {
    std::uint8_t h[HASH_SIZE]{};
    hash(h, img.data() + CERT_OFF, ReadU32(img, CERT_SIZE_OFF));
    for (std::size_t i = 0; i < HASH_SIZE; i++) {
        if (h[i] != img[CERT_HASH_OFF + i]) {
            return false;
        }
    }
    return true;
}

// everything the editor checks: certificate hash, body hash and the battery block CRC
inline auto Verify(const std::vector<std::uint8_t>& img, const HashFn& hash) -> bool {
    return IsUsable(img) && CertHashOk(img, hash) && BodyHashOk(img, hash) && BatteryCrcOk(img);
}

// Sets the battery version, then the block CRC and the body hash. Only these two ranges change:
// [BATTERY_OFF, +BATTERY_SIZE) and [BODY_HASH_OFF, +HASH_SIZE). false when the image is not usable or was not valid before
// (an image that fails its own checks is not touched: the edit would hide the damage).
inline auto PatchBatteryVersion(std::vector<std::uint8_t>& img, std::uint8_t version, const HashFn& hash) -> bool {
    if (!Verify(img, hash)) {
        return false;
    }
    img[BATTERY_OFF] = version;
    const auto crc = Crc16(img.data() + BATTERY_OFF, BATTERY_SIZE - 2);
    img[BATTERY_OFF + BATTERY_SIZE - 2] = static_cast<std::uint8_t>(crc & 0xFF);
    img[BATTERY_OFF + BATTERY_SIZE - 1] = static_cast<std::uint8_t>(crc >> 8);
    hash(img.data() + BODY_HASH_OFF, img.data() + BODY_OFF, ReadU32(img, BODY_SIZE_OFF));
    return true;
}

} // namespace sphaira::cal0
