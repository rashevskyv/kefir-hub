#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/dbi_internal.hpp"
#include "path_util.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"
#include "usb/usbds.hpp"
#include "title_info.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>

namespace sphaira::ui::menu::dbi {

void thread_func(void* user) {
    static_cast<Menu*>(user)->ThreadFunction();
}

auto ResultText(Result rc) -> std::string {
    char out[32]{};
    std::snprintf(out, sizeof(out), "0x%08X", R_VALUE(rc));
    return out;
}

auto IsDbiSessionError(Result rc) -> bool {
    // a dropped link poisons the whole session: every following package would
    // fail instantly with the same result, so it has to end the queue too.
    if (usb::IsLinkError(rc)) {
        return true;
    }

    switch (rc) {
        case Result_TransferCancelled:
        case Result_UsbCancelled:
        case Result_UsbBadMagic:
        case Result_UsbBadVersion:
        case Result_UsbBadCount:
        case Result_UsbBadBufferAlign:
        case Result_UsbBadTransferSize:
        case Result_UsbEmptyTransferSize:
        case Result_UsbOverflowTransferSize:
        case Result_UsbDsBadDeviceSpeed:
            return true;
        default:
            return false;
    }
}

void AddSizeSaturated(s64& total, s64 value) {
    total = value > INT64_MAX - total ? INT64_MAX : total + value;
}

u64 GetQueueEntryTitleId(const QueueEntry& entry) {
    for (const auto& col : entry.analysis.collections) {
        if (path::EndsWithIC(col.name, ".cnmt.nca") || path::EndsWithIC(col.name, ".cnmt.ncz")) {
            size_t pos = col.name.find(".cnmt.");
            if (pos != std::string::npos && pos >= 16) {
                std::string hex_str = col.name.substr(pos - 16, 16);
                u64 val = 0;
                bool ok = true;
                for (char c : hex_str) {
                    val <<= 4;
                    if (c >= '0' && c <= '9') val |= (c - '0');
                    else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
                    else { ok = false; break; }
                }
                if (ok) return val;
            }
        }
    }
    return 0;
}

s64 QueuePackageSize(const QueueEntry& entry) {
    if (entry.analysis.source_size > 0) return entry.analysis.source_size;
    if (entry.source_size > 0) return entry.source_size;
    return 0;
}

// how much a package is expected to write. Deferred entries only know their
// (possibly compressed) source size, so they get the same 1.6x factor that
// yati::ChooseInstallTarget uses, otherwise the plan under-books their space.
s64 PlanSize(const QueueEntry& entry) {
    if (!entry.analysis_deferred) {
        return std::max<s64>(0, entry.analysis.install_size);
    }
    return static_cast<s64>(std::max<s64>(0, QueuePackageSize(entry)) * 1.6);
}

bool IsTitleAlreadyInstalled(u64 title_id) {
    if (!title_id) return false;
    // Check BuiltInUser (NAND)
    {
        auto& db = title::GetNcmDb(NcmStorageId_BuiltInUser);
        NcmContentMetaKey key{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, title_id))) {
            return true;
        }
    }
    // Check SdCard
    {
        auto& db = title::GetNcmDb(NcmStorageId_SdCard);
        NcmContentMetaKey key{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, title_id))) {
            return true;
        }
    }
    return false;
}

// draws stat cells left to right, with the label faked bold (over-drawn -- no
// bold font face is loaded) so the labels stand out from the numbers.
void DrawStatRow(NVGcontext* vg, NVGcolor info_col, float x0, float y, float size, const std::vector<StatItem>& items) {
    float x = x0;
    nvgFontSize(vg, size);
    float space_w = nvgTextBounds(vg, 0.f, 0.f, " ", nullptr, nullptr);
    if (space_w < 1.0f) {
        space_w = std::round(size * 0.30f);
    }
    float b[4];
    for (const auto& item : items) {
        const auto lab = item.label + ":";
        gfx::drawTextArgs(vg, x, y, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info_col, "%s", lab.c_str());
        gfx::drawTextArgs(vg, x + 0.7f, y, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info_col, "%s", lab.c_str());
        gfx::textBounds(vg, 0, 0, b, lab.c_str());
        x += (b[2] - b[0]) + 0.7f + space_w;
        const auto value_col = item.colour.value_or(info_col);
        gfx::drawTextArgs(vg, x, y, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, value_col, "%s", item.value.c_str());
        gfx::textBounds(vg, 0, 0, b, item.value.c_str());
        x += (b[2] - b[0]) + 28.f;
    }
}

// "how long until those bytes are written at that rate". Empty when either
// number cannot answer the question.
auto FormatEta(s64 bytes_left, s64 bps) -> std::string {
    if (bps <= 0 || bytes_left <= 0) {
        return {};
    }
    const auto seconds_left = static_cast<size_t>(bytes_left / bps);
    char buf[64]{};
    if (seconds_left >= 3600) {
        std::snprintf(buf, sizeof(buf), "%zuh %zum", seconds_left / 3600, seconds_left % 3600 / 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%zum %zus", seconds_left / 60, seconds_left % 60);
    }
    return buf;
}

void DrawCheckbox(NVGcontext* vg, Theme* theme, const Vec4& row, bool selected) {
    gfx::drawCheckbox(vg, theme, row.x + 12.f, row.y + (row.h - gfx::CHECKBOX_SIZE) / 2.f, gfx::CHECKBOX_SIZE, selected);
}

} // namespace sphaira::ui::menu::dbi

#endif