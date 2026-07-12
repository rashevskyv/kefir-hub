#include "web_qr.hpp"
#include <algorithm>
#include <cmath>

namespace sphaira {

auto QrCode::Encode(std::string_view text) -> QrCode {
    QrCode qr;
    qr.DrawFunctionPatterns();
    const auto data = MakeDataCodewords(text);
    if (!data.empty()) {
        qr.DrawCodewords(AddEcc(data));
    }
    qr.DrawFormatBits();
    return qr;
}

auto QrCode::GfMultiply(u8 x, u8 y) -> u8 {
    if (!x || !y) {
        return 0;
    }

    const auto& tables = GetGfTables();
    return tables.exp[tables.log[x] + tables.log[y]];
}

auto QrCode::GetGfTables() -> const GfTables& {
    static const auto tables = []{
        GfTables out{};
        int x = 1;
        for (int i = 0; i < 255; i++) {
            out.exp[i] = static_cast<u8>(x);
            out.log[x] = static_cast<u8>(i);
            x <<= 1;
            if (x & 0x100) {
                x ^= 0x11D;
            }
        }
        for (int i = 255; i < 512; i++) {
            out.exp[i] = out.exp[i - 255];
        }
        return out;
    }();

    return tables;
}

auto QrCode::MakeDataCodewords(std::string_view text) -> std::vector<u8> {
    if (text.size() > 78) {
        return {};
    }

    std::vector<bool> bits;
    bits.reserve(DATA_CODEWORDS * 8);

    const auto append_bits = [&bits](u32 value, int count) {
        for (int i = count - 1; i >= 0; i--) {
            bits.push_back(((value >> i) & 1) != 0);
        }
    };

    append_bits(0x4, 4);
    append_bits(text.size(), 8);
    for (const auto c : text) {
        append_bits(static_cast<unsigned char>(c), 8);
    }

    const auto capacity = DATA_CODEWORDS * 8;
    const auto terminator = std::min<size_t>(4, capacity - bits.size());
    for (size_t i = 0; i < terminator; i++) {
        bits.push_back(false);
    }
    while (bits.size() % 8) {
        bits.push_back(false);
    }

    std::vector<u8> data;
    data.reserve(DATA_CODEWORDS);
    for (size_t i = 0; i < bits.size(); i += 8) {
        u8 value{};
        for (int j = 0; j < 8; j++) {
            value = static_cast<u8>((value << 1) | (bits[i + j] ? 1 : 0));
        }
        data.push_back(value);
    }

    for (u8 pad = 0xEC; data.size() < DATA_CODEWORDS; pad ^= 0xEC ^ 0x11) {
        data.push_back(pad);
    }

    return data;
}

auto QrCode::ReedSolomonDivisor() -> std::array<u8, ECC_CODEWORDS> {
    std::array<u8, ECC_CODEWORDS> result{};
    result[ECC_CODEWORDS - 1] = 1;

    u8 root = 1;
    for (int i = 0; i < ECC_CODEWORDS; i++) {
        for (int j = 0; j < ECC_CODEWORDS; j++) {
            result[j] = GfMultiply(result[j], root);
            if (j + 1 < ECC_CODEWORDS) {
                result[j] ^= result[j + 1];
            }
        }
        root = GfMultiply(root, 0x02);
    }

    return result;
}

auto QrCode::AddEcc(const std::vector<u8>& data) -> std::vector<u8> {
    const auto divisor = ReedSolomonDivisor();
    std::array<u8, ECC_CODEWORDS> remainder{};

    for (const auto b : data) {
        const auto factor = static_cast<u8>(b ^ remainder[0]);
        std::move(remainder.begin() + 1, remainder.end(), remainder.begin());
        remainder.back() = 0;
        for (int i = 0; i < ECC_CODEWORDS; i++) {
            remainder[i] ^= GfMultiply(divisor[i], factor);
        }
    }

    auto out = data;
    out.insert(out.end(), remainder.begin(), remainder.end());
    return out;
}

void QrCode::SetFunction(int x, int y, bool dark) {
    if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) {
        return;
    }

    m_modules[y * SIZE + x] = dark;
    m_is_function[y * SIZE + x] = true;
}

void QrCode::SetModule(int x, int y, bool dark) {
    m_modules[y * SIZE + x] = dark;
}

auto QrCode::IsFunction(int x, int y) const -> bool {
    return m_is_function[y * SIZE + x];
}

void QrCode::DrawFinder(int x, int y) {
    for (int dy = -1; dy <= 7; dy++) {
        for (int dx = -1; dx <= 7; dx++) {
            const auto xx = x + dx;
            const auto yy = y + dy;
            const auto dist = std::max(std::abs(dx - 3), std::abs(dy - 3));
            SetFunction(xx, yy, dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 && dist != 2);
        }
    }
}

void QrCode::DrawAlignment(int x, int y) {
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            SetFunction(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
        }
    }
}

void QrCode::ReserveFormatBits() {
    for (int i = 0; i <= 8; i++) {
        if (i != 6) {
            SetFunction(8, i, false);
            SetFunction(i, 8, false);
        }
    }

    for (int i = 0; i < 8; i++) {
        SetFunction(SIZE - 1 - i, 8, false);
    }
    for (int i = 0; i < 7; i++) {
        SetFunction(8, SIZE - 1 - i, false);
    }

    SetFunction(8, SIZE - 8, true);
}

void QrCode::DrawFunctionPatterns() {
    DrawFinder(0, 0);
    DrawFinder(SIZE - 7, 0);
    DrawFinder(0, SIZE - 7);
    DrawAlignment(26, 26);

    for (int i = 0; i < SIZE; i++) {
        if (!IsFunction(i, 6)) {
            SetFunction(i, 6, i % 2 == 0);
        }
        if (!IsFunction(6, i)) {
            SetFunction(6, i, i % 2 == 0);
        }
    }

    ReserveFormatBits();
}

auto QrCode::Mask(int x, int y) -> bool {
    return ((x + y) & 1) == 0;
}

void QrCode::DrawCodewords(const std::vector<u8>& codewords) {
    size_t bit_index{};
    bool upward = true;

    for (int right = SIZE - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right--;
        }

        for (int vert = 0; vert < SIZE; vert++) {
            const auto y = upward ? SIZE - 1 - vert : vert;
            for (int j = 0; j < 2; j++) {
                const auto x = right - j;
                if (IsFunction(x, y)) {
                    continue;
                }

                bool dark{};
                if (bit_index < codewords.size() * 8) {
                    dark = ((codewords[bit_index >> 3] >> (7 - (bit_index & 7))) & 1) != 0;
                    bit_index++;
                }
                if (Mask(x, y)) {
                    dark = !dark;
                }
                SetModule(x, y, dark);
            }
        }

        upward = !upward;
    }
}

auto QrCode::FormatBits() -> int {
    const int data = (0x1 << 3) | 0x0; // Error level L, mask 0.
    int rem = data << 10;
    for (int i = 14; i >= 10; i--) {
        if ((rem >> i) & 1) {
            rem ^= 0x537 << (i - 10);
        }
    }

    return ((data << 10) | rem) ^ 0x5412;
}

void QrCode::DrawFormatBits() {
    const auto bits = FormatBits();
    const auto bit = [bits](int i) {
        return ((bits >> i) & 1) != 0;
    };

    for (int i = 0; i <= 5; i++) {
        SetFunction(8, i, bit(i));
    }
    SetFunction(8, 7, bit(6));
    SetFunction(8, 8, bit(7));
    SetFunction(7, 8, bit(8));
    for (int i = 9; i < 15; i++) {
        SetFunction(14 - i, 8, bit(i));
    }

    for (int i = 0; i < 8; i++) {
        SetFunction(SIZE - 1 - i, 8, bit(i));
    }
    for (int i = 8; i < 15; i++) {
        SetFunction(8, SIZE - 15 + i, bit(i));
    }

    SetFunction(8, SIZE - 8, true);
}

} // namespace sphaira
