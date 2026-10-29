// Host test for sphaira/include/battery_init.hpp

#include "battery_init.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery;

int main() {
    // table choice: BDF main.asm 0x1354..0x1390; .141 is version 2, lot "BHDHHZZA..." -> 'A'
    assert(&ChooseTable(2, 'A') == &TABLE_2A && TABLE_2A[2] == 0x1C20); // 3600 mAh
    assert(&ChooseTable(2, 'M') == &TABLE_2M);
    assert(&ChooseTable(2, 'R') == &TABLE_2R);
    assert(&ChooseTable(1, 'M') == &TABLE_1);
    assert(&ChooseTable(0, 'X') == &TABLE_0A && TABLE_0A[2] == 0x2476);
    assert(&ChooseTable(0, 'R') == &TABLE_0R);

    // u32 maths: 0x1C20 * 0x504D / 25600 = 7200 * 20557 / 25600
    assert(RemCap(0x1C20, 0x504D) == 5781);

    const auto steps = FactoryInitSteps();
    assert(steps.size() == 11 + 48 + 35);
    assert(steps[0].op == Op::Read && steps[0].reg == 0x2B);
    assert(steps[1].reg == 0x2B && steps[1].src == Src::MiscOr15);
    assert(steps[11].reg == 0x80 && steps[11].op == Op::Verify && steps[58].reg == 0xAF);
    assert(steps[59].reg == 0x62 && steps[59].value == 0 && steps[60].reg == 0x63);

    std::array<std::uint16_t, 4> slots{0x0870, 0x504D, 0x5050, 0x9F5F}; // V_empty: the calibration's zero at 3.18 V
    int model = 0;
    std::uint16_t remcap = 0, status = 0xFFFF, cycles = 0, v_empty = 0;
    for (const auto& st : steps) {
        if (st.op != Op::Write && st.op != Op::Verify) {
            continue;
        }
        const auto v = StepValue(st, TABLE_2A, slots);
        if (st.reg >= 0x80 && st.reg <= 0xAF) {
            assert(v == TABLE_2A[9 + (st.reg - 0x80)]);
            model++;
        }
        if (st.reg == 0x2B) {
            assert(v == 0x8870);
        }
        if (st.reg == 0x0F) {
            remcap = v;
        }
        if (st.reg == 0x00) {
            status = v;
        }
        if (st.reg == 0x17) {
            cycles = v;
        }
        if (st.reg == 0x45) {
            assert(v == 0x1C20 >> 4);
        }
        if (st.reg == 0x4C) {
            assert(v == 0x5050);
        }
        if (st.reg == 0x3A) {
            v_empty = v;
        }
    }
    assert(model == 48 && remcap == 5781 && status == 0 && cycles == 0x0060);
    assert(v_empty == 0x9F5F); // the reset writes the zero it was given, not the factory 0xA05F

    std::puts("test_battery_init: ok");
}
