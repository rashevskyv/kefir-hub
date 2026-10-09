#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_ops.hpp"
#include "ui/menus/cheats/cheats_db.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/error_box.hpp"
#include "ui/scrollable_text.hpp"

#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <sstream>
#include <switch.h>
#include <cstdio>
#include <cstring>

namespace sphaira::ui::menu::hats {

using namespace detail;

namespace detail {

auto GetCheatsDirPath(u64 title_id) -> std::string {
    const auto title_id_str = FormatTitleIdLower(title_id);
    return std::string(ATMOSPHERE_CONTENTS_PATH) + "/" + title_id_str + "/" + CHEATS_SUBDIR;
}

auto GetFileStem(const std::string& path) -> std::string {
    const auto slash = path.find_last_of("/\\");
    const auto filename = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = filename.find_last_of('.');
    return dot == std::string::npos ? filename : filename.substr(0, dot);
}

auto GetManualCheatImportPath(u64 title_id, const std::string& build_id) -> fs::FsPath {
    fs::FsPath file_path;
    const auto cheats_dir = GetCheatsDirPath(title_id);
    std::snprintf(file_path, sizeof(file_path), "%s/%s.txt", cheats_dir.c_str(), build_id.c_str());
    return file_path;
}

// Get list of existing cheat files for a title
// Returns map of {build_id: filename}
auto GetExistingCheats(u64 title_id) -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> cheats;
    fs::FsNativeSd fs;

    const auto cheats_dir = GetCheatsDirPath(title_id);
    log_write("[Cheats] Checking for existing cheats in: %s\n", cheats_dir.c_str());

    if (!fs.DirExists(cheats_dir.c_str())) {
        log_write("[Cheats] Cheats directory doesn't exist\n");
        return cheats;
    }

    // Open directory and read entries
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(cheats_dir.c_str(), FsDirOpenMode_ReadFiles, &dir))) {
        log_write("[Cheats] Failed to open cheats directory\n");
        return cheats;
    }

    ON_SCOPE_EXIT(dir.Close());

    s64 count = 0;
    if (R_FAILED(dir.GetEntryCount(&count))) {
        log_write("[Cheats] Failed to get entry count\n");
        return cheats;
    }

    log_write("[Cheats] Found %ld cheat files\n", count);

    std::vector<FsDirectoryEntry> entries(count);
    s64 read_count = 0;
    if (R_FAILED(dir.Read(&read_count, entries.size(), entries.data()))) {
        log_write("[Cheats] Failed to read directory entries\n");
        return cheats;
    }

    for (s64 i = 0; i < read_count; i++) {
        const auto& entry = entries[i];
        if (entry.type == FsDirEntryType_File) {
            // Extract build ID from filename (without .txt extension)
            std::string name = entry.name;
            if (name.length() > 4 && name.substr(name.length() - 4) == ".txt") {
                std::string build_id = name.substr(0, name.length() - 4);
                cheats.push_back({build_id, name});
                log_write("[Cheats] Found cheat: %s (Build ID: %s)\n", name.c_str(), build_id.c_str());
            }
        }
    }

    return cheats;
}

// Delete a specific cheat file
auto DeleteCheatFile(u64 title_id, const std::string& build_id) -> bool {
    fs::FsNativeSd fs;

    const auto cheats_dir = GetCheatsDirPath(title_id);
    fs::FsPath file_path;
    std::snprintf(file_path, sizeof(file_path), "%s/%s.txt", cheats_dir.c_str(), build_id.c_str());

    if (fs.FileExists(file_path)) {
        Result rc = fs.DeleteFile(file_path);
        if (R_FAILED(rc)) {
            log_write("[Cheats] Failed to delete cheat file %s: %x\n", file_path.s, rc);
            return false;
        }
        log_write("[Cheats] Deleted cheat file: %s\n", file_path.s);
        return true;
    }

    return false;
}

auto ResolveManualTargetBuildId(const GameCheatInfo& game, const fs::FsPath* source_path) -> std::string {
    if (IsValidBuildId(game.build_id)) {
        return NormalizeBuildId(game.build_id);
    }

    const auto lookup = LookupBuildIdForCheats(game.title_id);
    if (IsValidBuildId(lookup.build_id)) {
        return NormalizeBuildId(lookup.build_id);
    }

    if (source_path) {
        const auto file_build_id = NormalizeBuildId(GetFileStem(source_path->s));
        if (IsValidBuildId(file_build_id)) {
            return file_build_id;
        }
    }

    return {};
}

} // namespace detail

namespace {

auto RenameCheatBuildId(u64 title_id, const std::string& old_build_id, const std::string& new_build_id, bool overwrite) -> Result {
    const auto old_id = NormalizeBuildId(old_build_id);
    const auto new_id = NormalizeBuildId(new_build_id);
    R_UNLESS(IsValidBuildId(old_id) && IsValidBuildId(new_id), 1);

    fs::FsNativeSd fs;
    const auto cheats_dir = GetCheatsDirPath(title_id);

    fs::FsPath old_path;
    std::snprintf(old_path, sizeof(old_path), "%s/%s.txt", cheats_dir.c_str(), old_id.c_str());

    fs::FsPath new_path;
    std::snprintf(new_path, sizeof(new_path), "%s/%s.txt", cheats_dir.c_str(), new_id.c_str());

    R_UNLESS(fs.FileExists(old_path), 1);

    if (fs.FileExists(new_path)) {
        R_UNLESS(overwrite, FsError_PathAlreadyExists);
        R_TRY(fs.DeleteFile(new_path));
    }

    R_TRY(fs.RenameFile(old_path, new_path));
    R_TRY(fs.Commit());
    R_SUCCEED();
}

} // namespace

// ============================================================
// CheatFilesMenu - View cheat files for a game
// ============================================================

CheatFilesMenu::CheatFilesMenu(const GameCheatInfo& game)
    : MenuBase{"Cheat Files", MenuFlag_None}, m_game(game) {

    LoadCheatFiles();

    this->SetActions(
        std::make_pair(Button::A, Action{"View"_i18n, [this](){
            if (!m_cheats.empty()) {
                OnView();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Delete"_i18n, [this](){
            if (!m_cheats.empty()) {
                OnDelete();
            }
        }}),
        std::make_pair(Button::Y, Action{"Fix BID"_i18n, [this](){
            if (!m_cheats.empty()) {
                OnFixBuildId();
            }
        }})
    );

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatFilesMenu::~CheatFilesMenu() {
}

void CheatFilesMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_cheats.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_cheats.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void CheatFilesMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // Draw game info
    gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "%s (%016lX)", m_game.name.c_str(), m_game.title_id);

    if (m_cheats.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No cheat files found"_i18n.c_str());
        return;
    }

    // Save and restore scissor to clip list drawing area
    nvgSave(vg);
    // Clip area starts below the header text; inflated by the selection outline
    // pad so the highlight of edge rows isn't clipped.
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, 75.f - p, GetY() + 40.f - p, 1220.f - 150.f + p * 2, 720.f - GetY() - 40.f + p * 2);
    ON_SCOPE_EXIT(nvgRestore(vg));

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_cheats.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& cheat = m_cheats[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_cheats.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "Build ID: %s", cheat.build_id.c_str());
    });
}

void CheatFilesMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

void CheatFilesMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatFilesMenu::LoadCheatFiles() {
    m_cheats.clear();

    auto existing = GetExistingCheats(m_game.title_id);
    for (const auto& [build_id, filename] : existing) {
        ExistingCheat cheat;
        cheat.build_id = NormalizeBuildId(build_id);
        cheat.filename = filename;
        cheat.installed = true;
        m_cheats.push_back(cheat);
    }

    if (m_index >= static_cast<s64>(m_cheats.size())) {
        m_index = m_cheats.empty() ? 0 : static_cast<s64>(m_cheats.size()) - 1;
    }
}

void CheatFilesMenu::OnView() {
    if (m_cheats.empty() || m_index >= (s64)m_cheats.size()) {
        return;
    }

    const auto& cheat = m_cheats[m_index];
    const auto cheats_dir = GetCheatsDirPath(m_game.title_id);
    fs::FsPath file_path;
    std::snprintf(file_path, sizeof(file_path), "%s/%s.txt", cheats_dir.c_str(), cheat.build_id.c_str());

    fs::FsNativeSd fs;
    std::vector<u8> data;
    if (R_FAILED(fs.read_entire_file(file_path, data))) {
        App::Notify("Failed to read cheat file");
        return;
    }

    data.push_back(0);
    std::string content(reinterpret_cast<char*>(data.data()));

    // Show cheat content in a proper scrollable view
    App::Push<CheatContentMenu>(m_game, cheat.build_id, content);
}

void CheatFilesMenu::OnDelete() {
    if (m_cheats.empty() || m_index >= (s64)m_cheats.size()) {
        return;
    }

    const auto& cheat = m_cheats[m_index];
    App::Push<OptionBox>(
        "Delete cheat file for Build ID " + cheat.build_id + "?",
        "Cancel"_i18n, "Delete", 1,
        [this, cheat](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            if (DeleteCheatFile(m_game.title_id, cheat.build_id)) {
                App::Notify("Deleted cheat file");
                LoadCheatFiles();
            } else {
                App::Notify("Failed to delete cheat file");
            }
        }
    );
}

void CheatFilesMenu::OnFixBuildId() {
    if (m_cheats.empty() || m_index >= (s64)m_cheats.size()) {
        return;
    }

    const auto cheat = m_cheats[m_index];
    const auto target_build_id = ResolveManualTargetBuildId(m_game);
    if (!IsValidBuildId(target_build_id)) {
        if (!HasProdKeys()) {
            ShowProdKeysMissingDialog();
            return;
        }
        App::Notify("Could not determine current Build ID");
        return;
    }

    if (NormalizeBuildId(cheat.build_id) == target_build_id) {
        App::Notify("Cheat file already matches current Build ID");
        return;
    }

    const auto dest_path = GetManualCheatImportPath(m_game.title_id, target_build_id);
    fs::FsNativeSd fs;
    const bool overwrite = fs.FileExists(dest_path);

    std::string prompt = "Rename cheat file to current Build ID?\n\n";
    prompt += "Old: " + NormalizeBuildId(cheat.build_id) + "\n";
    prompt += "New: " + target_build_id;
    if (overwrite) {
        prompt += "\n\nA cheat file for the current Build ID already exists and will be replaced.";
    }

    App::Push<OptionBox>(
        prompt,
        "Cancel"_i18n, "Fix BID", 1,
        [this, cheat, target_build_id, overwrite](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            const auto rc = RenameCheatBuildId(m_game.title_id, cheat.build_id, target_build_id, overwrite);
            if (R_SUCCEEDED(rc)) {
                App::Notify("Cheat Build ID updated");
                LoadCheatFiles();
            } else {
                App::Push<ErrorBox>(rc, "Failed to update cheat Build ID");
            }
        }
    );
}


} // namespace sphaira::ui::menu::hats
