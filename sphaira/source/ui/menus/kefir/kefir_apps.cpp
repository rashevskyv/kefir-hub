// The Apps tab of the Updater: what is on the card, what is newer, and A to get it.
#include "ui/menus/kefir/kefir_apps.hpp"
#include "ui/menus/appstore.hpp"
#include "ui/menus/appstore/appstore_internal.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/nvg_util.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "auto_update.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "nro.hpp"

#include <algorithm>
#include <strings.h>
#include <unordered_map>

namespace sphaira::ui::menu::kefir {
namespace {

constexpr const char* HUB_RELEASE_URL = "https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest";
constexpr const char* DBI_RELEASE_URL = "https://api.github.com/repos/rashevskyv/DBIPatcher/releases/latest";
constexpr const char* HUB_RELEASE_CACHE = "/switch/sphaira/cache/apps_hub_latest.json";
constexpr const char* DBI_RELEASE_CACHE = "/switch/sphaira/cache/apps_dbi_latest.json";
constexpr float ROW_H = 74.f;
constexpr float ROW_GAP = 8.f;

auto Lower(std::string s) -> std::string {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// the store entries of every source, keyed by their nro path
auto LoadStoreEntries() -> std::unordered_map<std::string, appstore::Entry> {
    std::unordered_map<std::string, appstore::Entry> out;
    const auto sources = appstore::LoadSources();
    for (u32 s = 0; s < sources.size(); s++) {
        std::vector<appstore::Entry> part;
        appstore::from_json(appstore::BuildRepoCachePath(s), part);
        for (auto& e : part) {
            if (e.binary.empty() || e.binary == "none") {
                continue;
            }
            e.base_url = sources[s];
            out.try_emplace(Lower(e.binary), std::move(e)); // the first source wins, as in the App Store
        }
    }
    return out;
}

auto StoreState(const appstore::Entry& e) -> AppState {
    switch (e.status) {
        case appstore::EntryStatus::Update: return AppState::Update;
        case appstore::EntryStatus::Installed:
        case appstore::EntryStatus::Local: return AppState::Latest;
        case appstore::EntryStatus::Get: break; // filtered (hbmenu replaced by the Hub, a single RetroArch core)
    }
    return AppState::NoInfo;
}

auto RowOrder(const AppRow& a, const AppRow& b) -> bool {
    const auto rank = [](const AppRow& r) {
        return r.source == AppSource::Hub ? 0 : r.source == AppSource::Dbi ? 1 : r.state == AppState::Update ? 2 : 3;
    };
    if (rank(a) != rank(b)) {
        return rank(a) < rank(b);
    }
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
}

auto ReadTextFile(const char* path) -> std::string {
    std::vector<u8> data;
    if (R_FAILED(fs::read_entire_file(fs::FsPath{path}, data))) {
        return {};
    }
    return std::string(data.begin(), data.end());
}

} // namespace

AppsTab::AppsTab(const Vec4& pos) {
    m_pos = pos;
    const Vec4 v{pos.x + 35.f, pos.y, pos.w - 70.f, ROW_H};
    const auto rows = std::max<s64>(1, static_cast<s64>((pos.h + ROW_GAP) / (ROW_H + ROW_GAP)));
    m_list = std::make_unique<List>(1, rows, pos, v, Vec2{0.f, ROW_GAP});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetScrollBarPos(pos.x + pos.w - 10.f, pos.y, pos.h);
    Refresh();
}

AppsTab::~AppsTab() = default;

void AppsTab::Refresh() {
    if (m_loading) {
        return;
    }
    fs::FsNativeSd().CreateDirectoryRecursively(appstore::CACHE_PATH);
    m_loading = true;

    // the rows are built when the last answer is in; a failed answer keeps the file saved last time
    const auto sources = appstore::LoadSources();
    m_pending = sources.size() + 2;
    const auto done = [this](auto&) {
        if (!--m_pending) {
            m_loading = false;
            Build();
        }
        return true;
    };
    for (u32 i = 0; i < sources.size(); i++) {
        curl::Api().ToFileAsync(curl::Url{sources[i] + "/repo.json"}, curl::Path{appstore::BuildRepoCachePath(i)},
            curl::Flags{curl::Flag_Cache}, curl::StopToken{this->GetToken()}, curl::OnComplete{done});
    }
    for (const auto& [url, path] : {std::pair{HUB_RELEASE_URL, HUB_RELEASE_CACHE}, std::pair{DBI_RELEASE_URL, DBI_RELEASE_CACHE}}) {
        curl::Api().ToFileAsync(curl::Url{url}, curl::Path{path}, curl::Flags{curl::Flag_Cache},
            curl::StopToken{this->GetToken()}, curl::Header{{"Accept", "application/vnd.github+json"}}, curl::OnComplete{done});
    }
}

void AppsTab::Build() {
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    const auto hub_path = auto_update::ResolveInstallDestination(App::GetExePath());
    m_rows.clear();

    // the Hub row is there even when the Hub runs from a path the scan does not cover (/hbmenu.nro)
    {
        AppRow row{.name = "Kefir Hub", .path = hub_path.s, .installed = APP_VERSION, .source = AppSource::Hub};
        auto_update::Release release;
        if (auto_update::ReadRelease(HUB_RELEASE_CACHE, release)) {
            const int best = auto_update::SelectBestAsset(release.assets, App::GetExePath().s);
            if (best >= 0) {
                row.latest = release.tag;
                row.url = release.assets[best].browser_download_url;
            }
        }
        row.state = AppStateOf(row.installed, row.latest);
        m_rows.push_back(std::move(row));
    }

    auto store = LoadStoreEntries();
    auto_update::Release dbi_release;
    const bool has_dbi_release = auto_update::ReadRelease(DBI_RELEASE_CACHE, dbi_release);

    for (const auto& nro : homebrew::ScanInstalledNros()) {
        AppRow row{.name = nro.GetName(), .path = nro.path.s, .installed = nro.GetDisplayVersion()};
        row.source = SourceOf(row.path, hub_path.s);
        // a second Hub copy under /switch is not a second row: the Hub row above is the one that updates
        if (row.source == AppSource::Hub || row.name == "Kefir Hub") {
            continue;
        }
        if (row.name.empty()) {
            row.name = row.path;
        }
        if (row.source == AppSource::Dbi) {
            row.latest = has_dbi_release ? dbi_release.tag : std::string{};
            row.state = AppStateOf(row.installed, row.latest);
        } else if (auto it = store.find(Lower(row.path)); it != store.end()) {
            row.store = std::make_shared<appstore::Entry>(std::move(it->second));
            store.erase(it); // one row per store entry
            appstore::ClassifyInstalledBinary(*row.store);
            if (!row.store->installed_version.empty()) {
                row.installed = row.store->installed_version;
            }
            row.latest = row.store->version;
            row.state = StoreState(*row.store);
        }
        m_rows.push_back(std::move(row));
    }

    std::stable_sort(m_rows.begin(), m_rows.end(), RowOrder);
    m_index = std::clamp<s64>(m_index, 0, std::max<s64>(0, static_cast<s64>(m_rows.size()) - 1));
}

void AppsTab::Activate() {
    if (m_loading || m_rows.empty()) {
        App::PlaySoundEffect(SoundEffect_Limit);
        return;
    }
    const auto row = m_rows[m_index]; // a copy: an update rebuilds the rows
    if (row.source == AppSource::Hub) {
        UpdateHub(row);
        return;
    }
    if (row.state == AppState::NoInfo) {
        App::Notify("No source knows this app."_i18n);
        return;
    }
    if (row.state == AppState::Latest) {
        App::Notify("Already the newest version."_i18n);
        return;
    }
    if (row.source == AppSource::Dbi) {
        UpdateDbi(row);
    } else {
        UpdateStoreApp(row);
    }
}

void AppsTab::UpdateHub(const AppRow& row) {
    const auto job = auto_update::GetJob();
    switch (job.state) {
        case auto_update::JobState::Ready:
            App::ExitRestart();
            return;
        case auto_update::JobState::Downloading:
        case auto_update::JobState::Installing:
            App::Notify("Updating"_i18n + " Kefir Hub...");
            return;
        default:
            break;
    }
    if (row.state != AppState::Update || row.url.empty()) {
        App::Notify(row.state == AppState::Latest ? "Already the newest version."_i18n : "No source knows this app."_i18n);
        return;
    }
    // the job may already hold this release (the start-up check found it); a new one only from Idle or Failed
    if (job.state != auto_update::JobState::Available) {
        auto_update::SetAvailable(row.latest, row.url);
    }
    auto_update::StartDownload(true);
}

void AppsTab::UpdateDbi(const AppRow& row) {
    using namespace settings::detail;
    using settings::DbiTranslationEntry;
    const auto name = row.name;
    const bool translated = fs::FsNativeSd().FileExists("/switch/DBI/translation.bin");
    const auto asset = DbiLanguageAsset(ReadTextFile(std::string(DBI_LANGUAGE_RECORD).c_str()));

    const auto install = [this, name](DbiTranslationEntry entry) {
        App::Push<ProgressBox>(0, "Updating"_i18n, name, [entry](auto pbox) -> Result {
            if (entry.translation_url.empty()) { // DBI without a translation: only the nro
                R_TRY(DownloadFile(pbox, "Downloading DBI..."_i18n, std::string(DBI_RELEASE_DOWNLOAD) + "DBI.nro", "/switch/DBI/DBI_new.nro"));
                R_TRY(MovePath("/switch/DBI/DBI_new.nro", "/switch/DBI/DBI.nro"));
                R_SUCCEED();
            }
            return InstallDbiTranslation(pbox, entry); // the nro and its translation, as a pair
        }, [this, name](Result rc) {
            Installed(rc, name);
        });
    };

    if (!translated) {
        install({});
        return;
    }
    if (!asset.empty()) {
        install({.name = asset, .translation_url = std::string(DBI_RELEASE_DOWNLOAD) + asset});
        return;
    }

    // a translation from before the Hub kept a record: ask once, the answer is the record from now on
    auto entries = std::make_shared<std::vector<DbiTranslationEntry>>(ParseDbiTranslations(DBI_TRANSLATIONS_PACKAGE));
    if (entries->empty()) {
        App::Notify("Choose the DBI language in Additional programs → DBI."_i18n);
        return;
    }
    PopupList::Items names;
    s64 preselect = 0;
    for (const auto& e : *entries) {
        if (const auto lang = HubLanguageFor(e.name); lang && lang->code == i18n::GetCurrentLanguageCode()) {
            preselect = static_cast<s64>(names.size());
        }
        names.push_back(e.name);
    }
    App::Push<PopupList>("Which language does your DBI use?"_i18n, std::move(names), [entries, install](std::optional<s64> op) {
        if (op && *op >= 0 && *op < static_cast<s64>(entries->size())) {
            install((*entries)[*op]);
        }
    }, preselect);
}

void AppsTab::UpdateStoreApp(const AppRow& row) {
    if (!row.store) {
        return;
    }
    auto entry = row.store;
    App::Push<ProgressBox>(0, "Updating"_i18n, row.name, [entry](auto pbox) -> Result {
        return appstore::InstallApp(pbox, *entry);
    }, [this, name = row.name](Result rc) {
        Installed(rc, name);
    });
}

void AppsTab::Installed(Result rc, const std::string& name) {
    if (rc == Result_TransferCancelled) {
        return;
    }
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed"_i18n);
        return;
    }
    homebrew::SignalChange();
    App::Notify(name + ": " + "updated"_i18n);
    Build(); // the new version is on the card; the answers saved before still hold
}

void AppsTab::Update(Controller* controller, TouchInfo* touch) {
    if (m_loading || m_rows.empty()) {
        return;
    }
    m_list->OnUpdate(controller, touch, m_index, m_rows.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            Activate();
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            m_index = i;
        }
    }, this);
}

auto AppsTab::StatusText(const AppRow& row) const -> std::string {
    if (row.source == AppSource::Hub) {
        const auto job = auto_update::GetJob();
        switch (job.state) {
            case auto_update::JobState::Downloading:
                return "Downloading"_i18n + " " + std::to_string(static_cast<int>(job.progress * 100.f)) + "%";
            case auto_update::JobState::Installing: return "Installing"_i18n;
            case auto_update::JobState::Ready: return "Press A to restart"_i18n;
            case auto_update::JobState::Failed:
                if (row.state == AppState::Update) {
                    return "Failed, press A to try again"_i18n;
                }
                break;
            default: break;
        }
    }
    switch (row.state) {
        case AppState::Update: return "UPDATE"_i18n;
        case AppState::Latest: return "Up to date"_i18n;
        case AppState::NoInfo: break;
    }
    return "No update info"_i18n;
}

void AppsTab::Draw(NVGcontext* vg, Theme* theme) {
    if (m_loading || m_rows.empty()) {
        const auto text = m_loading ? "Checking for updates..."_i18n : "No apps found."_i18n;
        gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_pos.y + m_pos.h / 2.f, 22.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }

    m_list->Draw(vg, theme, m_rows.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& row = m_rows[i];
        const bool selected = m_index == i;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != static_cast<s64>(m_rows.size()) - 1) {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const bool update = row.state == AppState::Update;
        const auto name_id = update ? ThemeEntryID_TEXT_SELECTED : selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        gfx::drawTextBox(vg, v.x + 20.f, v.y + 11.f, 23.f, v.w - 360.f, theme->GetColour(name_id), row.name.c_str());

        auto versions = "Installed:"_i18n + " " + (row.installed.empty() ? std::string{"?"} : row.installed);
        if (!row.latest.empty()) {
            versions += "   " + "Newest:"_i18n + " " + row.latest;
        }
        gfx::drawTextBox(vg, v.x + 20.f, v.y + 44.f, 16.f, v.w - 360.f, theme->GetColour(ThemeEntryID_TEXT_INFO), versions.c_str());

        const auto status = StatusText(row);
        gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + v.h / 2.f, 17.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            theme->GetColour(update ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO), "%s", status.c_str());
    });
}

} // namespace sphaira::ui::menu::kefir
