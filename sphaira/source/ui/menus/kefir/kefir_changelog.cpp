#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "utils/utils.hpp"

#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "download.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/list.hpp"

#include <deko3d.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>



namespace sphaira::ui::menu::kefir {

namespace {
constexpr const char* KEFIR_CHANGELOG_URL = "https://raw.githubusercontent.com/rashevskyv/kefir/master/changelog_full";
constexpr const char* KEFIR_VERSION_PATH = "/switch/kefir-updater/version";
constexpr const char* CACHE_DIR = "/config/kefir-updater";
constexpr const char* AMS_ZIP = "/config/kefir-updater/atmo.zip";
constexpr const char* KEFIR_PATH = "/kefir";
constexpr const char* COPY_FILES_TXT = "/config/kefir-updater/copy_files.txt";
constexpr const char* STAGED_COPY_FILES_TXT = "/kefir/config/kefir-updater/copy_files.txt";
}


namespace detail {

auto ExtractChangelogSection(const std::string& raw, bool ukrainian) -> std::string {
    constexpr std::string_view ukr_header = "#### **UKR**";
    constexpr std::string_view eng_header = "#### **ENG**";
    constexpr std::string_view separator = "____";

    if (ukrainian) {
        const auto pos = raw.find(ukr_header);
        if (pos == std::string::npos) {
            return raw;
        }

        const auto start = pos + ukr_header.size();
        const auto sep_pos = raw.find(separator, start);
        return raw.substr(start, (sep_pos != std::string::npos ? sep_pos : raw.size()) - start);
    }

    const auto pos = raw.find(eng_header);
    if (pos == std::string::npos) {
        return raw;
    }
    return raw.substr(pos + eng_header.size());
}

auto ParseKefirChangelogVersion(const std::string& version) -> int {
    std::string number;
    for (const auto c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            number += c;
        } else if (!number.empty()) {
            break;
        }
    }

    if (number.empty()) {
        return 0;
    }

    return std::strtol(number.c_str(), nullptr, 10);
}

auto IsUkrainianLanguage() -> bool {
    return i18n::IsUkrainian();
}

auto MergeDirectory(ProgressBox* pbox, fs::FsNativeSd& fs, const fs::FsPath& src_dir, const fs::FsPath& dst_dir) -> Result {
    if (!fs.DirExists(src_dir)) {
        R_SUCCEED();
    }

    if (!fs.DirExists(dst_dir)) {
        R_TRY(fs.CreateDirectoryRecursively(dst_dir));
    }

    fs::Dir dir;
    R_TRY(fs.OpenDirectory(src_dir, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir));

    std::vector<FsDirectoryEntry> entries;
    R_TRY(dir.ReadAll(entries));

    for (const auto& entry : entries) {
        fs::FsPath src_path{src_dir.toString() + "/" + entry.name};
        fs::FsPath dst_path{dst_dir.toString() + "/" + entry.name};

        if (entry.type == FsDirEntryType_Dir) {
            R_TRY(MergeDirectory(pbox, fs, src_path, dst_path));
        } else {
            if (fs.FileExists(dst_path)) {
                R_TRY(fs.DeleteFile(dst_path));
            }
            pbox->NewTransfer("Copying " + dst_path.toString());
            R_TRY(pbox->CopyFile(&fs, src_path, dst_path, true));
        }
    }

    R_SUCCEED();
}

auto CopyIfExists(ProgressBox* pbox, fs::FsNativeSd& fs, const fs::FsPath& src, const fs::FsPath& dst) -> Result {
    if (!fs.FileExists(src)) {
        R_SUCCEED();
    }

    R_TRY(fs.CreateDirectoryRecursivelyWithPath(dst));
    pbox->NewTransfer("Copying " + dst.toString());
    return pbox->CopyFile(&fs, src, dst, true);
}

auto CopyListedFiles(ProgressBox* pbox, fs::FsNativeSd& fs, const char* list_path) -> Result {
    FILE* file = std::fopen(list_path, "r");
    if (!file) {
        R_SUCCEED();
    }
    ON_SCOPE_EXIT(std::fclose(file));

    char line[FS_MAX_PATH * 2]{};
    while (std::fgets(line, sizeof(line), file)) {
        std::string entry{line};
        while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) {
            entry.pop_back();
        }
        if (entry.empty()) {
            continue;
        }

        const auto sep = entry.find('|');
        if (sep == std::string::npos) {
            continue;
        }

        fs::FsPath src{entry.substr(0, sep)};
        fs::FsPath dst{entry.substr(sep + 1)};
        if (!fs.FileExists(src) && src.s[0] == '/') {
            src = std::string{KEFIR_PATH} + src.s;
        }
        R_TRY(CopyIfExists(pbox, fs, src, dst));
    }

    R_SUCCEED();
}

auto DownloadAndInstallKefir(ProgressBox* pbox, const UpdaterEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.FileExists(AMS_ZIP)) {
        fs.DeleteFile(AMS_ZIP);
    }

    pbox->NewTransfer("Downloading " + entry.name);
    const auto result = curl::Api().ToFile(
        curl::Url{entry.url},
        curl::Path{AMS_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    if (!result.success) {
        if (pbox->ShouldExit()) {
            R_THROW(Result_TransferCancelled);
        }
        R_THROW(Result_AppstoreFailedZipDownload);
    }

    if (fs.DirExists(KEFIR_PATH)) {
        R_TRY(fs.DeleteDirectoryRecursively(KEFIR_PATH));
    }
    R_TRY(fs.CreateDirectoryRecursively(KEFIR_PATH));

    pbox->NewTransfer("Extracting to /kefir...");
    R_TRY(thread::TransferUnzipAll(pbox, AMS_ZIP, &fs, KEFIR_PATH));
    R_TRY(fs.Commit());

    if (fs.FileExists("/kefir/payload.bin")) {
        if (fs.FileExists("/payload.bin")) {
            R_TRY(fs.DeleteFile("/payload.bin"));
        }
        pbox->NewTransfer("Copying /payload.bin");
        R_TRY(pbox->CopyFile(&fs, "/kefir/payload.bin", "/payload.bin", true));
    }

    if (fs.DirExists("/kefir/bootloader")) {
        R_TRY(MergeDirectory(pbox, fs, "/kefir/bootloader", "/bootloader"));
    }

    R_TRY(CopyIfExists(pbox, fs, "/kefir/bootloader/hekate_ipl.ini", "/bootloader/hekate_ipl.ini"));
    R_TRY(CopyIfExists(pbox, fs, "/kefir/config/kefir-updater/kefir_updater.ini", "/bootloader/ini/!kefir_updater.ini"));
    R_TRY(CopyIfExists(pbox, fs, "/kefir/bootloader/res/ku.bmp", "/bootloader/res/ku.bmp"));
    R_TRY(CopyListedFiles(pbox, fs, COPY_FILES_TXT));
    R_TRY(CopyListedFiles(pbox, fs, STAGED_COPY_FILES_TXT));

    if (fs.FileExists(AMS_ZIP)) {
        fs.DeleteFile(AMS_ZIP);
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

} // namespace detail


KefirChangelogBox::KefirChangelogBox(UpdaterEntry entry, Callback callback)
: m_entry{std::move(entry)}
, m_callback{std::move(callback)} {
    m_current_version = detail::ReadFirstLine(KEFIR_VERSION_PATH);
    m_target_version = detail::ExtractKefirVersion(m_entry.name, m_entry.url);

    if (const auto version = detail::ParseKefirChangelogVersion(m_target_version)) {
        m_title = "Kefir " + std::to_string(version) + " changelog";
    } else {
        m_title = "Kefir changelog";
    }

    m_pos = Vec4{70.f, 42.f, 1140.f, 636.f};
    SetUiButtonPos({m_pos.x + m_pos.w - 30.f, m_pos.y + m_pos.h - 49.f});

    SetActions(
        std::make_pair(Button::A, Action{"Install"_i18n, [this](){
            if (m_loading || !m_unlocked || m_installing || !m_button_focused) {
                App::PlaySoundEffect(SoundEffect_Limit);
                return;
            }

            m_installing = true;
            m_callback();
            SetPop();
        }}),
        std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::UP | Button::LS_UP | Button::RS_UP, Action{static_cast<u8>(ActionType::DOWN | ActionType::HELD), [this](){
            if (m_button_focused) {
                m_button_focused = false;
                App::PlaySoundEffect(SoundEffect_Focus);
            } else {
                ScrollBy(-CHANGELOG_SCROLL_STEP);
            }
        }}),
        std::make_pair(Button::DOWN | Button::LS_DOWN | Button::RS_DOWN, Action{static_cast<u8>(ActionType::DOWN | ActionType::HELD), [this](){
            if (!m_button_focused) {
                if (m_scroll >= m_max_scroll - 0.5f) {
                    if (m_unlocked) {
                        m_button_focused = true;
                        App::PlaySoundEffect(SoundEffect_Focus);
                    } else {
                        App::PlaySoundEffect(SoundEffect_Limit);
                    }
                } else {
                    ScrollBy(CHANGELOG_SCROLL_STEP);
                }
            } else {
                App::PlaySoundEffect(SoundEffect_Limit);
            }
        }}),
        std::make_pair(Button::L | Button::L2, Action{[this](){
            if (!m_button_focused) {
                ScrollBy(-m_text_area.h);
            }
        }}),
        std::make_pair(Button::R | Button::R2, Action{[this](){
            if (!m_button_focused) {
                ScrollBy(m_text_area.h);
            }
        }})
    );

    LoadChangelog();
}

void KefirChangelogBox::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);

    if (touch->is_clicked) {
        if (m_unlocked && touch->in_range(m_install_button_rect)) {
            if (!m_installing) {
                m_installing = true;
                m_callback();
                SetPop();
            }
        }
    }
}

void KefirChangelogBox::Draw(NVGcontext* vg, Theme* theme) {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

    gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_pos.y + 28.f, 27.f,
        theme->GetColour(ThemeEntryID_TEXT_SELECTED), m_title.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    const auto target = m_target_version.empty() ? "Unknown" : m_target_version;
    const std::string current_lbl = "Current:"_i18n;
    const std::string target_lbl = "Target:"_i18n;

    nvgSave(vg);
    nvgFontSize(vg, 17.f);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT_INFO));

    // Draw "Current:" bold, version normal
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, m_pos.x + 42.f, m_pos.y + 70.f, current_lbl.c_str(), nullptr);
    nvgText(vg, m_pos.x + 42.f + 1.f, m_pos.y + 70.f, current_lbl.c_str(), nullptr);

    float current_bounds[4]{};
    nvgTextBounds(vg, m_pos.x + 42.f, m_pos.y + 70.f, current_lbl.c_str(), nullptr, current_bounds);
    float current_width = current_bounds[2] - current_bounds[0];

    nvgText(vg, m_pos.x + 42.f + current_width + 7.f, m_pos.y + 70.f, m_current_version.c_str(), nullptr);

    // Draw "Target:" bold, version normal
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    float target_bounds[4]{};
    nvgTextBounds(vg, m_pos.x + m_pos.w - 42.f, m_pos.y + 70.f, target.c_str(), nullptr, target_bounds);
    float target_width = target_bounds[2] - target_bounds[0];

    nvgText(vg, m_pos.x + m_pos.w - 42.f, m_pos.y + 70.f, target.c_str(), nullptr);

    nvgText(vg, m_pos.x + m_pos.w - 42.f - target_width - 7.f, m_pos.y + 70.f, target_lbl.c_str(), nullptr);
    nvgText(vg, m_pos.x + m_pos.w - 42.f - target_width - 7.f + 1.f, m_pos.y + 70.f, target_lbl.c_str(), nullptr);
    nvgRestore(vg);

    gfx::drawRect(vg, m_pos.x, m_pos.y + 102.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    gfx::drawRect(vg, m_pos.x, m_pos.y + m_pos.h - 82.f, m_pos.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    m_text_area = Vec4{m_pos.x + 48.f, m_pos.y + 122.f, m_pos.w - 118.f, m_pos.h - 232.f};
    if (m_loading) {
        gfx::drawText(vg, m_pos.x + m_pos.w / 2.f, m_text_area.y + m_text_area.h / 2.f, 24.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading changelog..."_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    } else {
        DrawChangelogText(vg, theme);
    }

    const auto footer = m_loading ? "Loading changelog..."_i18n :
        (m_unlocked ? (m_button_focused ? "Press A to Install."_i18n : "Scroll down to select Install."_i18n) : "Scroll to the bottom to unlock Install."_i18n);
    gfx::drawText(vg, m_pos.x + 42.f, m_pos.y + m_pos.h - 52.f, 17.f,
        theme->GetColour(m_unlocked ? ThemeEntryID_TEXT_INFO : ThemeEntryID_ERROR),
        footer.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    // Draw button centered horizontally
    m_install_button_rect = Vec4{m_pos.x + (m_pos.w - 220.f) / 2.f, m_pos.y + m_pos.h - 64.f, 220.f, 46.f};

    nvgSave(vg);
    if (m_button_focused) {
        // Focused - Green
        const auto btn_color = nvgRGBA(46, 125, 50, 255);
        gfx::drawRect(vg, m_install_button_rect, btn_color, 4.f);
        gfx::drawRectOutline(vg, theme, 4.f, m_install_button_rect);
        gfx::drawText(vg, m_install_button_rect.x + m_install_button_rect.w / 2.f,
            m_install_button_rect.y + m_install_button_rect.h / 2.f, 18.f,
            nvgRGBA(255, 255, 255, 255), "Install"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    } else {
        // Unfocused (always gray, whether unlocked or not)
        const auto btn_color = nvgRGBA(60, 60, 64, 255);
        gfx::drawRect(vg, m_install_button_rect, btn_color, 4.f);
        gfx::drawText(vg, m_install_button_rect.x + m_install_button_rect.w / 2.f,
            m_install_button_rect.y + m_install_button_rect.h / 2.f, 18.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Install"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);

    Widget::Draw(vg, theme);
}

void KefirChangelogBox::LoadChangelog() {
    if (!detail::ParseKefirChangelogVersion(m_target_version)) {
        bool dummy = false;
        m_text = detail::BuildKefirChangelogText({}, m_current_version, m_target_version, dummy);
        m_loading = false;
        return;
    }

    const auto queued = curl::Api().ToMemoryAsync(
        curl::Url{KEFIR_CHANGELOG_URL},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            bool should_skip = false;
            if (!result.success || result.data.empty()) {
                m_text = detail::BuildKefirChangelogText({}, m_current_version, m_target_version, should_skip);
            } else {
                const std::string raw{reinterpret_cast<const char*>(result.data.data()), result.data.size()};
                m_text = detail::BuildKefirChangelogText(raw, m_current_version, m_target_version, should_skip);
            }

            m_scroll = 0.f;
            m_max_scroll = 0.f;
            m_unlocked = false;
            m_loading = false;
        }}
    );

    if (!queued) {
        m_text = "Failed to queue changelog download.";
        m_loading = false;
    }
}

void KefirChangelogBox::ScrollBy(float amount) {
    if (m_loading || m_max_scroll <= 0.f) {
        return;
    }

    const auto old_scroll = m_scroll;
    m_scroll = std::clamp(m_scroll + amount, 0.f, m_max_scroll);
    if (old_scroll != m_scroll) {
        App::PlaySoundEffect(SoundEffect_Scroll);
    } else {
        App::PlaySoundEffect(SoundEffect_Limit);
    }
}

void KefirChangelogBox::DrawChangelogText(NVGcontext* vg, Theme* theme) {
    if (m_text.empty()) {
        m_text = "No changelog entries found.";
    }

    nvgSave(vg);
    m_text_height = detail::RenderChangelogText(vg, theme, m_text, Vec4{0.f, 0.f, m_text_area.w, m_text_area.h}, 0.f, false,
        CHANGELOG_FONT_SIZE, CHANGELOG_LINE_HEIGHT, CHANGELOG_HEADER_FONT_SIZE, CHANGELOG_PREAMBLE_FONT_SIZE);
    m_max_scroll = std::max(0.f, m_text_height - m_text_area.h + 14.f);
    m_scroll = std::clamp(m_scroll, 0.f, m_max_scroll);

    if (m_max_scroll <= 0.5f || m_scroll >= m_max_scroll - 1.f) {
        m_unlocked = true;
    }

    nvgScissor(vg, m_text_area.x, m_text_area.y, m_text_area.w, m_text_area.h);
    detail::RenderChangelogText(vg, theme, m_text, m_text_area, m_scroll, true,
        CHANGELOG_FONT_SIZE, CHANGELOG_LINE_HEIGHT, CHANGELOG_HEADER_FONT_SIZE, CHANGELOG_PREAMBLE_FONT_SIZE);
    nvgRestore(vg);

    const auto count = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_height / CHANGELOG_SCROLL_STEP)));
    const auto page = std::max<s64>(1, static_cast<s64>(std::ceil(m_text_area.h / CHANGELOG_SCROLL_STEP)));
    const auto index = std::min<s64>(std::max<s64>(0, count - page), static_cast<s64>(std::ceil(m_scroll / CHANGELOG_SCROLL_STEP)));
    gfx::drawScrollbar2(vg, theme, m_text_area.x + m_text_area.w + 20.f, m_text_area.y, m_text_area.h, index, count, 1, page);
}

} // namespace sphaira::ui::menu::kefir
