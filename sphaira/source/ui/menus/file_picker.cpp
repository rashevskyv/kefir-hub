#include "ui/menus/file_picker.hpp"
#include "path_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/error_box.hpp"
#include "ui/file_icon.hpp"
#include "ui/menus/file_viewer.hpp"

#include "log.hpp"
#include "app.hpp"
#include "image.hpp"
#include "ui/nvg_util.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "minizip_helper.hpp"

#include <minIni.h>
#include <cstring>
#include <cassert>
#include <string>
#include <string_view>
#include <ctime>
#include <span>
#include <utility>
#include <ranges>
#include <algorithm>

namespace sphaira::ui::menu::filepicker {
namespace {

constexpr FsEntry FS_ENTRY_DEFAULT{
    "microSD card", "/", FsType::Sd, FsEntryFlag_Assoc,
};

constexpr FsEntry FS_ENTRIES[]{
    FS_ENTRY_DEFAULT,
};

// namespace
} // namespace

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff();
    }

    if (IsSd() && !m_entries_current.empty() && !GetEntry().checked_internal_extension && path::EqualsIC(GetEntry().extension, "zip")) {
        GetEntry().checked_internal_extension = true;

        TimeStamp ts;
        fs::FsPath filename_inzip{};
        if (R_SUCCEEDED(mz::PeekFirstFileName(GetFs(), GetNewPathCurrent(), filename_inzip))) {
            if (auto ext = std::strrchr(filename_inzip, '.')) {
                GetEntry().internal_name = filename_inzip.toString();
                GetEntry().internal_extension = ext+1;
            }
            log_write("\tzip, time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
        }
    }

    UpdateSubheading();
}

auto Menu::Scan(const fs::FsPath& new_path, bool is_walk_up) -> Result {
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    log_write("new scan path: %s\n", new_path.s);
    if (!is_walk_up && !m_path.empty() && !m_entries_current.empty()) {
        const LastFile f(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
        m_previous_highlighted_file.emplace_back(f);
    }

    m_path = new_path;
    FreeThumbs();
    m_entries.clear();
    m_index = 0;
    m_list->SetYoff(0);
    SetTitleSubHeading(m_path, true);

    fs::Dir d;
    R_TRY(m_fs->OpenDirectory(new_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d));

    // we won't run out of memory here (tm)
    std::vector<FsDirectoryEntry> dir_entries;
    R_TRY(d.ReadAll(dir_entries));

    const auto count = dir_entries.size();
    m_entries.reserve(count);

    m_entries_index.clear();
    m_entries_index_hidden.clear();

    m_entries_index.reserve(count);
    m_entries_index_hidden.reserve(count);

    u32 i = 0;
    for (const auto& e : dir_entries) {
        m_entries_index_hidden.emplace_back(i);

        bool hidden = false;
        // check if we have a filter.
        if (e.type == FsDirEntryType_File && !m_filter.empty()) {
            hidden = true;
            if (const auto ext = std::strrchr(e.name, '.')) {
                const char* ext_clean = (*ext == '.') ? ext + 1 : ext;
                for (const auto& filter : m_filter) {
                    const char* filter_clean = (!filter.empty() && filter[0] == '.') ? filter.c_str() + 1 : filter.c_str();
                    if (path::EqualsIC(ext_clean, filter_clean)) {
                        hidden = false;
                        break;
                    }
                }
            }
        }

        if (!hidden) {
            m_entries_index.emplace_back(i);
        }

        m_entries.emplace_back(e);
        i++;
    }
    m_thumbs.assign(m_entries.size(), 0);
    m_mosaics.assign(m_entries.size(), {});

    Sort();
    SetIndex(0);

    // find previous entry
    if (is_walk_up && !m_previous_highlighted_file.empty()) {
        ON_SCOPE_EXIT(m_previous_highlighted_file.pop_back());
        SetIndexFromLastFile(m_previous_highlighted_file.back());
    }

    log_write("finished scan\n");
    R_SUCCEED();
}

void Menu::Sort() {
    // returns true if lhs should be before rhs
    const auto sort = m_sort.Get();
    const auto order = m_order.Get();
    const auto folders_first = m_folders_first.Get();
    const auto hidden_last = m_hidden_last.Get();

    const auto sorter = [this, sort, order, folders_first, hidden_last](u32 _lhs, u32 _rhs) -> bool {
        const auto& lhs = m_entries[_lhs];
        const auto& rhs = m_entries[_rhs];

        if (hidden_last) {
            if (lhs.IsHidden() && !rhs.IsHidden()) {
                return false;
            } else if (!lhs.IsHidden() && rhs.IsHidden()) {
                return true;
            }
        }

        if (folders_first) {
            if (lhs.type == FsDirEntryType_Dir && !(rhs.type == FsDirEntryType_Dir)) { // left is folder
                return true;
            } else if (!(lhs.type == FsDirEntryType_Dir) && rhs.type == FsDirEntryType_Dir) { // right is folder
                return false;
            }
        }

        switch (sort) {
            case SortType_Size: {
                if (lhs.file_size == rhs.file_size) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else if (order == OrderType_Descending) {
                    return lhs.file_size > rhs.file_size;
                } else {
                    return lhs.file_size < rhs.file_size;
                }
            } break;
            case SortType_Alphabetical: {
                if (order == OrderType_Descending) {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) < 0;
                } else {
                    return strncasecmp(lhs.name, rhs.name, sizeof(lhs.name)) > 0;
                }
            } break;
        }

        std::unreachable();
    };

    if (m_show_hidden.Get()) {
        m_entries_current = m_entries_index_hidden;
    } else {
        m_entries_current = m_entries_index;
    }

    std::sort(m_entries_current.begin(), m_entries_current.end(), sorter);
}

void Menu::SortAndFindLastFile(bool scan) {
    std::optional<LastFile> last_file;
    if (!m_path.empty() && !m_entries_current.empty()) {
        last_file = LastFile(GetEntry().name, m_index, m_list->GetYoff(), m_entries_current.size());
    }

    if (scan) {
        Scan(m_path);
    } else {
        Sort();
    }

    if (last_file.has_value()) {
        SetIndexFromLastFile(*last_file);
    }
}

void Menu::SetIndexFromLastFile(const LastFile& last_file) {
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries_current.size(); i++) {
        if (last_file.name == GetEntry(i).name) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        if (index == last_file.index && m_entries_current.size() == last_file.entries_count) {
            m_list->SetYoff(last_file.offset);
            log_write("index is the same as last time\n");
        } else {
            // file position changed!
            log_write("file position changed\n");
            // guesstimate where the position is
            if (index >= 8) {
                m_list->SetYoff(((index - 8) + 1) * m_list->GetMaxY());
            } else {
                m_list->SetYoff(0);
            }
        }
        SetIndex(index);
    }
}

void Menu::SetFs(const fs::FsPath& new_path, const FsEntry& new_entry) {
    if (m_fs && m_fs_entry.root == new_entry.root && m_fs_entry.type == new_entry.type) {
        log_write("same fs, ignoring\n");
        return;
    }

    // m_fs.reset();
    m_path = new_path;
    m_entries.clear();
    m_entries_index.clear();
    m_entries_index_hidden.clear();
    m_entries_current = {};
    m_previous_highlighted_file.clear();
    m_fs_entry = new_entry;

    switch (new_entry.type) {
         case FsType::Sd:
            m_fs = std::make_unique<fs::FsNativeSd>(m_ignore_read_only.Get());
            break;
        case FsType::ImageNand:
            m_fs = std::make_unique<fs::FsNativeImage>(FsImageDirectoryId_Nand);
            break;
        case FsType::ImageSd:
            m_fs = std::make_unique<fs::FsNativeImage>(FsImageDirectoryId_Sd);
            break;
        case FsType::Stdio:
            m_fs = std::make_unique<fs::FsStdio>(true, new_entry.root);
            break;
        default: // the picker only offers the storages above
            break;
    }

    if (HasFocus()) {
        if (m_path.empty()) {
            Scan(m_fs->Root());
        } else {
            Scan(m_path);
        }
    }
}


Menu::Menu(const Callback& cb, const std::vector<std::string>& filter, const fs::FsPath& path)
: Menu{[cb](const fs::FsPath& selected_path, const FsEntry&) {
    return cb(selected_path);
}, filter, path, false} {
}

Menu::Menu(const LocationCallback& cb, const std::vector<std::string>& filter, const fs::FsPath& path, bool pick_directory)
: MenuBase{"FilePicker"_i18n, MenuFlag_None}
, m_callback{cb}
, m_filter{filter}
, m_pick_directory{pick_directory} {
    FsEntry entry = FS_ENTRY_DEFAULT;

    if (!IsTab()) {
        SetAction(Button::SELECT, Action{App::HandleMinus});
    }

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (m_entries_current.empty()) {
                return;
            }

            const auto& entry = GetEntry();

            if (entry.type == FsDirEntryType_Dir) {
                Scan(GetNewPathCurrent());
            } else if (!m_pick_directory) {
                if (IsImagePicker()) {
                    OpenPreview();
                } else if (m_callback(GetNewPathCurrent(), m_fs_entry)) {
                    SetPop();
                }
            }
        }}),

        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (!IsTab() && App::GetApp()->m_controller.GotHeld(Button::R2)) {
                PromptIfShouldExit();
                return;
            }

            std::string_view view{m_path};
            if (view != m_fs->Root()) {
                const auto end = view.find_last_of('/');
                assert(end != view.npos);

                if (end == 0) {
                    Scan(m_fs->Root(), true);
                } else {
                    Scan(view.substr(0, end), true);
                }
            } else {
                if (!IsTab()) {
                    PromptIfShouldExit();
                }
            }
        }}),

        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    if (m_pick_directory) {
        SetAction(Button::Y, Action{"Use Folder"_i18n, [this](){
            if (m_callback(m_path, m_fs_entry)) {
                SetPop();
            }
        }});
    } else if (IsImagePicker()) {
        SetAction(Button::Y, Action{"Use this image"_i18n, [this](){
            UseCurrentFile();
        }});
    }

    ApplyLayout();

    auto buf = path;
    if (path.empty()) {
        ini_gets(INI_SECTION, "last_path", entry.root, buf, sizeof(buf), App::CONFIG_PATH);
    }

    SetFs(buf, entry);
}

Menu::~Menu() {
    FreeThumbs();
    // don't store mount points for non-sd card paths.
    if (IsSd()) {
        ini_puts(INI_SECTION, "last_path", m_path, App::CONFIG_PATH);

        // save last selected file.
        if (!m_entries.empty()) {
            ini_puts(INI_SECTION, "last_file", GetEntry().name, App::CONFIG_PATH);
        }
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    m_list->OnUpdate(controller, touch, m_index, m_entries_current.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}



void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (m_entries.empty()) {
        auto path = m_path.empty() ? m_fs->Root() : m_path;
        if (m_fs && !m_fs->DirExists(path)) {
            path = m_fs->Root();
        }
        if (R_FAILED(Scan(path))) {
            Scan(m_fs->Root());
        }

        if (IsSd() && !m_entries.empty()) {
            LastFile last_file{};
            if (ini_gets(INI_SECTION, "last_file", "", last_file.name, sizeof(last_file.name), App::CONFIG_PATH)) {
                SetIndexFromLastFile(last_file);
            }
        }
    }
}

void Menu::UpdateSubheading() {
    const auto index = m_entries_current.empty() ? 0 : m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_entries_current.size()));
}

void Menu::PromptIfShouldExit() {
    if (IsTab()) {
        return;
    }

    App::Push<ui::OptionBox>(
        "Close File Picker?"_i18n,
        "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
            if (op_index && *op_index) {
                SetPop();
            }
        }
    );
}

} // namespace sphaira::ui::menu::filepicker
