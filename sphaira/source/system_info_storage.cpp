// Tools → System information: the Storage and Play activity groups.
#include "system_info_internal.hpp"
#include "system_info_parse.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <switch.h>
#include <span>

namespace sphaira::system_info {
namespace {

using ui::menu::grid::FormatBytes;

auto SpeedModeName(s64 mode) -> std::string {
    static constexpr const char* names[] = {"Identification", "Default speed", "High speed", "SDR12", "SDR25", "SDR50", "SDR104", "DDR50"};
    return mode >= 0 && static_cast<size_t>(mode) < std::size(names) ? names[mode] : Fmt("%ld", mode);
}

auto UsedOfTotal(s64 free, s64 total) -> std::string {
    return FormatBytes(static_cast<u64>(total - free)) + " / " + FormatBytes(static_cast<u64>(total));
}

} // namespace

auto CollectStorage() -> Group {
    GroupBuilder g{"Storage"_i18n};

    s64 nand_free{}, nand_total{}, sd_free{}, sd_total{};
    fs::GetStorageSpaces(&nand_free, &nand_total, &sd_free, &sd_total);
    if (nand_total) {
        g.Line("System memory used"_i18n, UsedOfTotal(nand_free, nand_total));
        g.Line("System memory free"_i18n, FormatBytes(static_cast<u64>(nand_free)));
    }

    FsDeviceOperator op{};
    if (R_FAILED(fsOpenDeviceOperator(&op))) {
        return g.group;
    }
    ON_SCOPE_EXIT(fsDeviceOperatorClose(&op));

    bool inserted{};
    if (R_FAILED(fsDeviceOperatorIsSdCardInserted(&op, &inserted)) || !inserted) {
        g.Line("microSD card"_i18n, "Not inserted"_i18n);
        return g.group;
    }
    if (sd_total) {
        g.Line("microSD card used"_i18n, UsedOfTotal(sd_free, sd_total));
        g.Line("microSD card free"_i18n, FormatBytes(static_cast<u64>(sd_free)));
    }
    s64 v{};
    if (R_SUCCEEDED(fsDeviceOperatorGetSdCardSpeedMode(&op, &v))) {
        g.Line("microSD card speed mode"_i18n, SpeedModeName(v));
    }
    if (R_SUCCEEDED(fsDeviceOperatorGetSdCardUserAreaSize(&op, &v))) {
        g.Line("microSD card user area"_i18n, FormatBytes(static_cast<u64>(v)));
    }
    if (R_SUCCEEDED(fsDeviceOperatorGetSdCardProtectedAreaSize(&op, &v))) {
        g.Line("microSD card protected area"_i18n, FormatBytes(static_cast<u64>(v)));
    }

    u8 raw[16]{};
    if (R_SUCCEEDED(fsDeviceOperatorGetSdCardCid(&op, raw, sizeof(raw), sizeof(raw)))) {
        const auto cid = ParseSdCid(std::span<const u8, 16>{raw});
        if (cid.valid) {
            const auto* maker = SdManufacturerName(cid.mid);
            g.Line("microSD card maker"_i18n, *maker ? Fmt("%s (0x%02X)", maker, cid.mid) : Fmt("0x%02X", cid.mid));
            g.Line("microSD card OEM id"_i18n, cid.oid);
            g.Line("microSD card product"_i18n, cid.pnm);
            g.Line("microSD card revision"_i18n, Fmt("%u.%u", cid.prv_major, cid.prv_minor));
            g.Line("microSD card serial"_i18n, Fmt("%08X", cid.psn));
            g.Line("microSD card made"_i18n, Fmt("%04u-%02u", cid.year, cid.month));
        } else {
            g.Line("microSD card CID"_i18n, Fmt("%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]));
        }
    }
    return g.group;
}

auto CollectPlayActivity() -> Group {
    GroupBuilder g{"Play activity"_i18n};
    if (R_FAILED(pdmqryInitialize())) {
        return g.group;
    }
    ON_SCOPE_EXIT(pdmqryExit());
    const bool titles = R_SUCCEEDED(title::Init());
    ON_SCOPE_EXIT(if (titles) title::Exit());

    u64 games{}, playtime_ns{}, launches{}, top_ns{}, top_id{};
    title::ForEachApplicationRecord([&](std::span<const NsApplicationRecord> records) {
        for (const auto& rec : records) {
            games++;
            PdmPlayStatistics stats{};
            if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationId(rec.application_id, true, &stats))) {
                continue;
            }
            playtime_ns += stats.playtime;
            launches += stats.total_launches;
            if (stats.playtime > top_ns) {
                top_ns = stats.playtime;
                top_id = rec.application_id;
            }
        }
    });

    const auto hm = [](u64 ns) {
        const auto minutes = ns / 60'000'000'000ULL;
        return Fmt("%lu h %02lu min", minutes / 60, minutes % 60);
    };
    g.Line("Installed games"_i18n, Fmt("%lu", games));
    g.Line("Total play time"_i18n, hm(playtime_ns));
    g.Line("Total launches"_i18n, Fmt("%lu", launches));
    if (top_id) {
        std::string name = Fmt("%016lX", top_id);
        if (titles) {
            if (const auto data = title::Get(top_id); data && data->status == title::NacpLoadStatus::Loaded && data->lang.name[0]) {
                name = data->lang.name;
            }
        }
        g.Line("Most played"_i18n, name + " · " + hm(top_ns));
    }
    return g.group;
}

} // namespace sphaira::system_info
