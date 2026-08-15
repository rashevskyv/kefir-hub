#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/forwarder_editor.hpp"
#include "nacp_util.hpp"
#include "owo.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"

#include <minIni.h>
#include <cstdio>
#include <optional>
#include <utility>
#include <algorithm>
#include <functional>

namespace sphaira::ui::menu::homebrew {
namespace {

constexpr const char* SEARCH_PATHS_INI_SECTION = "homebrew_paths";
constexpr const char* DEFAULT_SEARCH_PATH = "/switch";

Menu* g_menu{};
constinit UEvent g_change_uevent;
constexpr const char* KEFIR_UPDATER_STUB_PATH = "/switch/kefir-updater/kefir-updater.nro";
option::OptionBool g_kefir_updater_notice_ack{"homebrew", "kefir_updater_notice_ack", false};

auto NormalizeSearchPath(std::string_view path) -> std::optional<std::string> {
    const auto normalized = path::NormalizeAbsoluteSdPath(path);
    if (!normalized) {
        return std::nullopt;
    }
    if (*normalized == "/" || path::EqualsIC(*normalized, DEFAULT_SEARCH_PATH)) {
        return std::nullopt;
    }
    if (normalized->size() >= FS_MAX_PATH) {
        return std::nullopt;
    }
    return normalized;
}

auto LoadSearchPaths() -> std::vector<std::string> {
    std::vector<std::string> search_paths;

    ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        if (Section && Value && path::EqualsIC(Section, SEARCH_PATHS_INI_SECTION)) {
            auto* paths = static_cast<std::vector<std::string>*>(UserData);
            const auto normalized = NormalizeSearchPath(Value);
            if (normalized) {
                const bool duplicate = std::any_of(paths->begin(), paths->end(), [&](const std::string& existing) {
                    return path::EqualsIC(existing, *normalized);
                });
                if (!duplicate) {
                    paths->push_back(*normalized);
                }
            }
        }
        return 1;
    }, &search_paths, App::CONFIG_PATH);

    return search_paths;
}

auto SaveSearchPaths(const std::vector<std::string>& search_paths) -> bool {
    if (!ini_puts(SEARCH_PATHS_INI_SECTION, nullptr, nullptr, App::CONFIG_PATH)) {
        return false;
    }

    char key[32];
    for (size_t i = 0; i < search_paths.size(); i++) {
        std::snprintf(key, sizeof(key), "path_%zu", i);
        if (!ini_puts(SEARCH_PATHS_INI_SECTION, key, search_paths[i].c_str(), App::CONFIG_PATH)) {
            return false;
        }
    }

    return true;
}

void AppendUniqueEntries(std::vector<NroEntry>& dest, std::vector<NroEntry>& src) {
    for (auto& entry : src) {
        bool duplicate = false;
        for (const auto& existing : dest) {
            if (path::EqualsIC(existing.path.s, entry.path.s)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            dest.push_back(std::move(entry));
        }
    }
}

auto GetNroFilename(const NroEntry& e) -> std::string {
    std::string filename = e.path.s;
    size_t last_slash = filename.find_last_of('/');
    if (last_slash != std::string::npos) {
        filename = filename.substr(last_slash + 1);
    }
    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos) {
        filename = filename.substr(0, dot);
    }
    return filename;
}

auto GenerateStarPath(const fs::FsPath& nro_path) -> fs::FsPath {
    fs::FsPath out{};
    const auto dilem = std::strrchr(nro_path.s, '/');
    std::snprintf(out, sizeof(out), "%.*s.%s.star", int(dilem - nro_path.s + 1), nro_path.s, dilem + 1);
    return out;
}

void FreeEntry(NVGcontext* vg, NroEntry& e) {
    nvgDeleteImage(vg, e.image);
    e.image = 0;
}

auto IsKefirUpdaterStub(const NroEntry& e) -> bool {
    return e.path == KEFIR_UPDATER_STUB_PATH;
}

auto IsKefirUpdaterEntry(const NroEntry& e) -> bool {
    return IsKefirUpdaterStub(e) ||
        !strcasecmp(e.GetName(), "Kefir Updater") ||
        !strcasecmp(e.path, KEFIR_UPDATER_STUB_PATH);
}

class HoldOkBox final : public Widget {
public:
    using Callback = std::function<void()>;

    HoldOkBox(std::string message, Callback callback)
    : m_message{std::move(message)}
    , m_callback{std::move(callback)} {
        m_pos = Vec4{240.f, 136.f, 800.f, 444.f};
        SetActions(
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                SetPop();
            }})
        );
    }

    auto IsModal() const -> bool override { return true; }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);

        if (controller->GotHeld(Button::A)) {
            if (!m_holding) {
                m_holding = true;
                m_hold_start = armTicksToNs(armGetSystemTick());
            }

            const auto now = armTicksToNs(armGetSystemTick());
            m_progress = std::min(1.f, static_cast<float>(now - m_hold_start) / 5'000'000'000.f);
            if (m_progress >= 1.f) {
                m_callback();
                SetPop();
            }
        } else {
            m_holding = false;
            m_progress = 0.f;
        }
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        gfx::dimBackground(vg);
        gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

        constexpr float padding = 38.f;
        constexpr float text_size = 18.f;
        constexpr float line_height = 1.28f;
        const Vec4 button{m_pos.x, m_pos.y + m_pos.h - 86.f, m_pos.w, 86.f};
        const float text_x = m_pos.x + padding;
        const float text_w = m_pos.w - padding * 2.f;
        const float text_area_y = m_pos.y + 22.f;
        const float text_area_h = button.y - text_area_y - 18.f;

        nvgSave(vg);
        nvgFontSize(vg, text_size);
        nvgTextLineHeight(vg, line_height);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        float bounds[4]{};
        nvgTextBoxBounds(vg, text_x, 0.f, text_w, m_message.c_str(), nullptr, bounds);
        const float text_h = bounds[3] - bounds[1];
        const float text_y = text_area_y + std::max(0.f, (text_area_h - text_h) / 2.f) - bounds[1];

        gfx::drawTextBox(
            vg, text_x, text_y, text_size, text_w,
            theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP
        );
        nvgRestore(vg);

        gfx::drawRect(vg, button.x, button.y - 2.f, button.w, 2.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        gfx::drawRectOutline(vg, theme, 4.f, Vec4{button.x + 170.f, button.y + 10.f, button.w - 340.f, button.h - 20.f});

        const Vec4 bar{button.x + 198.f, button.y + button.h - 22.f, button.w - 396.f, 6.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * m_progress, bar.h, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 3.f);

        gfx::drawText(
            vg, button.x + button.w / 2.f, button.y + 35.f, 24.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED),
            "OK", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE
        );
    }

private:
    std::string m_message;
    Callback m_callback;
    bool m_holding{};
    u64 m_hold_start{};
    float m_progress{};
};

void ShowKefirUpdaterRemovedDialog() {
    App::Push<HoldOkBox>(
        "Kefir Updater has been removed.\n\nKefir updates now happen in the Updater menu.\n\nTo get there manually, press R, choose Updater, then choose a Kefir version."_i18n,
        []() {
            g_kefir_updater_notice_ack.Set(true);
            ueventSignal(&g_change_uevent);
        });
}

} // namespace

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

auto GetNroEntries() -> std::span<const NroEntry> {
    if (!g_menu) {
        return {};
    }

    return g_menu->GetHomebrewList();
}

Menu::Menu() : grid::Menu{"Homebrew"_i18n, MenuFlag_Tab} {
    g_menu = this;

    this->SetActions(
        std::make_pair(Button::A, Action{"Launch"_i18n, [this](){
            if (!g_kefir_updater_notice_ack.Get() && IsKefirUpdaterEntry(GetEntry())) {
                ShowKefirUpdaterRemovedDialog();
            } else {
                nro_launch(GetEntry().path);
            }
        }}),
        std::make_pair(Button::B, Action{"Exit"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
                App::Exit();
            }
        }}),
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){
            ToggleCurrentSelection();
        }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
            InvertSelection();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    OnLayoutChange();
    ueventCreate(&g_change_uevent, true);
}

Menu::~Menu() {
    *m_alive = false;
    g_menu = {};
    FreeEntries();
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (R_SUCCEEDED(waitSingle(waiterForUEvent(&g_change_uevent), 0))) {
        m_dirty = true;
    }

    if (m_dirty) {
        SortAndFindLastFile(true);
    }

    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_layout.Get() == grid::LayoutType_HbMenu && !m_entries_current.empty()) {
        const auto index = m_entries_current[m_index];
        auto& e = m_entries[index];
        bool has_star = false;
        if (IsStarEnabled()) {
            if (!e.has_star.has_value()) {
                e.has_star = fs::FsNativeSd().FileExists(GenerateStarPath(e.path));
            }
            has_star = e.has_star.value();
        }
        std::string title_text = GetNroFilename(e);
        if (has_star) {
            title_text = std::string("\u2605 ") + title_text;
        }
        DrawHbMenuHeader(vg, theme, e.image, title_text.c_str(), e.GetAuthor(), e.GetDisplayVersion(), e.GetName());
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, m_entries_current.size(), [this, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto index = m_entries_current[pos];
        auto& e = m_entries[index];

        // lazy load image
        if (image_load_count < image_load_max) {
            if (!e.image && e.icon_size && e.icon_offset) {
                // NOTE: it seems that images can be any size. SuperTux uses a 1024x1024
                // ~300Kb image, which takes a few frames to completely load.
                // really, switch-tools should handle this by resizing the image before
                // adding it to the nro, as well as validate its a valid jpeg.
                const auto icon = nro_get_icon(e.path, e.icon_size, e.icon_offset);
                TimeStamp ts;
                if (!icon.empty()) {
                    const auto image = ImageLoadIcon(icon);
                    if (!image.data.empty() && image.w == 256 && image.h == 256) {
                        e.image = nvgCreateImageRGBA(vg, image.w, image.h, 0, image.data.data());
                        log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
                        image_load_count++;
                    } else {
                        // prevent loading of this icon again as it's already failed.
                        e.icon_offset = e.icon_size = 0;
                    }
                } else {
                    // prevent loading of this icon again as it's already failed.
                    e.icon_offset = e.icon_size = 0;
                }
            }
        }


        bool has_star = false;
        if (IsStarEnabled()) {
            if (!e.has_star.has_value()) {
                e.has_star = fs::FsNativeSd().FileExists(GenerateStarPath(e.path));
            }
            has_star = e.has_star.value();
        }

        std::string card_name;
        std::string card_version;
        if (m_layout.Get() == grid::LayoutType_HbMenu) {
            std::string nro_fn = GetNroFilename(e);
            if (has_star) {
                card_name = std::string("\u2605 ") + nro_fn;
            } else {
                card_name = nro_fn;
            }
            card_version = e.GetName();
        } else {
            if (has_star) {
                card_name = std::string("\u2605 ") + e.GetName();
            } else {
                card_name = e.GetName();
            }
            card_version = e.GetDisplayVersion();
            if (m_layout.Get() == grid::LayoutType_List) {
                // right column, DBI-style: version in brackets, then the nro size.
                card_version = (card_version.empty() ? "" : "[" + card_version + "]  ") + grid::FormatBytes(e.size);
            }
        }

        const auto layout = m_layout.Get();
        const auto selected = pos == m_index;
        DrawEntry(vg, theme, layout, v, selected, e.image, card_name.c_str(), e.GetAuthor(), card_version.c_str(), e.selected);
        DrawSelectionMark(vg, theme, layout, v, v, e.selected, m_selected_count > 0);
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        ScanHomebrew();
    }
}

auto GetSearchPaths() -> std::vector<std::string> {
    return LoadSearchPaths();
}

auto IsSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeSearchPath(path.s);
    if (!normalized) {
        return false;
    }

    const auto paths = LoadSearchPaths();
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& existing) {
        return path::EqualsIC(existing, *normalized);
    });
}

auto AddSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeSearchPath(path.s);
    if (!normalized) {
        return false;
    }

    const fs::FsPath norm_path{*normalized};
    if (!fs::DirExists(norm_path)) {
        return false;
    }

    auto paths = LoadSearchPaths();
    const bool duplicate = std::any_of(paths.begin(), paths.end(), [&](const std::string& existing) {
        return path::EqualsIC(existing, *normalized);
    });
    if (duplicate) {
        return false;
    }

    paths.push_back(*normalized);
    if (!SaveSearchPaths(paths)) {
        return false;
    }

    SignalChange();
    return true;
}

auto RemoveSearchPath(const fs::FsPath& path) -> bool {
    const auto normalized = NormalizeSearchPath(path.s);
    if (!normalized) {
        return false;
    }

    auto paths = LoadSearchPaths();
    const auto it = std::find_if(paths.begin(), paths.end(), [&](const std::string& existing) {
        return path::EqualsIC(existing, *normalized);
    });
    if (it == paths.end()) {
        return false;
    }

    paths.erase(it);
    if (!SaveSearchPaths(paths)) {
        return false;
    }

    SignalChange();
    return true;
}

void Menu::SetIndex(s64 index) {
    if (m_entries_current.empty()) {
        m_index = 0;
        m_list->SetYoff(0);
        RemoveAction(Button::R3);
        SetTitleSubHeading("");
        this->SetSubHeading("0 / 0");
        return;
    }

    m_index = std::clamp<s64>(index, 0, m_entries_current.size() - 1);
    if (!m_index) {
        m_list->SetYoff(0);
    }

    if (IsStarEnabled() && !IsKefirUpdaterStub(GetEntry())) {
        const auto star_path = GenerateStarPath(GetEntry().path);
        if (fs::FsNativeSd().FileExists(star_path)) {
            SetAction(Button::R3, Action{"Unstar"_i18n, [this](){
                fs::FsNativeSd().DeleteFile(GenerateStarPath(GetEntry().path));
                App::Notify("Unstarred "_i18n + GetEntry().GetName());
                SortAndFindLastFile();
            }});
        } else {
            SetAction(Button::R3, Action{"Star"_i18n, [this](){
                fs::FsNativeSd().CreateFile(GenerateStarPath(GetEntry().path));
                App::Notify("Starred "_i18n + GetEntry().GetName());
                SortAndFindLastFile();
            }});
        }
    } else {
        RemoveAction(Button::R3);
    }

    // TimeCalendarTime caltime;
    // timeToCalendarTimeWithMyRule()
    // todo: fix GetFileTimeStampRaw being different to timeGetCurrentTime
    // log_write("name: %s hbini.ts: %lu file.ts: %lu smaller: %s\n", e.GetName(), e.hbini.timestamp, e.timestamp.modified, e.hbini.timestamp < e.timestamp.modified ? "true" : "false");

    SetTitleSubHeading(GetEntry().path);
    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries_current.size()));
}

void Menu::InstallHomebrew() {
    const auto& nro = GetEntry();
    if (R_FAILED(InstallHomebrew(nro.path, nro_get_icon(nro.path, nro.icon_size, nro.icon_offset)))) {
        log_write("failed to create forwarder\n");
    }
}

// rewrites the name and icon inside the nro itself, so the change shows up in
// every launcher, not just here.
void Menu::CustomizeHomebrew() {
    const auto& nro = GetEntry();
    const auto path = nro.path;

    forwarder::Config editor{};
    editor.values.title = nro.GetName();
    editor.values.icon = nro_get_icon(path, nro.icon_size, nro.icon_offset);
    editor.steam_query = editor.values.title;
    editor.screen_title = "Edit name and icon"_i18n;
    editor.submit_label = "Save Changes"_i18n;
    editor.on_create = [weak_alive = std::weak_ptr<bool>(m_alive), this, path](const forwarder::Values& values) {
        const auto alive = weak_alive.lock();
        if (!alive || !*alive) {
            return false;
        }
        const auto title = values.title;
        const auto icon = values.icon;

        App::Push<ProgressBox>(
            0, "Updating Homebrew"_i18n, title,
            [path, title, icon](auto pbox) -> Result {
                pbox->NewTransfer(title);
                return nro_update_info(path, title, icon);
            },
            [weak_alive, this](Result rc) {
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Failed to update the homebrew"_i18n);
                    return;
                }
                const auto alive = weak_alive.lock();
                if (!alive || !*alive) {
                    return;
                }
                App::Notify("Homebrew updated"_i18n);
                SortAndFindLastFile(true);
            }
        );
        return true;
    };

    forwarder::Show(std::move(editor));
}

void Menu::ScanHomebrew() {
    TimeStamp ts;
    FreeEntries();
    nro_scan(DEFAULT_SEARCH_PATH, m_entries);

    const auto search_paths = LoadSearchPaths();
    for (const auto& path_str : search_paths) {
        const fs::FsPath path{path_str};
        if (!fs::DirExists(path)) {
            continue;
        }

        std::vector<NroEntry> custom_entries;
        if (R_SUCCEEDED(nro_scan_depth(path, custom_entries, 2))) {
            AppendUniqueEntries(m_entries, custom_entries);
        }
    }

    if (!g_kefir_updater_notice_ack.Get() && std::ranges::none_of(m_entries, [](const auto& entry) {
        return IsKefirUpdaterEntry(entry);
    })) {
        NroEntry entry{};
        entry.path = KEFIR_UPDATER_STUB_PATH;
        std::strncpy(entry.nacp.lang.name, "Kefir Updater", sizeof(entry.nacp.lang.name) - 1);
        std::strncpy(entry.nacp.lang.author, "rashevskyv", sizeof(entry.nacp.lang.author) - 1);
        std::strncpy(entry.nacp.display_version, "Removed", sizeof(entry.nacp.display_version) - 1);
        m_entries.emplace_back(entry);
    }

    log_write("nros found: %zu time_taken: %.2f\n", m_entries.size(), ts.GetSecondsD());

    struct IniUser {
        std::vector<NroEntry>& entires;
        Hbini* ini{};
        std::string last_section{};
    } ini_user{ m_entries };

    ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto user = static_cast<IniUser*>(UserData);

        if (user->last_section != Section) {
            user->last_section = Section;
            user->ini = nullptr;

            for (auto& e : user->entires) {
                if (e.path == Section) {
                    user->ini = &e.hbini;
                    break;
                }
            }
        }

        if (user->ini) {
            if (!strcmp(Key, "timestamp")) {
                user->ini->timestamp = ini_parse_getl(Value, 0);
            } else if (!strcmp(Key, "hidden")) {
                user->ini->hidden = ini_parse_getbool(Value, false);
            }
        }

        // log_write("found: %s %s %s\n", Section, Key, Value);
        return 1;
    }, &ini_user, App::PLAYLOG_PATH);

    // pre-allocate the max size.
    for (auto& index : m_entries_index) {
        index.reserve(m_entries.size());
    }

    for (u32 i = 0; i < m_entries.size(); i++) {
        auto& e = m_entries[i];

        m_entries_index[Filter_All].emplace_back(i);

        if (!e.hbini.hidden) {
            m_entries_index[Filter_HideHidden].emplace_back(i);
        }
    }

    this->Sort();
    SetIndex(0);
    m_dirty = false;
}

void Menu::Sort() {
    if (IsStarEnabled()) {
        fs::FsNativeSd fs;
        fs::FsPath star_path;
        for (auto& p : m_entries) {
            p.has_star = fs.FileExists(GenerateStarPath(p.path));
        }
    }

    // returns true if lhs should be before rhs
    const auto sort = m_sort.Get();
    const auto order = m_order.Get();

    const auto sorter = [this, sort, order](u32 _lhs, u32 _rhs) -> bool {
        const auto& lhs = m_entries[_lhs];
        const auto& rhs = m_entries[_rhs];

        const auto name_cmp = [order](const NroEntry& lhs, const NroEntry& rhs) -> bool {
            auto r = strcasecmp(lhs.GetName(), rhs.GetName());
            if (!r) {
                r = strcasecmp(lhs.GetAuthor(), rhs.GetAuthor());
                if (!r) {
                    r = strcasecmp(lhs.path, rhs.path);
                }
            }

            if (order == OrderType_Descending) {
                return r < 0;
            } else {
                return r > 0;
            }
        };

        switch (sort) {
            case SortType_UpdatedStar:
                if (lhs.has_star.value() && !rhs.has_star.value()) {
                    return true;
                } else if (!lhs.has_star.value() && rhs.has_star.value()) {
                    return false;
                }
                [[fallthrough]];
            case SortType_Updated: {
                auto lhs_timestamp = lhs.hbini.timestamp;
                auto rhs_timestamp = rhs.hbini.timestamp;
                if (lhs.timestamp.is_valid && lhs_timestamp < lhs.timestamp.modified) {
                    lhs_timestamp = lhs.timestamp.modified;
                }
                if (rhs.timestamp.is_valid && rhs_timestamp < rhs.timestamp.modified) {
                    rhs_timestamp = rhs.timestamp.modified;
                }

                if (lhs_timestamp == rhs_timestamp) {
                    return name_cmp(lhs, rhs);
                } else if (order == OrderType_Descending) {
                    return lhs_timestamp > rhs_timestamp;
                } else {
                    return lhs_timestamp < rhs_timestamp;
                }
            } break;

            case SortType_SizeStar:
                if (lhs.has_star.value() && !rhs.has_star.value()) {
                    return true;
                } else if (!lhs.has_star.value() && rhs.has_star.value()) {
                    return false;
                }
                [[fallthrough]];
            case SortType_Size: {
                if (lhs.size == rhs.size) {
                    return name_cmp(lhs, rhs);
                } else if (order == OrderType_Descending) {
                    return lhs.size > rhs.size;
                } else {
                    return lhs.size < rhs.size;
                }
            } break;

            case SortType_AlphabeticalStar:
                if (lhs.has_star.value() && !rhs.has_star.value()) {
                    return true;
                } else if (!lhs.has_star.value() && rhs.has_star.value()) {
                    return false;
                }
                [[fallthrough]];
            case SortType_Alphabetical: {
                return name_cmp(lhs, rhs);
            } break;
        }

        std::unreachable();
    };

    if (m_show_hidden.Get()) {
        m_entries_current = m_entries_index[Filter_All];
    } else {
        m_entries_current = m_entries_index[Filter_HideHidden];
    }

    std::sort(m_entries_current.begin(), m_entries_current.end(), sorter);
}

void Menu::SortAndFindLastFile(bool scan) {
    fs::FsPath path;
    if (!m_entries_current.empty()) {
        path = GetEntry().path;
    }

    if (scan) {
        ScanHomebrew();
    } else {
        Sort();
    }
    SetIndex(0);

    if (path.empty()) {
        return;
    }

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (path == GetEntry(i).path) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        const auto row = m_list->GetRow();
        const auto page = m_list->GetPage();
        // guesstimate where the position is
        if (index >= page) {
            m_list->SetYoff((((index - page) + row) / row) * m_list->GetMaxY());
        } else {
            m_list->SetYoff(0);
        }
        SetIndex(index);
    }
}

void Menu::FreeEntries() {
    auto vg = App::GetVg();

    for (auto&p : m_entries) {
        FreeEntry(vg, p);
    }

    m_entries.clear();
    for (auto& e : m_entries_index) {
        e.clear();
    }
    m_selected_count = 0;
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
}

Result Menu::InstallHomebrew(const fs::FsPath& path, const std::vector<u8>& icon) {
    OwoConfig config{};
    config.nro_path = path.toString();
    R_TRY(nro_get_nacp(path, config.nacp));
    config.icon = icon;

    // the default is a silent install using the settings defaults.
    if (!App::GetForwarderAsk()) {
        return App::Install(config);
    }

    forwarder::Config editor{};
    editor.values.title = nacp_util::GetName(config.nacp);
    editor.values.author = nacp_util::GetAuthor(config.nacp);
    editor.values.version = std::string(config.nacp.display_version, strnlen(config.nacp.display_version, sizeof(config.nacp.display_version)));
    editor.values.icon = icon;
    editor.values.options = App::GetForwarderOptions();
    editor.steam_query = editor.values.title;
    editor.show_author = true;
    editor.show_version = true;
    editor.on_create = [config](const forwarder::Values& values) mutable {
        config.name = values.title;
        config.author = values.author;
        std::snprintf(config.nacp.display_version, sizeof(config.nacp.display_version), "%s", values.version.c_str());
        config.icon = values.icon;
        config.options = values.options;
        return R_SUCCEEDED(App::Install(config));
    };

    forwarder::Show(std::move(editor));
    R_SUCCEED();
}

Result Menu::InstallHomebrewFromPath(const fs::FsPath& path) {
    return InstallHomebrew(path, nro_get_icon(path));
}

void Menu::ToggleCurrentSelection() {
    if (m_entries_current.empty()) {
        return;
    }

    auto& entry = GetEntry();
    if (!IsKefirUpdaterStub(entry)) {
        entry.selected ^= 1;
        m_selected_count += entry.selected ? 1 : -1;
    }

    if (m_index + 1 < static_cast<s64>(m_entries_current.size())) {
        SetIndex(m_index + 1);
        m_list->EnsureVisible(m_index, m_entries_current.size());
    }
}

void Menu::InvertSelection() {
    m_selected_count = 0;
    for (auto& entry : m_entries) {
        if (IsKefirUpdaterStub(entry)) {
            entry.selected = false;
            continue;
        }
        if (entry.hbini.hidden && !m_show_hidden.Get()) {
            entry.selected = false;
            continue;
        }
        entry.selected ^= 1;
        if (entry.selected) {
            m_selected_count++;
        }
    }
}

void Menu::ClearSelection() {
    for (auto& entry : m_entries) {
        entry.selected = false;
    }
    m_selected_count = 0;
}

auto Menu::GetSelectedEntries() const -> std::vector<NroEntry> {
    std::vector<NroEntry> out;
    if (m_selected_count > 0) {
        for (const auto& e : m_entries) {
            if (e.selected && !IsKefirUpdaterStub(e)) {
                out.emplace_back(e);
            }
        }
    }

    if (out.empty() && !m_entries_current.empty()) {
        const auto& focused = m_entries[m_entries_current[m_index]];
        if (!IsKefirUpdaterStub(focused)) {
            out.emplace_back(focused);
        }
    }

    return out;
}

void Menu::StarHomebrew(bool star) {
    const auto targets = GetSelectedEntries();
    if (targets.empty()) {
        return;
    }

    fs::FsNativeSd fs;
    size_t count = 0;
    for (const auto& e : targets) {
        if (IsKefirUpdaterStub(e)) {
            continue;
        }
        const auto star_path = GenerateStarPath(e.path);
        const bool exists = fs.FileExists(star_path);
        if (star) {
            if (!exists) {
                auto rc = fs.CreateFile(star_path);
                if (R_SUCCEEDED(rc) || rc == FsError_PathAlreadyExists) {
                    count++;
                }
            }
        } else {
            if (exists) {
                auto rc = fs.DeleteFile(star_path);
                if (R_SUCCEEDED(rc) || rc == FsError_PathNotFound) {
                    count++;
                }
            }
        }
    }

    if (count > 0) {
        if (targets.size() == 1) {
            App::Notify((star ? "Starred "_i18n : "Unstarred "_i18n) + targets.front().GetName());
        } else {
            App::Notify(std::to_string(count) + " " + (star ? "homebrew starred"_i18n : "homebrew unstarred"_i18n));
        }
    }

    ClearSelection();
    SortAndFindLastFile();
}

void Menu::DeleteHomebrew() {
    const auto targets = GetSelectedEntries();
    if (targets.empty()) {
        return;
    }

    App::Push<ProgressBox>(0, "Deleting"_i18n, "", [targets](auto pbox) -> Result {
        fs::FsNativeSd fs;
        for (size_t i = 0; i < targets.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& e = targets[i];
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, targets.size());

            const auto star_path = GenerateStarPath(e.path);
            if (fs.FileExists(star_path)) {
                R_TRY(fs.DeleteFile(star_path));
            }

            R_TRY(fs.DeleteFile(e.path));
        }
        return 0;
    }, [this](Result rc){
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Delete failed!"_i18n);
        } else {
            App::Notify("Delete successful!"_i18n);
        }

        ClearSelection();
        ScanHomebrew();
    });
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryHeader>("SORT"_i18n);

    SidebarEntryArray::Items sort_items;
    sort_items.push_back("Updated"_i18n);
    sort_items.push_back("Alphabetical"_i18n);
    sort_items.push_back("Size"_i18n);
    sort_items.push_back("Updated (Star)"_i18n);
    sort_items.push_back("Alphabetical (Star)"_i18n);
    sort_items.push_back("Size (Star)"_i18n);

    SidebarEntryArray::Items order_items;
    order_items.push_back("Descending"_i18n);
    order_items.push_back("Ascending"_i18n);

    // item order matches LayoutType: List, Grid(Icon), GridDetail(Grid), HbMenu.
    SidebarEntryArray::Items layout_items;
    layout_items.push_back("List"_i18n);
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);

    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this, sort_items](s64& index_out){
        m_sort.Set(index_out);
        SortAndFindLastFile();
    }, m_sort.Get(), "Select which field to sort homebrew by."_i18n);

    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this, order_items](s64& index_out){
        m_order.Set(index_out);
        SortAndFindLastFile();
    }, m_order.Get(), "Display entries in Ascending or Descending order."_i18n);

    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_layout.Set(index_out);
        OnLayoutChange();
    }, m_layout.Get(), "Choose how apps are displayed on screen."_i18n);

    options->Add<SidebarEntryBool>("Show Hidden"_i18n, m_show_hidden.Get(), [this](bool& enable){
        m_show_hidden.Set(enable);
        SortAndFindLastFile();
    }, "Shows all hidden homebrew."_i18n);

    const auto targets = GetSelectedEntries();
    if (!targets.empty()) {
        fs::FsNativeSd fs;
        bool has_unstarred = false;
        bool has_starred = false;
        for (const auto& t : targets) {
            if (fs.FileExists(GenerateStarPath(t.path))) {
                has_starred = true;
            } else {
                has_unstarred = true;
            }
        }

        if (m_selected_count > 0) {
            options->Add<SidebarEntryHeader>("SELECTED HOMEBREW"_i18n,
                std::to_string(targets.size()) + " " + "selected"_i18n);
        } else {
            options->Add<SidebarEntryHeader>("THIS HOMEBREW"_i18n);
        }

        if (targets.size() == 1 && m_selected_count == 0) {
            options->Add<SidebarEntryCallback>("Edit name and icon"_i18n, [this](){
                CustomizeHomebrew();
            }, "Change the name and icon stored inside the nro file itself. Affects every launcher, not just this one."_i18n);
        }

        if (has_unstarred) {
            options->Add<SidebarEntryCallback>("Star"_i18n, [this](){
                StarHomebrew(true);
            }, "Mark as favorite."_i18n);
        }

        if (has_starred) {
            options->Add<SidebarEntryCallback>("Unstar"_i18n, [this](){
                StarHomebrew(false);
            }, "Remove favorite mark."_i18n);
        }

        options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
            const auto targets = GetSelectedEntries();
            if (targets.empty()) {
                return;
            }
            const auto msg = targets.size() == 1
                ? "Are you sure you want to delete "_i18n + targets.front().GetName() + "?"
                : "Are you sure you want to delete the selected homebrew?"_i18n;
            App::Push<OptionBox>(
                msg,
                "Back"_i18n, "Delete"_i18n, 0, [this](auto op_index){
                    if (op_index && *op_index) {
                        DeleteHomebrew();
                    }
                }, targets.front().image
            );
        }, true, "Permanently delete all selected homebrew."_i18n);

        if (targets.size() == 1 && m_selected_count == 0) {
            options->Add<SidebarEntryHeader>("FORWARDER"_i18n);

            auto entry = options->Add<SidebarEntryCallback>("Install Forwarder"_i18n, [this](){
                InstallHomebrew();
            }, "Add this homebrew to the HOME menu as its own tile."_i18n);
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);

            options->Add<SidebarEntryBool>("Ask every time"_i18n, App::GetApp()->m_forwarder_ask,
                "Open the forwarder editor when creating a forwarder instead of using the defaults from Settings."_i18n);

            options->Add<SidebarEntryCallback>("Forwarder options"_i18n, [](){
                App::DisplayForwarderOptions(false);
            }, "Defaults baked into forwarders you create."_i18n);
        }
    }

    AddInstallShareOptions(options.get());
    AddSettingsOption(options.get());
}

} // namespace sphaira::ui::menu::homebrew
