// Host test for sphaira/include/cal0_battery.hpp

#include "cal0_battery.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace sphaira::cal0;

namespace {

// not SHA-256: a 32-byte digest that depends on every input byte, enough to see where the hash is written
void FakeHash(std::uint8_t* out, const std::uint8_t* data, std::size_t size) {
    std::uint32_t a = 2166136261u;
    for (std::size_t i = 0; i < HASH_SIZE; i++) {
        out[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }
    for (std::size_t i = 0; i < size; i++) {
        a = (a ^ data[i]) * 16777619u;
        out[i % HASH_SIZE] ^= static_cast<std::uint8_t>(a >> 24);
    }
}

auto MakeImage(std::uint8_t version) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> img(IMAGE_SIZE, 0);
    for (std::size_t i = 0; i < img.size(); i++) {
        img[i] = static_cast<std::uint8_t>(i * 31 + 5);
    }
    // body size 0x4300 bytes from 0x40, cert size 0x100 from 0xAE0
    img[BODY_SIZE_OFF] = 0x00; img[BODY_SIZE_OFF + 1] = 0x43; img[BODY_SIZE_OFF + 2] = 0; img[BODY_SIZE_OFF + 3] = 0;
    img[CERT_SIZE_OFF] = 0x00; img[CERT_SIZE_OFF + 1] = 0x01; img[CERT_SIZE_OFF + 2] = 0; img[CERT_SIZE_OFF + 3] = 0;
    img[BATTERY_OFF] = version;
    const auto crc = Crc16(img.data() + BATTERY_OFF, BATTERY_SIZE - 2);
    img[BATTERY_OFF + BATTERY_SIZE - 2] = static_cast<std::uint8_t>(crc & 0xFF);
    img[BATTERY_OFF + BATTERY_SIZE - 1] = static_cast<std::uint8_t>(crc >> 8);
    FakeHash(img.data() + CERT_HASH_OFF, img.data() + CERT_OFF, 0x100);
    FakeHash(img.data() + BODY_HASH_OFF, img.data() + BODY_OFF, 0x4300);
    return img;
}

} // namespace

int main() {
    // the CRC16 of nothing is the init value; one byte changes it
    const std::uint8_t zero[1]{};
    assert(Crc16(zero, 0) == 0x55AA);
    assert(Crc16(zero, 1) != 0x55AA);

    auto img = MakeImage(0);
    assert(IsUsable(img) && Verify(img, FakeHash));
    assert(BatteryVersion(img) == 0 && BatteryCrcOk(img));

    // the patch: version 2, CRC and the body hash are redone, nothing else moves
    const auto before = img;
    assert(PatchBatteryVersion(img, 2, FakeHash));
    assert(BatteryVersion(img) == 2 && Verify(img, FakeHash));
    for (std::size_t i = 0; i < img.size(); i++) {
        const bool in_block = i >= BATTERY_OFF && i < BATTERY_OFF + BATTERY_SIZE;
        const bool in_hash = i >= BODY_HASH_OFF && i < BODY_HASH_OFF + HASH_SIZE;
        if (!in_block && !in_hash) {
            assert(img[i] == before[i]);
        }
    }
    assert(img[BATTERY_OFF] == 2 && before[BATTERY_OFF] == 0);

    // going back gives the original image byte for byte
    assert(PatchBatteryVersion(img, 0, FakeHash) && img == before);

    // the certificate hash is never rewritten
    assert(std::equal(img.begin() + CERT_HASH_OFF, img.begin() + CERT_HASH_OFF + HASH_SIZE, before.begin() + CERT_HASH_OFF));

    // a damaged image is refused and left as it was
    auto bad = MakeImage(0);
    bad[BODY_OFF + 10] ^= 0xFF; // the body no longer matches its hash
    const auto bad_before = bad;
    assert(!Verify(bad, FakeHash) && !PatchBatteryVersion(bad, 2, FakeHash) && bad == bad_before);
    auto bad_crc = MakeImage(0);
    bad_crc[BATTERY_OFF + 14] ^= 1;
    assert(!BatteryCrcOk(bad_crc) && !PatchBatteryVersion(bad_crc, 2, FakeHash));

    // a wrong size or a size field pointing outside the image is refused
    std::vector<std::uint8_t> small(0x4000);
    assert(!IsUsable(small));
    auto out_of_range = MakeImage(0);
    out_of_range[BODY_SIZE_OFF + 2] = 0xFF;
    assert(!IsUsable(out_of_range) && !PatchBatteryVersion(out_of_range, 2, FakeHash));

    std::puts("test_cal0_battery: ok");
    return 0;
}
