#include "ui/menus/uninstaller_menu.hpp"
#include "meminfo.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/sidebar.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "download.hpp"
#include "utils/utils.hpp"
#include <yyjson.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

UninstallerMenu::UninstallerMenu() : MenuBase{"Module Manager"_i18n, MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Toggle"_i18n, [this](){
            ToggleSelectedModule();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::Y, Action{"Autostart"_i18n, [this](){
            ToggleSelectedAutostart();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            LoadModules();
            RequestCatalogUpdate(true);
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            ShowContextMenu();
        }}),
        std::make_pair(Button::SELECT, Action{"Info"_i18n, [this](){
            ShowInfo();
        }})
    );

    const float list_x = 75.f;
    const float list_y = GetY() + 44.f;
    const float list_w = 1070.f;
    const float list_h = 575.f;
    const float row_h = 79.f;

    m_list = std::make_unique<List>(1, 7, Vec4{list_x, list_y, list_w, list_h}, Vec4{list_x, list_y, list_w, row_h});
    m_list->SetLayout(List::Layout::GRID);
}

UninstallerMenu::~UninstallerMenu() = default;

void UninstallerMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_view.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_view.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void UninstallerMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    DrawRamPanel(vg, theme);

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
        return;
    }

    if (m_items.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No sysmodules found"_i18n.c_str());
        return;
    }

    if (m_view.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No modules match this filter"_i18n.c_str());
        return;
    }

    nvgSave(vg);
    const float list_x = 75.f;
    const float list_y = GetY() + 44.f;
    const float list_w = 1070.f;
    const float list_h = 575.f;
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, list_x - p, list_y - p, list_w + p * 2, list_h + p * 2);

    m_list->Draw(vg, theme, m_view.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[m_view[i]];
        const auto selected = m_index == i;

        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_view.size() - 1) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const auto on_colour = nvgRGBA(76, 190, 120, 255);
        const auto off_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawRect(vg, x + 15.f, y + h / 2.f - 7.f, 14.f, 14.f,
            item.running ? on_colour : off_colour, 7.f);

        gfx::drawTextArgs(vg, x + 44.f, y + h / 2.f - 11.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", item.name.c_str());

        gfx::drawTextArgs(vg, x + 44.f, y + h / 2.f + 14.f, 13.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s%s", item.program_id_text.c_str(), item.requires_reboot ? " - Applies after reboot"_i18n.c_str() : "");

        if (item.running && item.memory_bytes) {
            gfx::drawTextArgs(vg, x + w - 15.f, y + h / 2.f - 11.f, 18.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(text_id),
                "%s", utils::formatSizeStorage(item.memory_bytes).c_str());
        }

        const auto reboot_text = item.autostart ? "After reboot: Enabled"_i18n : "After reboot: Disabled"_i18n;
        const auto reboot_colour = item.autostart ? on_colour : off_colour;
        gfx::drawTextArgs(vg, x + w - 15.f, y + h / 2.f + 14.f, 14.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            reboot_colour,
            "%s", reboot_text.c_str());
    });

    nvgRestore(vg);
}

void UninstallerMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded) {
        LoadModules();
    } else {
        RefreshStatuses();
    }
    RequestCatalogUpdate();
}

void UninstallerMenu::SetIndex(s64 index) {
    if (m_view.empty()) {
        m_index = 0;
        SetTitleSubHeading(m_items.empty()
            ? "No sysmodules found"_i18n
            : "No modules match this filter"_i18n, true);
        SetSubHeading("");
        return;
    }

    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_view.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    UpdateSubheading();
}


void UninstallerMenu::DrawRamPanel(NVGcontext* vg, Theme* theme) {
    u32 running{};
    for (const auto& item : m_items) {
        if (item.running) {
            running++;
        }
    }
    meminfo::DrawSystemPool(vg, theme, 80.f, GetY() + 6.f, m_system, running,
        static_cast<u32>(m_items.size()));
}

void UninstallerMenu::UpdateSubheading() {
    if (!HasCurrent()) {
        SetTitleSubHeading(m_items.empty()
            ? "No sysmodules found"_i18n
            : "No modules match this filter"_i18n, true);
        SetSubHeading("");
        return;
    }

    SetTitleSubHeading(Current().description, true);
    SetSubHeading("");
}

auto UninstallerMenu::MatchesFilter(const ModuleItem& item) const -> bool {
    switch (m_filter) {
        case ModuleFilter::Running:
            return item.running;
        case ModuleFilter::Stopped:
            return !item.running;
        case ModuleFilter::Autostart:
            return item.autostart;
        case ModuleFilter::NeedsReboot:
            return item.requires_reboot;
        case ModuleFilter::All:
        default:
            return true;
    }
}

auto UninstallerMenu::HasCurrent() const -> bool {
    return m_index >= 0 && m_index < static_cast<s64>(m_view.size());
}

auto UninstallerMenu::Current() -> ModuleItem& {
    return m_items[m_view[m_index]];
}

auto UninstallerMenu::Current() const -> const ModuleItem& {
    return m_items[m_view[m_index]];
}

void UninstallerMenu::RebuildView(u64 keep_program_id) {
    if (!keep_program_id && HasCurrent()) {
        keep_program_id = Current().program_id;
    }

    m_view.clear();
    for (s64 i = 0; i < static_cast<s64>(m_items.size()); i++) {
        if (MatchesFilter(m_items[i])) {
            m_view.push_back(i);
        }
    }

    s64 index = 0;
    if (keep_program_id) {
        for (s64 i = 0; i < static_cast<s64>(m_view.size()); i++) {
            if (m_items[m_view[i]].program_id == keep_program_id) {
                index = i;
                break;
            }
        }
    }
    SetIndex(index);
}

void UninstallerMenu::SortItems(u64 keep_program_id) {
    if (!keep_program_id && HasCurrent()) {
        keep_program_id = Current().program_id;
    }

    std::sort(m_items.begin(), m_items.end(), [this](const ModuleItem& a, const ModuleItem& b) {
        switch (m_sort) {
            case ModuleSort::Running:
                if (a.running != b.running) {
                    return a.running && !b.running;
                }
                break;
            case ModuleSort::Autostart:
                if (a.autostart != b.autostart) {
                    return a.autostart && !b.autostart;
                }
                break;
            case ModuleSort::Name:
            default:
                break;
        }
        if (a.requires_reboot != b.requires_reboot) {
            return !a.requires_reboot;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    RebuildView(keep_program_id);
}

void UninstallerMenu::ShowContextMenu() {
    auto options = std::make_unique<Sidebar>(
        HasCurrent() ? Current().name : "Module Manager"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (HasCurrent()) {
        options->Add<SidebarEntryCallback>(Current().running ? "Stop"_i18n : "Start"_i18n, [this](){
            ToggleSelectedModule();
        }, true, "Start or stop this module now."_i18n);
        options->Add<SidebarEntryCallback>("Autostart"_i18n, [this](){
            ToggleSelectedAutostart();
        }, true, "Launch this module when the console boots."_i18n);
        options->Add<SidebarEntryCallback>("Info"_i18n, [this](){
            ShowInfo();
        }, true, "Name, memory and GitHub description."_i18n);
    }
    options->Add<SidebarEntryCallback>("Filter"_i18n, [this](){
        ShowFilterMenu();
    }, "Show all, running, stopped, autostart or reboot-required modules."_i18n);
    options->Add<SidebarEntryCallback>("Sort"_i18n, [this](){
        ShowSortMenu();
    }, "Order the list by name, status or autostart."_i18n);
}

void UninstallerMenu::ShowSortMenu() {
    PopupList::Items choices = {
        "Name"_i18n,
        "Running"_i18n,
        "Autostart"_i18n,
    };
    App::Push<PopupList>("Sort"_i18n, std::move(choices), [this](std::optional<s64> op){
        if (!op) {
            return;
        }
        m_sort = static_cast<ModuleSort>(*op);
        SortItems();
    }, static_cast<s64>(m_sort));
}

void UninstallerMenu::ShowFilterMenu() {
    PopupList::Items choices = {
        "All"_i18n,
        "Running"_i18n,
        "Stopped"_i18n,
        "Autostart"_i18n,
        "Applies after reboot"_i18n,
    };
    App::Push<PopupList>("Filter"_i18n, std::move(choices), [this](std::optional<s64> op){
        if (!op) {
            return;
        }
        m_filter = static_cast<ModuleFilter>(*op);
        RebuildView();
    }, static_cast<s64>(m_filter));
}

void UninstallerMenu::ShowInfo() {
    if (!HasCurrent()) {
        return;
    }

    auto& item = Current();
    if (!item.github_description.empty() || item.repository.empty()) {
        ShowInfoBox(item);
        return;
    }

    const auto parsed = path::ParseGitHubRepoUrl(item.repository);
    if (!parsed) {
        ShowInfoBox(item);
        return;
    }

    const auto url = "https://api.github.com/repos/" + parsed->owner + "/" + parsed->repo;
    const auto program_id = item.program_id;
    App::Notify("Loading..."_i18n);
    curl::Api().ToMemoryAsync(
        curl::Url{url},
        curl::Header{{"Accept", "application/vnd.github+json"}},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, program_id](auto& result) {
            ModuleItem* item{};
            for (auto& candidate : m_items) {
                if (candidate.program_id == program_id) {
                    item = &candidate;
                    break;
                }
            }
            if (!item) {
                return;
            }
            if (result.success && !result.data.empty()) {
                auto* doc = yyjson_read(reinterpret_cast<const char*>(result.data.data()), result.data.size(), YYJSON_READ_NOFLAG);
                if (doc) {
                    ON_SCOPE_EXIT(yyjson_doc_free(doc));
                    auto* root = yyjson_doc_get_root(doc);
                    auto* desc = root && yyjson_is_obj(root) ? yyjson_obj_get(root, "description") : nullptr;
                    if (desc && yyjson_is_str(desc) && yyjson_get_str(desc) && *yyjson_get_str(desc)) {
                        item->github_description = yyjson_get_str(desc);
                    }
                }
            }
            if (item->github_description.empty()) {
                item->github_description = item->description;
            }
            ShowInfoBox(*item);
        }}
    );
}

void UninstallerMenu::ShowInfoBox(const ModuleItem& item) {
    std::string msg = item.name + "\n" + item.program_id_text + "\n";
    msg += (item.running ? "Now: On"_i18n : "Now: Off"_i18n);
    if (item.running && item.memory_bytes) {
        msg += "  " + utils::formatSizeStorage(item.memory_bytes);
    }
    msg += "\n";
    msg += item.autostart ? "After reboot: Enabled"_i18n : "After reboot: Disabled"_i18n;
    msg += "\n";
    if (!item.repository.empty()) {
        msg += item.repository + "\n";
    }
    msg += "\n";
    if (!item.github_description.empty()) {
        msg += item.github_description;
    } else {
        msg += item.description;
    }
    App::Push<OptionBox>(msg, "OK"_i18n);
}

} // namespace sphaira::ui::menu::hats
