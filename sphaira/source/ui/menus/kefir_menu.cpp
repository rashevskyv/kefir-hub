#include "ui/menus/kefir_menu.hpp"
#include "ui/menus/kefir/kefir_internal.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "net.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "download.hpp"
#include "hats_version.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace sphaira::ui::menu::kefir {
using namespace detail;

Menu::Menu() : MenuBase{"Updater", MenuFlag_None} {
    RefreshSystemInfo();

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (!m_entries.empty() && !m_loading) {
                OpenSelected();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_retry_on_connect = false;
            m_loaded = false;
            RefreshSystemInfo();
            FetchLinks();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    OnLayoutChange();
}

Menu::~Menu() = default;

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_retry_on_connect && !m_loading && net::IsConnectedCached()) {
        m_retry_on_connect = false;
        FetchLinks();
    }

    if (m_entries.empty()) {
        return;
    }

    if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles) {
        if (controller->GotDown(Button::RIGHT)) {
            MoveTileSelection(1);
            return;
        } else if (controller->GotDown(Button::LEFT)) {
            MoveTileSelection(-1);
            return;
        } else if (controller->GotDown(Button::DOWN)) {
            MoveTileSelection(TILE_COLUMNS);
            return;
        } else if (controller->GotDown(Button::UP)) {
            MoveTileSelection(-TILE_COLUMNS);
            return;
        } else if (controller->GotDown(Button::R2)) {
            MoveTileSelection(TILE_COLUMNS * 2);
            return;
        } else if (controller->GotDown(Button::L2)) {
            MoveTileSelection(-TILE_COLUMNS * 2);
            return;
        }

        m_list->OnUpdate(controller, touch, m_tile_index, m_tile_entries.size(), [this](bool touch, auto i) {
            const auto tile_index = ResolveTileSlotIndex(m_tile_entries, i, m_tile_index);
            if (touch && m_tile_index == tile_index) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_tile_index = tile_index;
                if (tile_index >= 0 && tile_index < static_cast<s64>(m_tile_entries.size()) && m_tile_entries[tile_index] != TILE_EMPTY) {
                    SetIndex(m_tile_entries[tile_index]);
                }
            }
        }, this);
    } else {
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    RefreshSystemInfo();

    // a folder or zip was chosen in the manual-install picker; now that it has
    // closed and we are the top menu again, kick off validation/installation.
    if (m_pending_manual_firmware) {
        const auto path = *m_pending_manual_firmware;
        const bool is_zip = m_pending_manual_firmware_is_zip;
        m_pending_manual_firmware.reset();
        m_pending_manual_firmware_is_zip = false;

        if (is_zip) {
            StartManualZipFirmware(path);
        } else {
            std::string name = path.s;
            if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
                name = name.substr(slash + 1);
            }
            if (name.empty()) {
                name = "Firmware";
            }

            PromptInstallFirmware(name, path, std::nullopt, std::nullopt, true);
        }
        return;
    }

    if (!m_loaded && !m_loading) {
        FetchLinks();
    }
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Updater Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items view_items{
        "List"_i18n,
        "Grid"_i18n,
    };

    options->Add<SidebarEntryArray>("Layout"_i18n, view_items, [this](s64& index_out) {
        m_view_mode.Set(index_out);
        OnLayoutChange();
    }, m_view_mode.Get(), "Switch between list and grid view for the updater."_i18n);

    // the whole downgrade-fix surface is hidden while it has no working
    // implementation, so nothing in the ui promises something it cannot do.
    if (detail::IsDowngradeFixAvailable()) {
        // policy for the downgrade fix when installing a lower firmware.
        // items order must match enum DowngradeFixMode.
        SidebarEntryArray::Items downgrade_items{
            "Automatic"_i18n,
            "Optional (ask)"_i18n,
            "Off"_i18n,
        };
        options->Add<SidebarEntryArray>("Downgrade fix"_i18n, downgrade_items, [this](s64& index_out) {
            m_downgrade_fix_mode.Set(index_out);
        }, m_downgrade_fix_mode.Get(), "When installing a lower firmware: Automatic deletes the system save 8000000000000073, Optional asks, Off never deletes it."_i18n);

        // run the downgrade fix on its own so it can be tested in isolation.
        options->Add<SidebarEntryCallback>("Apply downgrade fix"_i18n, [](){
            App::Push<OptionBox>(
                "Apply downgrade fix now?\n\nThis will reboot into TegraExplorer to delete the system save 8000000000000073."_i18n,
                "Cancel"_i18n, "Apply"_i18n, 0,
                [](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }
                    if (!detail::StageAndLaunchDowngradeFix(App::IsEmummc())) {
                        App::Push<OptionBox>("Failed to stage or launch TegraExplorer downgrade fix."_i18n, "OK"_i18n);
                    }
                });
        }, "Delete system save 8000000000000073 (via TegraExplorer)."_i18n);
    }
}

void Menu::OnLayoutChange() {
    auto make_content_pos = [this](float top) {
        auto pos = m_pos;
        pos.y = top;
        pos.h = std::max(0.f, GetY() + GetH() - top);
        return pos;
    };

    Vec4 content_pos{};
    if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles) {
        constexpr float x = 75.f;
        constexpr float tile_w = 370.f;
        constexpr float tile_h = 155.f;
        constexpr float x_gap = 10.f;
        constexpr float y_gap = 65.f;
        content_pos = make_content_pos(GetY() + UPDATER_TILE_CLIP_TOP_OFFSET);
        const Vec4 v{x, GetY() + UPDATER_TILE_TOP_OFFSET, tile_w, tile_h};
        m_list = std::make_unique<List>(3, 2 * 3, content_pos, v, Vec2{x_gap, y_gap});
    } else {
        content_pos = make_content_pos(GetY() + UPDATER_LIST_TOP_OFFSET);
        const Vec4 v{75.f, GetY() + UPDATER_LIST_TOP_OFFSET, 1220.f - 150.f, UPDATER_LIST_ROW_HEIGHT};
        m_list = std::make_unique<List>(1, UPDATER_LIST_PAGE_ROWS, content_pos, v, Vec2{0.f, UPDATER_LIST_ROW_GAP});
    }
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetScrollBarPos(m_pos.x + m_pos.w, content_pos.y, content_pos.h);

    m_tile_entries = TileSlots(m_entries);
    const auto it = std::ranges::find(m_tile_entries, m_index);
    m_tile_index = it == m_tile_entries.end() ? 0 : std::distance(m_tile_entries.begin(), it);
    EnsureTileVisible();
}

void Menu::EnsureTileVisible() {
    if (!m_list || static_cast<UpdaterViewMode>(m_view_mode.Get()) != UpdaterViewMode::Tiles || m_tile_entries.empty()) {
        return;
    }

    constexpr s64 visible_rows = 2;
    const auto row = m_tile_index / TILE_COLUMNS;
    const auto first_visible_row = static_cast<s64>(m_list->GetYoff() / m_list->GetMaxY());

    if (row < first_visible_row) {
        m_list->SetYoff(static_cast<float>(row) * m_list->GetMaxY());
    } else if (row >= first_visible_row + visible_rows) {
        m_list->SetYoff(static_cast<float>(row - visible_rows + 1) * m_list->GetMaxY());
    }
}

bool Menu::MoveTileSelection(s64 step) {
    if (m_tile_entries.empty()) {
        return false;
    }

    const auto old_index = m_tile_index;
    const auto size = static_cast<s64>(m_tile_entries.size());
    // wraps rather than clamps, so the tile grid scrolls round the same way
    // every other list in the app does.
    const auto target = ((m_tile_index + step) % size + size) % size;
    const auto next_index = ResolveTileSlotIndex(m_tile_entries, target, m_tile_index);
    if (next_index == old_index || m_tile_entries[next_index] == TILE_EMPTY) {
        return false;
    }

    m_tile_index = next_index;
    SetIndex(m_tile_entries[m_tile_index]);
    App::PlaySoundEffect(SoundEffect_Focus);
    return true;
}

void Menu::FetchLinks() {
    m_loading = true;
    m_error_message.clear();
    m_entries.clear();
    m_tile_entries.clear();
    m_latest_kefir = "Unknown";
    BuildSectionedEntries(m_entries, {});
    m_tile_entries = TileSlots(m_entries);
    m_index = 0;
    SetIndex(0);

    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_DIR);

    curl::Api().ToFileAsync(
        curl::Url{NXLINKS_URL},
        curl::Path{NXLINKS_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            std::vector<UpdaterEntry> entries;
            std::string latest_kefir;
            if (!result.success || !ParseUpdaterLinks(result.path, entries, latest_kefir)) {
                m_retry_on_connect = !net::IsConnectedCached();
                m_error_message = "Failed to load updater lists.";
                m_index = 0;
                SetIndex(0);
                return false;
            }

            m_retry_on_connect = false;
            BuildSectionedEntries(m_entries, entries);
            m_tile_entries = TileSlots(m_entries);
            m_latest_kefir = latest_kefir.empty() ? "Unknown" : latest_kefir;

            if (entries.empty()) {
                m_error_message = "No Kefir or firmware downloads found.";
            }

            m_index = 0;
            SetIndex(0);
            return true;
        }}
    );
}

void Menu::SetIndex(s64 index) {
    m_index = ResolveSelectableIndex(m_entries, index, m_index);
    if (m_list) {
        if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::List) {
            const auto max = UPDATER_LIST_ROW_HEIGHT + UPDATER_LIST_ROW_GAP;
            const auto start = static_cast<s64>(m_list->GetYoff() / max);
            if (m_index < start) {
                m_list->SetYoff(m_index * max);
            } else if (m_index >= start + UPDATER_LIST_PAGE_ROWS) {
                m_list->SetYoff((m_index - UPDATER_LIST_PAGE_ROWS + 1) * max);
            }
        }
    }

    if (m_index <= 1) {
        m_list->SetYoff(0);
    }

    const auto it = std::ranges::find(m_tile_entries, m_index);
    if (it != m_tile_entries.end()) {
        m_tile_index = std::distance(m_tile_entries.begin(), it);
        EnsureTileVisible();
    }

    UpdateSubheading();
}

void Menu::UpdateSubheading() {
    const auto count = detail::SelectableCount(m_entries);
    if (m_entries.empty() || !count) {
        this->SetSubHeading("0 / 0");
        return;
    }

    const auto& entry = m_entries[m_index];
    this->SetSubHeading(std::to_string(detail::SelectablePosition(m_entries, m_index)) + " / " + std::to_string(count) + " - " + detail::TypeLabel(entry.type));
}

void Menu::RefreshSystemInfo() {
    m_current_kefir = detail::ReadFirstLine(KEFIR_VERSION_PATH);
    m_supported_firmware = detail::ReadCurrentKefirSupportedFirmware();
    m_current_firmware = hats::getSystemFirmware();
    m_console_revision = hats::isErista() ? "Erista (v1)" : "Mariko (v2)";
    if (m_latest_kefir.empty()) {
        m_latest_kefir = "Unknown";
    }
}

bool Menu::IsDowngrade(const std::string& target_version) const {
    return detail::IsVersionLower(target_version, m_current_firmware);
}

bool Menu::IsFirmwareSupported(const std::string& target_version) const {
    if (!detail::IsKnownVersion(m_supported_firmware)) {
        return true;
    }

    return !detail::IsVersionLower(m_supported_firmware, target_version);
}

bool Menu::IsKefirUpdate(const UpdaterEntry& entry) const {
    if (entry.type != UpdaterEntryType::Kefir) {
        return false;
    }
    if (!detail::IsKnownVersion(m_current_kefir)) {
        return false;
    }
    const auto target = detail::ExtractKefirVersion(entry.name, entry.url);
    if (!detail::IsKnownVersion(target)) {
        return false;
    }
    return detail::IsVersionLower(m_current_kefir, target);
}

bool Menu::FindKefirUpdate(UpdaterEntry& out) const {
    for (const auto& entry : m_entries) {
        if (entry.type == UpdaterEntryType::Kefir && entry.pack) {
            out = entry;
            return true;
        }
    }

    for (const auto& entry : m_entries) {
        if (entry.type == UpdaterEntryType::Kefir) {
            out = entry;
            return true;
        }
    }

    return false;
}

} // namespace sphaira::ui::menu::kefir
