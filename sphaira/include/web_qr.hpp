#pragma once

#include <string_view>
#include <vector>
#include <array>
#include <switch.h>

namespace sphaira {

class QrCode final {
public:
    static constexpr int VERSION = 4;
    static constexpr int SIZE = VERSION * 4 + 17;
    static constexpr int DATA_CODEWORDS = 80;
    static constexpr int ECC_CODEWORDS = 20;

    static auto Encode(std::string_view text) -> QrCode;

    auto Get(int x, int y) const -> bool {
        return m_modules[y * SIZE + x];
    }

private:
    static auto GfMultiply(u8 x, u8 y) -> u8;

    struct GfTables {
        std::array<u8, 512> exp{};
        std::array<u8, 256> log{};
    };

    static auto GetGfTables() -> const GfTables&;
    static auto MakeDataCodewords(std::string_view text) -> std::vector<u8>;
    static auto ReedSolomonDivisor() -> std::array<u8, ECC_CODEWORDS>;
    static auto AddEcc(const std::vector<u8>& data) -> std::vector<u8>;

    void SetFunction(int x, int y, bool dark);
    void SetModule(int x, int y, bool dark);
    auto IsFunction(int x, int y) const -> bool;

    void DrawFinder(int x, int y);
    void DrawAlignment(int x, int y);
    void ReserveFormatBits();
    void DrawFunctionPatterns();

    static auto Mask(int x, int y) -> bool;

    void DrawCodewords(const std::vector<u8>& codewords);
    static auto FormatBits() -> int;
    void DrawFormatBits();

private:
    std::array<bool, SIZE * SIZE> m_modules{};
    std::array<bool, SIZE * SIZE> m_is_function{};
};

} // namespace sphaira
