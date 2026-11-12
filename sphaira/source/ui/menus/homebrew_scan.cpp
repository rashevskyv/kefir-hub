#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/homebrew_internal.hpp"
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

} // namespace

auto ShouldSignalChange(std::string_view path, bool is_directory) -> bool {
    const auto search_paths = LoadSearchPaths();
    return path::PathAffectsHomebrew(path, search_paths, is_directory);
}

void NotifyFileCreated(std::string_view path) {
    if (ShouldSignalChange(path, false)) {
        SignalChange();
    }
}

void NotifyFileDeleted(std::string_view path) {
    if (ShouldSignalChange(path, false)) {
        SignalChange();
    }
}

void NotifyDirectoryCreated(std::string_view path) {
    if (ShouldSignalChange(path, true)) {
        SignalChange();
    }
}

void NotifyDirectoryDeleted(std::string_view path) {
    if (ShouldSignalChange(path, true)) {
        SignalChange();
    }
}

void NotifyRename(std::string_view old_path, std::string_view new_path, bool is_directory) {
    if (ShouldSignalChange(old_path, is_directory) || ShouldSignalChange(new_path, is_directory)) {
        SignalChange();
    }
}

void NotifyPathChanged(std::string_view path, bool is_directory) {
    if (ShouldSignalChange(path, is_directory)) {
        SignalChange();
    }
}


auto GetSearchPaths() -> std::vector<std::string> {
    return LoadSearchPaths();
}

auto GetShareableHomebrewRoots() -> std::vector<std::string> {
    std::vector<std::string> roots;
    auto add_unique = [&](const std::string& p) {
        if (p.empty()) {
            return;
        }
        for (const auto& existing : roots) {
            if (path::EqualsIC(existing, p)) {
                return;
            }
        }
        roots.push_back(p);
    };

    add_unique(DEFAULT_SEARCH_PATH);
    for (const auto& extra : LoadSearchPaths()) {
        add_unique(extra);
    }
    return roots;
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

    // Kefir no longer ships Kefir Updater: keep its tile, A opens the Hub's own updater page
    if (std::ranges::none_of(m_entries, [](const auto& entry) {
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

    this->LoadFolders();
    this->BuildView();
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
        const auto& lhs = Entry(_lhs);
        const auto& rhs = Entry(_rhs);

        // folders first, by name, whatever the sort
        if (lhs.is_dir || rhs.is_dir) {
            if (lhs.is_dir != rhs.is_dir) {
                return lhs.is_dir;
            }
            return strcasecmp(lhs.GetName(), rhs.GetName()) < 0;
        }

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

    for (auto& p : m_folders) {
        FreeEntry(vg, p);
    }

    m_entries.clear();
    m_folders.clear();
    for (auto& e : m_entries_index) {
        e.clear();
    }
    m_selected_count = 0;
}


} // namespace sphaira::ui::menu::homebrew
