#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/dbi_internal.hpp"
#include "sd_bundle_rules.hpp"
#include "path_util.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"
#include "usb/usbds.hpp"
#include "title_info.hpp"
#include "ui/menus/install_plan.hpp"
#include "ui/menus/update_choice.hpp"

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

s64 QueuePackageSize(const QueueEntry& entry) {
    if (entry.analysis.source_size > 0) return entry.analysis.source_size;
    if (entry.source_size > 0) return entry.source_size;
    return 0;
}

// how much a package is expected to write. Deferred entries only know their
// (possibly compressed) source size, so they get the same 1.6x factor that
// yati::ChooseInstallTarget uses, otherwise the plan under-books their space.
Result AnalyzeQueueName(yati::source::Usb* source, const std::string& name, yati::InstallAnalysis& out) {
    if (sd_bundle::IsBundleName(name)) {
        out = {};
        out.source_size = out.install_size = std::max<s64>(0, source->GetFileSize(name));
        out.suggested_sd = true;
        R_SUCCEED();
    }
    source->SetFileNameForTranfser(name);
    return yati::AnalyzeSource(source, fs::FsPath{name}, out);
}

s64 PlanSize(const QueueEntry& entry) {
    if (!entry.analysis_deferred) {
        return std::max<s64>(0, entry.analysis.install_size);
    }
    return static_cast<s64>(std::max<s64>(0, QueuePackageSize(entry)) * 1.6);
}

// the same version or a newer one is in NCM: yati would skip it (same key) or
// refuse it as a downgrade, so it takes no space. The id comes from the file
// name; the cnmt nca's name is its content id, not the title id.
// an update or DLC whose base game is in neither NCM database: it installs but
// does not run. The PC lists these in its end-of-session report.
bool IsBaseMissing(const QueueEntry& entry) {
    const auto base = BaseTitleId(ParseNameTitle(entry.file_name).id);
    if (!base) return false;
    for (const auto storage : {NcmStorageId_BuiltInUser, NcmStorageId_SdCard}) {
        NcmContentMetaKey key{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&title::GetNcmDb(storage), &key, base))) {
            return false;
        }
    }
    return true;
}

bool IsEntryAlreadyInstalled(const QueueEntry& entry) {
    const auto t = ParseNameTitle(entry.file_name);
    if (!t.id) return false;
    for (const auto storage : {NcmStorageId_BuiltInUser, NcmStorageId_SdCard}) {
        auto& db = title::GetNcmDb(storage);
        NcmContentMetaKey key{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, t.id))) {
            if (!t.has_version || key.version >= t.version) {
                return true;
            }
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

std::optional<bool> InstallSession::CheckQueuedUpdate(size_t i, bool final_pass) {
    m_ask_mode = final_pass ? AskMode::Wait : AskMode::Defer;
    m_current_file_deferred = false;
    m_current_update_count = 0;
    m_current_update_picked = false;

    std::vector<NameTitle> titles;
    std::string name;
    {
        SCOPED_MUTEX(&m_mutex);
        if (i >= m_queue.size()) return true;
        name = m_queue[i].file_name;
        titles.reserve(m_queue.size());
        for (const auto& e : m_queue) {
            titles.push_back(e.install_selected ? ParseNameTitle(e.file_name) : NameTitle{});
        }
    }
    QueueUpdates q{};
    const auto r = ResolveQueuedUpdate(UpdateChoiceOption().Get(), titles, i, m_update_picks, q,
        [this](int count, uint32_t version) {
            char text[256];
            std::snprintf(text, sizeof(text), "The queue has %d updates for this game. Install v%u?"_i18n.c_str(), count, version >> 16);
            return AskUpdate(text);
        });
    m_current_update_count = q.count;
    switch (r) {
        case QueuedUpdate::Install:
            return true;
        case QueuedUpdate::Picked:
            m_current_update_picked = true;
            return true;
        case QueuedUpdate::SkipNewerInQueue:
            AddLog("Skipped: "_i18n + name + " — " + "a newer update is in the queue"_i18n, LogKind::Success);
            return false;
        case QueuedUpdate::SkipOtherPicked:
            AddLog("Skipped: "_i18n + name + " — " + "another update of this game was chosen"_i18n, LogKind::Success);
            return false;
        case QueuedUpdate::SkipNo:
            AddLog("Skipped: "_i18n + name, LogKind::Success);
            return false;
        case QueuedUpdate::NoAnswer:
            if (!m_current_file_deferred) return false; // cancelled or skipped while asking
            AddLog("No answer yet, moved to the end of the queue: "_i18n + name, LogKind::Normal);
            return std::nullopt;
    }
    return true;
}

} // namespace sphaira::ui::menu::dbi

#endif