#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
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

auto IsCheatHeaderLine(const std::string& line) -> bool {
    return line.size() >= 3 &&
        ((line.front() == '[' && line.back() == ']') ||
         (line.front() == '{' && line.back() == '}'));
}

auto GetCheatHeaderName(const std::string& line) -> std::string {
    if (!IsCheatHeaderLine(line)) {
        return {};
    }

    return line.substr(1, line.length() - 2);
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
            "No cheat files found");
        return;
    }

    // Save and restore scissor to clip list drawing area
    nvgSave(vg);
    // Clip area starts below the header text
    nvgScissor(vg, 75.f, GetY() + 40.f, 1220.f - 150.f, 720.f - GetY() - 40.f);
    ON_SCOPE_EXIT(nvgRestore(vg));

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_cheats.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
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

// ============================================================
// CheatContentMenu - View cheat file content (shows titles in list)
// ============================================================

CheatContentMenu::CheatContentMenu(const GameCheatInfo& game, const std::string& build_id, const std::string& content)
    : MenuBase{"Cheat Content", MenuFlag_None}, m_game(game), m_build_id(build_id) {

    this->SetActions(
        std::make_pair(Button::A, Action{"View Code"_i18n, [this](){
            OnViewCheat();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    // Parse the cheat content to extract titles
    ParseCheatContent(content);

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatContentMenu::~CheatContentMenu() {
}

void CheatContentMenu::ParseCheatContent(const std::string& content) {
    m_cheats.clear();

    // First, scan for source footer comment to determine default source
    // Format: // source: CheatSlips or // source: nx-cheats-db
    CheatSource default_source = CheatSource::NxDb; // Default source
    std::string lower_content = content;
    std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);

    if (lower_content.find("// source: cheatslips") != std::string::npos) {
        default_source = CheatSource::Cheatslips;
    } else if (lower_content.find("// source: nx-cheats-db") != std::string::npos ||
               lower_content.find("// source: nxdb") != std::string::npos) {
        default_source = CheatSource::NxDb;
    } else if (lower_content.find("// source: gbatemp") != std::string::npos) {
        default_source = CheatSource::Gbatemp;
    }

    // Parse cheat file format
    std::istringstream stream(content);
    std::string line;
    CheatTitle current_cheat;
    bool in_cheat = false;
    CheatSource current_source = default_source; // Use detected source

    // Helper function to check if a line should be skipped (non-cheat entries)
    auto should_skip_entry = [](const std::string& name) -> bool {
        if (name.empty()) return true;

        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        // Skip common non-cheat entries
        if (lower_name.find("www.") != std::string::npos) return true;  // Website URLs (e.g., www.cheatslips.com)
        if (lower_name.find("credits:") == 0) return true;  // Credits entries (e.g., credits: author)
        if (lower_name.find("credit:") == 0) return true;   // Credits entries (e.g., credit: author)
        if (lower_name == "credits") return true;           // Just "Credits"
        if (lower_name == "credit") return true;            // Just "Credit"

        return false;
    };

    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Check for source comment: // cheats from: xxx
        if (!line.empty() && line.substr(0, 2) == "//") {
            std::string lower_line = line;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

            if (lower_line.find("cheatslips") != std::string::npos) {
                current_source = CheatSource::Cheatslips;
            } else if (lower_line.find("nx-cheats-db") != std::string::npos || lower_line.find("nxdb") != std::string::npos) {
                current_source = CheatSource::NxDb;
            } else if (lower_line.find("gbatemp") != std::string::npos) {
                current_source = CheatSource::Gbatemp;
            }
            continue;
        }

        // Check for cheat title/master-code format [Title] or {Title}
        if (IsCheatHeaderLine(line)) {
            // Save previous cheat if exists
            if (in_cheat && !current_cheat.name.empty()) {
                // Skip non-cheat entries like credits, website headers
                if (!should_skip_entry(current_cheat.name)) {
                    // Determine if cheat is empty (no actual code)
                    std::string content_check = current_cheat.content;
                    size_t start_pos = content_check.find('\n');
                    if (start_pos != std::string::npos) {
                        content_check = content_check.substr(start_pos + 1);
                    }
                    // Check if empty, whitespace only, or contains "Quota exceeded" message
                    std::string content_lower = content_check;
                    std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
                    current_cheat.is_empty = content_check.empty() ||
                        content_check.find_first_not_of(" \t\r\n") == std::string::npos ||
                        content_lower.find("quota exceeded") != std::string::npos ||
                        content_lower.find("quotaexceeded") != std::string::npos;

                    // Only add if it's a valid cheat (not empty/whitespace)
                    if (!current_cheat.name.empty() && current_cheat.name.find_first_not_of(" \t\r\n") != std::string::npos) {
                        m_cheats.push_back(current_cheat);
                    }
                }
            }

            // Start new cheat
            current_cheat.name = GetCheatHeaderName(line);
            current_cheat.content = line + "\n";
            current_cheat.source = current_source;
            current_cheat.is_empty = false;
            in_cheat = true;
        } else if (in_cheat) {
            // Add line to current cheat content
            current_cheat.content += line + "\n";
        }
    }

    // Don't forget the last cheat
    if (in_cheat && !current_cheat.name.empty()) {
        // Skip non-cheat entries
        if (!should_skip_entry(current_cheat.name)) {
            // Determine if cheat is empty
            std::string content_check = current_cheat.content;
            size_t start_pos = content_check.find('\n');
            if (start_pos != std::string::npos) {
                content_check = content_check.substr(start_pos + 1);
            }
            // Check if empty, whitespace only, or contains "Quota exceeded" message
            std::string content_lower = content_check;
            std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
            current_cheat.is_empty = content_check.empty() ||
                content_check.find_first_not_of(" \t\r\n") == std::string::npos ||
                content_lower.find("quota exceeded") != std::string::npos ||
                content_lower.find("quotaexceeded") != std::string::npos;

            // Only add if it's a valid cheat (not empty/whitespace)
            if (!current_cheat.name.empty() && current_cheat.name.find_first_not_of(" \t\r\n") != std::string::npos) {
                m_cheats.push_back(current_cheat);
            }
        }
    }

    log_write("[Cheats] Parsed %zu cheat titles (filtered out non-cheat entries)\n", m_cheats.size());
}

void CheatContentMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_cheats.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_cheats.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                // On touch, could show full cheat content in a popup
                App::Notify("Press A to view cheat code");
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void CheatContentMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // Draw header info
    gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "%s | %s | %zu cheats", m_game.name.c_str(), m_build_id.c_str(), m_cheats.size());

    if (m_cheats.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Cheats Not Found");
        return;
    }

    // Save and restore scissor to clip list drawing area
    nvgSave(vg);
    // Clip area starts below the header text
    nvgScissor(vg, 75.f, GetY() + 40.f, 1220.f - 150.f, SCREEN_HEIGHT - 100.f - (GetY() + 40.f));
    ON_SCOPE_EXIT(nvgRestore(vg));

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_cheats.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
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

        // Draw source badge
        const char* source_badge = "";
        NVGcolor source_color = theme->GetColour(ThemeEntryID_TEXT_INFO);
        if (cheat.source == CheatSource::Cheatslips) {
            source_badge = "CS";  // Removed brackets to fix rendering
            source_color = nvgRGB(0x4A, 0x90, 0xE2); // Blue for CheatSlips
        } else if (cheat.source == CheatSource::NxDb) {
            source_badge = "NX";  // Removed brackets to fix rendering
            source_color = nvgRGB(0x6B, 0xC6, 0x58); // Green for nx-cheats-db
        } else if (cheat.source == CheatSource::Gbatemp) {
            source_badge = "GB";  // Removed brackets to fix rendering
            source_color = nvgRGB(0xE2, 0x7D, 0x4A); // Orange for GBATemp
        }

        // Cheat name (truncated if too long)
        std::string name = cheat.name;
        if (name.length() > 50) {
            name = name.substr(0, 47) + "...";
        }

        float text_offset = text_xoffset;

        // Draw source badge first (without brackets to avoid rendering issues)
        if (strlen(source_badge) > 0) {
            gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                source_color,
                "%s", source_badge);
            text_offset += 28.f; // Width of CS/NX/GB text
        }

        // Draw empty indicator if cheat has no content (without brackets)
        if (cheat.is_empty) {
            gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                nvgRGB(0xFF, 0x00, 0x00), // Red for empty
                "EMPTY");  // Removed brackets to fix rendering
            text_offset += 45.f; // Width of EMPTY text
        }

        // Add extra space before cheat name
        text_offset += 8.f;

        // Draw cheat name with proper spacing
        gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            cheat.is_empty ? nvgRGB(0xFF, 0x66, 0x66) : theme->GetColour(text_id), // Red tint if empty
            "[%s]", name.c_str());
    });
}

void CheatContentMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

void CheatContentMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatContentMenu::OnViewCheat() {
    if (m_cheats.empty() || m_index < 0 || m_index >= (s64)m_cheats.size()) {
        return;
    }

    const auto& cheat = m_cheats[m_index];
    App::Push<CheatCodeViewerMenu>(cheat.name, cheat.content, cheat.is_empty);
}

// ============================================================
// CheatCodeViewerMenu - View individual cheat code (scrollable)
// ============================================================

CheatCodeViewerMenu::CheatCodeViewerMenu(const std::string& title, const std::string& content, bool is_empty)
    : MenuBase{"Cheat Code", MenuFlag_None}, m_title(title), m_content(content), m_is_empty(is_empty) {

    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    // Calculate content height for scrolling
    // Rough estimation: each line is about 20px tall
    size_t line_count = std::count(m_content.begin(), m_content.end(), '\n') + 1;
    m_content_height = line_count * 20.f;
    if (m_content_height < 100.f) {
        m_content_height = 100.f;
    }
}

CheatCodeViewerMenu::~CheatCodeViewerMenu() {
}

void CheatCodeViewerMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    // Handle scrolling with joystick
    if (controller->GotDown(Button::LS_UP) ||
        controller->GotDown(Button::RS_UP) ||
        controller->GotHeld(Button::LS_UP) ||
        controller->GotHeld(Button::RS_UP)) {
        m_scroll_offset -= 5.f;
    }
    if (controller->GotDown(Button::LS_DOWN) ||
        controller->GotDown(Button::RS_DOWN) ||
        controller->GotHeld(Button::LS_DOWN) ||
        controller->GotHeld(Button::RS_DOWN)) {
        m_scroll_offset += 5.f;
    }

    // Clamp scroll offset
    float max_scroll = m_content_height - (SCREEN_HEIGHT - 150.f);
    if (max_scroll < 0) max_scroll = 0;
    if (m_scroll_offset < 0) m_scroll_offset = 0;
    if (m_scroll_offset > max_scroll) m_scroll_offset = max_scroll;
}

void CheatCodeViewerMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const float margin = 80.f;
    const float top_margin = GetY() + 50.f;
    const float content_width = SCREEN_WIDTH - 150.f;
    const float max_height = SCREEN_HEIGHT - 150.f;

    // Draw title
    gfx::drawTextArgs(vg, margin, GetY() + 20.f, 20.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_HIGHLIGHT_1),
        "[%s]", m_title.c_str());

    // Draw empty warning if applicable
    if (m_is_empty) {
        gfx::drawTextArgs(vg, margin, GetY() + 120.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            nvgRGB(0xFF, 0x00, 0x00),
            "⚠️ EMPTY CHEAT CODE or QUOTA EXCEEDED!");
    }

    // Save and clip for scrolling
    nvgSave(vg);
    nvgScissor(vg, margin, top_margin, content_width, max_height);

    // Draw cheat code content with scrolling
    float y = top_margin - m_scroll_offset;
    constexpr float line_height = 20.f;

    std::istringstream stream(m_content);
    std::string line;
    while (std::getline(stream, line)) {
        if (y + line_height > top_margin - 20.f && y < top_margin + max_height) {
            // Use monospace-like font for code
            NVGcolor color = theme->GetColour(ThemeEntryID_TEXT);

            // Highlight empty quota message
            std::string line_lower = line;
            std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
            if (line_lower.find("quota") != std::string::npos) {
                color = nvgRGB(0xFF, 0x66, 0x66);
            }

            gfx::drawTextArgs(vg, margin, y, 16.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                color,
                "%s", line.c_str());
        }
        y += line_height;
    }

    nvgRestore(vg);

    // Draw scroll indicator if content is scrollable
    if (m_content_height > max_height) {
        float scroll_bar_height = (max_height / m_content_height) * max_height;
        float scroll_bar_y = top_margin + (m_scroll_offset / m_content_height) * max_height;

        gfx::drawRect(vg, SCREEN_WIDTH - margin + 10.f, scroll_bar_y, 5.f, scroll_bar_height,
            nvgRGBA(0x80, 0x80, 0x80, 0x80));
    }
}

} // namespace sphaira::ui::menu::hats
