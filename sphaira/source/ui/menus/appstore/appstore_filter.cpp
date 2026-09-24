#include "ui/menus/appstore.hpp"
#include "ui/menus/appstore/appstore_internal.hpp"
#include "ui/menus/appstore_util.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "nro.hpp"
#include "nacp_util.hpp"
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ranges>
#include <vector>

namespace sphaira::ui::menu::appstore {
void Menu::ScanHomebrew() {
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    from_json(REPO_PATH, m_entries);

    fs::FsNativeSd fs;
    if (R_FAILED(fs.GetFsOpenResult())) {
        log_write("failed to open sd card in appstore scan\n");
        return;
    }

    // pre-allocate the max size, can shrink later if needed
    for (auto& index : m_entries_index) {
        index.reserve(m_entries.size());
    }

    for (u32 i = 0; i < m_entries.size(); i++) {
        auto& e = m_entries[i];

        m_entries_index[Filter_All].push_back(i);

        if (e.category == std::string_view{"game"}) {
            m_entries_index[Filter_Games].push_back(i);
        } else if (e.category == std::string_view{"emu"}) {
            m_entries_index[Filter_Emulators].push_back(i);
        } else if (e.category == std::string_view{"tool"}) {
            m_entries_index[Filter_Tools].push_back(i);
        } else if (e.category == std::string_view{"advanced"}) {
            m_entries_index[Filter_Advanced].push_back(i);
        } else if (e.category == std::string_view{"theme"}) {
            m_entries_index[Filter_Themes].push_back(i);
        } else if (e.category == std::string_view{"legacy"}) {
            m_entries_index[Filter_Legacy].push_back(i);
        } else {
            m_entries_index[Filter_Misc].push_back(i);
        }

        // fwiw, this is how N stores update info
        e.updated_num = std::atoi(e.updated.c_str()); // day
        e.updated_num += std::atoi(e.updated.c_str() + 3) * 100; // month
        e.updated_num += std::atoi(e.updated.c_str() + 6) * 100 * 100; // year

        e.status = EntryStatus::Get;
        // if binary is present, check for it, if not avalible, report as not installed
        // if there is not a binary path, then we have to trust the info.json
        // this can result in applications being shown as installed even though they
        // are deleted, this includes sys-modules.
        if (e.binary.empty() || e.binary == "none") {
            ReadFromInfoJson(e);
        } else {
            if (fs.FileExists(e.binary)) {
                // first check the info.json
                ReadFromInfoJson(e);
                // if we didn't get an installed_version from info.json, try reading it from NACP
                if (e.installed_version.empty()) {
                    NacpStruct nacp;
                    if (R_SUCCEEDED(nro_get_nacp(e.binary, nacp))) {
                        e.installed_version = nacp_util::GetDisplayVersion(nacp);
                    }
                }
                // if we get here, this means that we have the file, but not the .info file
                // report the file as locally installed to match hb-appstore.
                if (e.status == EntryStatus::Get) {
                    if (IsRetroArchPackage(e)) {
                        e.status = EntryStatus::Update;
                    } else {
                        // filter out some apps.
                        bool filtered{};

                        // ignore hbmenu if it was replaced with sphaira.
                        if (e.name == "hbmenu") {
                            NacpStruct nacp;
                            if (R_SUCCEEDED(nro_get_nacp(e.binary, nacp))) {
                                filtered = std::strcmp(nacp_util::GetName(nacp), "nx-hbmenu");
                            }
                        }
                        // ignore single retroarch core.
                        else if (e.name == "snes9x_2010") {
                            filtered = true;
                        }
                        // todo: filter
                        // - sys-clk

                        if (!filtered) {
                            e.status = EntryStatus::Local;
                        } else {
                            log_write("filtered: %s path: %s\n", e.name.c_str(), e.binary.c_str());
                        }
                    }
                }
            }
        }

        e.image.state = ImageDownloadState::None;
        e.image.image = 0; // images are lazy loaded
    }

    for (auto& index : m_entries_index) {
        index.shrink_to_fit();
    }

    SetFilter();
    SetIndex(0);
    Sort();
}

void Menu::Sort() {
    // log_write("doing sort: size: %zu count: %zu\n", repo_json.size(), m_entries.size());

    const auto sort = m_sort.Get();
    const auto order = m_order.Get();
    const auto filter = m_filter.Get();

    // returns true if lhs should be before rhs
    const auto sorter = [this, sort, order](EntryMini _lhs, EntryMini _rhs) -> bool {
        const auto& lhs = m_entries[_lhs];
        const auto& rhs = m_entries[_rhs];

        // fallback to name compare if the updated num is the same
        if (lhs.status == EntryStatus::Update && !(rhs.status == EntryStatus::Update)) {
            return true;
        } else if (!(lhs.status == EntryStatus::Update) && rhs.status == EntryStatus::Update) {
            return false;
        } else if (lhs.status == EntryStatus::Installed && !(rhs.status == EntryStatus::Installed)) {
            return true;
        } else if (!(lhs.status == EntryStatus::Installed) && rhs.status == EntryStatus::Installed) {
            return false;
        } else if (lhs.status == EntryStatus::Local && !(rhs.status == EntryStatus::Local)) {
            return true;
        } else if (!(lhs.status == EntryStatus::Local) && rhs.status == EntryStatus::Local) {
            return false;
        } else {
            switch (sort) {
                case SortType_Updated: {
                    if (lhs.updated_num == rhs.updated_num) {
                        return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
                    } else if (order == OrderType_Descending) {
                        return lhs.updated_num > rhs.updated_num;
                    } else {
                        return lhs.updated_num < rhs.updated_num;
                    }
                } break;
                case SortType_Downloads: {
                    if (lhs.app_dls == rhs.app_dls) {
                        return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
                    } else if (order == OrderType_Descending) {
                        return lhs.app_dls > rhs.app_dls;
                    } else {
                        return lhs.app_dls < rhs.app_dls;
                    }
                } break;
                case SortType_Size: {
                    if (lhs.extracted == rhs.extracted) {
                        return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
                    } else if (order == OrderType_Descending) {
                        return lhs.extracted > rhs.extracted;
                    } else {
                        return lhs.extracted < rhs.extracted;
                    }
                } break;
                case SortType_Alphabetical: {
                    if (order == OrderType_Descending) {
                        return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) < 0;
                    } else {
                        return strcasecmp(lhs.name.c_str(), rhs.name.c_str()) > 0;
                    }
                } break;
            }

            std::unreachable();
        }
    };


    char subheader[128]{};
    std::snprintf(subheader, sizeof(subheader), "Filter: %s | Sort: %s | Order: %s"_i18n.c_str(), i18n::get(FILTER_STR[filter]).c_str(), i18n::get(SORT_STR[sort]).c_str(), i18n::get(ORDER_STR[order]).c_str());
    SetTitleSubHeading(subheader);

    std::sort(m_entries_current.begin(), m_entries_current.end(), sorter);
}

void Menu::SortAndFindLastFile() {
    const auto name = GetEntry().name;
    Sort();
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (name == GetEntry(i).name) {
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

void Menu::SetFilter() {
    m_is_search = false;
    m_is_author = false;

    m_entries_current = m_entries_index[m_filter.Get()];
    SetIndex(0);
    Sort();
}

void Menu::SetSearch(const std::string& term) {
    if (!m_is_search) {
        m_entry_search_jump_back = m_index;
    }

    m_search_term = term;
    m_entries_index_search.clear();
    const auto query = m_search_term;

    for (u64 i = 0; i < m_entries.size(); i++) {
        const auto& e = m_entries[i];
        if (FindCaseInsensitive(e.title, query) || FindCaseInsensitive(e.author, query) || FindCaseInsensitive(e.description, query)) {
            m_entries_index_search.emplace_back(i);
        }
    }

    m_is_search = true;
    m_entries_current = m_entries_index_search;
    SetIndex(0);
    Sort();
}

void Menu::SetAuthor() {
    if (!m_is_author) {
        m_entry_author_jump_back = m_index;
    }

    m_author_term = m_entries[m_entries_current[m_index]].author;
    m_entries_index_author.clear();
    const auto query = m_author_term;

    for (u64 i = 0; i < m_entries.size(); i++) {
        const auto& e = m_entries[i];
        if (FindCaseInsensitive(e.author, query)) {
            m_entries_index_author.emplace_back(i);
        }
    }

    m_is_author = true;
    m_entries_current = m_entries_index_author;
    SetIndex(0);
    Sort();
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
}


} // namespace sphaira::ui::menu::appstore
