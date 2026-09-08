#include "ui/menus/settings/translation_policy.hpp"

#include <cstdio>
#include <string>

using namespace sphaira::ui::menu::settings;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_fw_16() {
    // lower bound and upper bound of 16.0.0-16.1.0
    auto r1 = ResolveFirmwareCompatibility("16.0.0");
    CHECK(r1.available);
    CHECK(r1.target_tag == "FW16.1.0-TR1.09");
    CHECK(r1.metadata_tag == "FW17.0.1-TR1.18");
    CHECK(!r1.warning_required);

    auto r2 = ResolveFirmwareCompatibility("16.1.0");
    CHECK(r2.available);
    CHECK(r2.target_tag == "FW16.1.0-TR1.09");
    CHECK(r2.metadata_tag == "FW17.0.1-TR1.18");
    CHECK(!r2.warning_required);

    return 0;
}

static int test_fw_17_0_0() {
    // 17.0.0
    auto r = ResolveFirmwareCompatibility("17.0.0");
    CHECK(r.available);
    CHECK(r.target_tag == "FW17.0.0-TR1.11");
    CHECK(r.metadata_tag == "FW17.0.1-TR1.18");
    CHECK(!r.warning_required);
    return 0;
}

static int test_fw_17_0_1() {
    // 17.0.1 selecting TR1.18
    auto r = ResolveFirmwareCompatibility("17.0.1");
    CHECK(r.available);
    CHECK(r.target_tag == "FW17.0.1-TR1.18");
    CHECK(r.metadata_tag == "FW17.0.1-TR1.18");
    CHECK(!r.warning_required);
    return 0;
}

static int test_fw_18() {
    // lower and upper bounds of 18.0.0-18.1.0
    auto r1 = ResolveFirmwareCompatibility("18.0.0");
    CHECK(r1.available);
    CHECK(r1.target_tag == "FW18.1.0-TR1.20");
    CHECK(r1.metadata_tag == "FW18.1.0-TR1.20");
    CHECK(!r1.warning_required);

    auto r2 = ResolveFirmwareCompatibility("18.1.0");
    CHECK(r2.available);
    CHECK(r2.target_tag == "FW18.1.0-TR1.20");
    CHECK(r2.metadata_tag == "FW18.1.0-TR1.20");
    CHECK(!r2.warning_required);

    return 0;
}

static int test_fw_19() {
    // normal 19.0.2 versus Chinese 19.0.2 unavailable
    auto r_normal = ResolveFirmwareCompatibility("19.0.2", false);
    CHECK(r_normal.available);
    CHECK(r_normal.target_tag == "FW19.0.0-TR1.21");
    CHECK(r_normal.metadata_tag == "FW19.0.0-TR1.21");
    CHECK(!r_normal.warning_required);

    auto r_china = ResolveFirmwareCompatibility("19.0.2", true);
    CHECK(!r_china.available);
    CHECK(r_china.target_tag.empty());
    CHECK(r_china.metadata_tag.empty());

    return 0;
}

static int test_fw_20() {
    // lower and upper bounds of 20.0.0-20.5.0
    auto r1 = ResolveFirmwareCompatibility("20.0.0");
    CHECK(r1.available);
    CHECK(r1.target_tag == "FW20.4.0-TR2.00");
    CHECK(r1.metadata_tag == "FW20.4.0-TR2.00");
    CHECK(!r1.warning_required);

    auto r2 = ResolveFirmwareCompatibility("20.5.0");
    CHECK(r2.available);
    CHECK(r2.target_tag == "FW20.4.0-TR2.00");
    CHECK(r2.metadata_tag == "FW20.4.0-TR2.00");
    // warning false at 20.5.0
    CHECK(!r2.warning_required);

    return 0;
}

static int test_fw_21_22_fallback() {
    // warning true at 21.0.0 and 22.5.0
    auto r1 = ResolveFirmwareCompatibility("21.0.0");
    CHECK(r1.available);
    CHECK(r1.target_tag == "FW20.4.0-TR2.00");
    CHECK(r1.metadata_tag == "FW20.4.0-TR2.00");
    CHECK(r1.warning_required);

    auto r2 = ResolveFirmwareCompatibility("22.5.0");
    CHECK(r2.available);
    CHECK(r2.target_tag == "FW20.4.0-TR2.00");
    CHECK(r2.metadata_tag == "FW20.4.0-TR2.00");
    CHECK(r2.warning_required);

    return 0;
}

static int test_fw_unavailable() {
    // unavailable below 16.0.0 and above 22.5.0
    auto r_low = ResolveFirmwareCompatibility("1.0.0");
    CHECK(!r_low.available);
    CHECK(r_low.target_tag.empty());
    CHECK(r_low.metadata_tag.empty());

    auto r_15 = ResolveFirmwareCompatibility("15.0.1");
    CHECK(!r_15.available);
    CHECK(r_15.target_tag.empty());
    CHECK(r_15.metadata_tag.empty());

    auto r_high = ResolveFirmwareCompatibility("22.5.1");
    CHECK(!r_high.available);
    CHECK(r_high.target_tag.empty());
    CHECK(r_high.metadata_tag.empty());

    auto r_23 = ResolveFirmwareCompatibility("23.0.0");
    CHECK(!r_23.available);
    CHECK(r_23.target_tag.empty());
    CHECK(r_23.metadata_tag.empty());

    auto r_empty = ResolveFirmwareCompatibility("");
    CHECK(!r_empty.available);
    CHECK(r_empty.target_tag.empty());
    CHECK(r_empty.metadata_tag.empty());

    auto r_invalid = ResolveFirmwareCompatibility("invalid");
    CHECK(!r_invalid.available);
    CHECK(r_invalid.target_tag.empty());
    CHECK(r_invalid.metadata_tag.empty());

    return 0;
}

static int test_extraction_folder() {
    // legacy archives: strip TR..._ prefix
    CHECK(TranslationExtractFolder("TR1.09_ukrainian_FW16.1.0.zip") == "ukrainian_FW16.1.0");
    CHECK(TranslationExtractFolder("TR1.11_ukrainian_FW17.0.0.zip") == "ukrainian_FW17.0.0");
    CHECK(TranslationExtractFolder("tr1.09_german_FW16.1.0.zip") == "german_FW16.1.0");
    CHECK(TranslationExtractFolder("TR1.11_french_FW17.0.0.zip") == "french_FW17.0.0");

    // modern archives: NX- -> Nx-
    CHECK(TranslationExtractFolder("NX-Translation_ukrainian.zip") == "Nx-Translation_ukrainian");
    CHECK(TranslationExtractFolder("Nx-Translation_ukrainian.zip") == "Nx-Translation_ukrainian");
    CHECK(TranslationExtractFolder("NX-Translation_japanese.zip") == "Nx-Translation_japanese");

    return 0;
}

int main() {
    if (test_fw_16() != 0) return 1;
    if (test_fw_17_0_0() != 0) return 1;
    if (test_fw_17_0_1() != 0) return 1;
    if (test_fw_18() != 0) return 1;
    if (test_fw_19() != 0) return 1;
    if (test_fw_20() != 0) return 1;
    if (test_fw_21_22_fallback() != 0) return 1;
    if (test_fw_unavailable() != 0) return 1;
    if (test_extraction_folder() != 0) return 1;

    std::printf("ok  translation_policy: %d checks passed\n", g_checks);
    return 0;
}
