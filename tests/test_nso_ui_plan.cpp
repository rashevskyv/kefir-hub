// NSO libnx-layer decisions (include/nso/ui_plan.hpp): extension filter, N64 warning, cover fit, remove cleanup.
#include "nso_test_util.hpp"
#include "nso/ui_plan.hpp"

using namespace nso;

int main() {
    Installed none{};
    Installed nes{};
    nes[size_t(System::Nes)] = true;
    Installed gen{};
    gen[size_t(System::Genesis)] = true;

    CHECK(!AnyInstalled(none));
    CHECK(AnyInstalled(nes));
    CHECK(!CanTakeExt("nes", none));
    CHECK(CanTakeExt("nes", nes));
    CHECK(CanTakeExt(".NES", nes));
    CHECK(!CanTakeExt("sfc", nes));
    CHECK(!CanTakeExt("txt", nes));
    CHECK(!CanTakeExt("", nes));
    CHECK(CanTakeExt("bin", gen));
    CHECK(!CanTakeExt("bin", none));
    CHECK(CanTakeExt("md", gen));

    // N64: unknown game -> warn; non-N64 data -> no warning (the planner reports that error)
    Bytes z64(0x1000, 0);
    z64[0] = 0x80; z64[1] = 0x37; z64[2] = 0x12; z64[3] = 0x40;
    CHECK(N64NeedsWarning(z64));
    z64[0x10] = 1;  // crc1 != 0, still not in the default table
    CHECK(N64NeedsWarning(z64));
    static const u32 idle[1] = {0};
    const N64Config row[] = {{0, 0, "SRAM", "None", "", 0, nullptr, 0}, {0x01000000u, 0, "SRAM", "None", "", 0, idle, 0}};
    CHECK(!N64NeedsWarning(z64, row));
    CHECK(!N64NeedsWarning(Bytes(0x1000, 0x41)));

    // cover fit: longer side fills the target, aspect kept, centered
    Rect r = FitInside(200, 100, 400, 300);
    CHECK_EQ(r.w, 400); CHECK_EQ(r.h, 200); CHECK_EQ(r.x, 0); CHECK_EQ(r.y, 50);
    r = FitInside(100, 200, 512, 512);
    CHECK_EQ(r.w, 256); CHECK_EQ(r.h, 512); CHECK_EQ(r.x, 128); CHECK_EQ(r.y, 0);
    r = FitInside(1000, 1, 768, 224);
    CHECK_EQ(r.w, 768); CHECK_EQ(r.h, 1);
    r = FitInside(0, 5, 10, 10);
    CHECK_EQ(r.w, 0);

    // remove cleanup
    CHECK(!CleanupAfterRemove(false, true).db);
    CHECK(CleanupAfterRemove(true, true).db);
    CHECK(CleanupAfterRemove(true, true).flag);
    CHECK(CleanupAfterRemove(true, false).marker);
    CHECK(!CleanupAfterRemove(true, false).db);

    CHECK_EQ(DirPart("titles/S-3123_e/3123_e.sfrom"), "titles/S-3123_e");
    CHECK_EQ(DirPart("x"), "");
    CHECK_EQ(JoinPath("/roms/nes", "a.nes"), "/roms/nes/a.nes");
    CHECK_EQ(JoinPath("/roms/nes/", "a.nes"), "/roms/nes/a.nes");
    CHECK_EQ(FindSfromsig({"a.txt", "titles/S-1/1.sfromsig", "titles/S-2/2.sfromsig"}), "titles/S-1/1.sfromsig");
    CHECK_EQ(FindSfromsig({"a.txt"}), "");

    CHECK_EQ(EntryTitle("{\"sort_title\":\"x\",\"title\":\"A \\\"B\\\" c\",\"z\":1}"), "A \"B\" c");
    CHECK_EQ(EntryTitle("{}"), "");

    if (g_fail) return 1;
    std::printf("test_nso_ui_plan: ok\n");
    return 0;
}
