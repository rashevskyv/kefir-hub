#include "ui/menus/storage_menu.hpp"

#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "storage_model.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <minIni.h>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::storage {
namespace {

using namespace sphaira::storage;

struct Data {
    std::int64_t total{};      // the whole card: the file system plus the partition outside it
    std::int64_t visible{};    // what the file system sees
    std::int64_t free_bytes{};
    std::int64_t nand_total{};
    std::int64_t nand_free{};
    bool booted_emummc{};
    std::vector<Slice> slices;
};

// bytes of every file inside a folder; a runaway tree stops at the depth limit
auto DirBytes(fs::FsNativeSd& fs, const fs::FsPath& path, int depth, ProgressBox* pbox) -> std::int64_t {
    if (depth > 12 || (pbox && pbox->ShouldExit())) {
        return 0;
    }
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(path, FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &dir))) {
        return 0;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return 0;
    }
    std::int64_t total = 0;
    for (const auto& e : entries) {
        if (e.type == FsDirEntryType_Dir) {
            total += DirBytes(fs, fs::AppendPath(path, e.name), depth + 1, pbox);
        } else {
            total += e.file_size;
        }
    }
    return total;
}

auto Name(Id id) -> std::string {
    switch (id) {
        case Id::Games: return "Games in the system (sysMMC)"_i18n;
        case Id::EmuGames: return "Games in the emuMMC"_i18n;
        case Id::EmuData: return "emuMMC files"_i18n;
        case Id::EmuPartition: return "emuMMC partition"_i18n;
        case Id::Saves: return "Saves and album"_i18n;
        case Id::System: return "Atmosphère and Kefir"_i18n;
        case Id::Homebrew: return "Homebrew"_i18n;
        case Id::Backups: return "Dumps and backups"_i18n;
        case Id::Files: return "Game files and ROMs"_i18n;
        case Id::Other: return "Other files"_i18n;
        case Id::Free: return "Free space"_i18n;
    }
    return {};
}

auto AboutEmummc() -> std::string {
    return "What an emuMMC is: a full copy of the console's system (its NAND) kept on the microSD card instead of in the internal memory. When you start the console in the emuMMC, it runs that copy: its own users, its own installed games, its own saves. The original system in the internal memory stays untouched.\n\nWhy it is on the card: the internal memory is small (about 25 GB for games and saves) and belongs to the original system. A copy of the whole system does not fit there next to it, and the card is large and can be replaced.\n\nWhy people use it: the original system stays clean and can go online as before, while the emuMMC is where modified software, installs and experiments live.\n\nHow it takes space: as a partition it is a block of the card outside the file system (a Windows or Linux PC sees it as unallocated space), so it never appears in a folder. As files it is a folder (usually /emuMMC) of big image files. The games you install while the emuMMC runs go to its own Nintendo folder on the card, and they count as the emuMMC's, not as the original system's."_i18n;
}

// what it is, whether it can go, where it is, why it is there
auto Card(Id id) -> std::string {
    switch (id) {
        case Id::Games: return "What: the games installed from the original system (sysMMC) to the microSD card.\n\nDelete: yes, from Tools → Games (Delete). The game's saves stay. The same game can be installed again later.\n\nWhere: /Nintendo/Contents on the card. These games are not visible from the emuMMC: each system keeps its own list.\n\nWhy: installing to the card keeps the small system memory free."_i18n;
        case Id::EmuGames: return "What: the games installed while the emuMMC runs. They belong to the emuMMC: its own Nintendo folder on the card.\n\nDelete: yes, from Tools → Games while the emuMMC runs (Delete). The saves stay.\n\nWhere: the emuMMC's Nintendo folder (see emummc.ini, nintendo_path), for example /emuMMC/SD00/Nintendo/Contents.\n\nWhy: the emuMMC is a separate system with its own games, so they are kept apart from the original ones."_i18n;
        case Id::EmuData: return "What: the emuMMC as files on the card: the copy of the system and its data (users, saves, settings).\n\nDelete: no, unless you give up the emuMMC. Everything you did in it is in there.\n\nWhere: the folder named in emummc.ini (path), usually /emuMMC/SD00.\n\nWhy: a file-based emuMMC lives in the card's file system, so it takes space you can see in the folders."_i18n;
        case Id::EmuPartition: return "What: the emuMMC as a partition: a block at the end of the card that the file system does not use. Its size is what the card has beyond the file system.\n\nDelete: no, unless you give up the emuMMC. Only a partition tool (hekate, a PC) can change it, and the card's free space does not grow when it is removed until the file system is enlarged.\n\nWhere: raw sectors of the card, outside any folder.\n\nWhy: this is where the 30 GB 'missing' from the free space went."_i18n;
        case Id::Saves: return "What: game saves kept on the card and the album (screenshots and videos).\n\nDelete: the album can go once copied to a computer. Delete saves only from the Saves menu: a save cannot be recovered.\n\nWhere: /Nintendo/save and /Nintendo/Album.\n\nWhy: the console writes the album here; saves land here when a game uses card-based saves."_i18n;
        case Id::System: return "What: Atmosphère, the hekate bootloader, the Hub's own settings and the Kefir files.\n\nDelete: no. The console may stop starting. Cleanup of caches and old downloads is in Tools → Clean system junk.\n\nWhere: /atmosphere, /bootloader, /config, /kefir, /sept.\n\nWhy: the custom firmware and its modules, cheats and settings live here."_i18n;
        case Id::Homebrew: return "What: homebrew apps and their data.\n\nDelete: yes, an app at a time from the Homebrew tab (the question can also remove its folder and its HOME icon).\n\nWhere: /switch.\n\nWhy: the apps you launch from the Homebrew tab."_i18n;
        case Id::Backups: return "What: dumps of games, saves and system data you made, and backups of saves.\n\nDelete: yes, once a copy is safe elsewhere. They are the only copy until then.\n\nWhere: /dumps, /backups, /backup, /JKSV.\n\nWhy: kept so that a lost save or game can be put back."_i18n;
        case Id::Files: return "What: game files (NSP, XCI) and emulator ROMs that you copied to the card.\n\nDelete: yes, in the File Browser. Installed games are not touched: these are only the files.\n\nWhere: /games, /roms, /ROMs, /retroarch.\n\nWhy: the source for installing or emulating."_i18n;
        case Id::Other: return "What: files that no other slice counts: themes, pictures, documents, leftovers of other tools.\n\nDelete: look first in the File Browser. Anything you do not know is safer left.\n\nWhere: all over the card.\n\nWhy: it is what remains after the named parts."_i18n;
        case Id::Free: return "What: unused space on the card.\n\nDelete: nothing to delete. Keep some free: installs and updates need room, and a full card slows the console.\n\nWhere: the whole card.\n\nWhy: free space is where the next game, update or save will go."_i18n;
    }
    return {};
}

auto Colour(Id id) -> NVGcolor {
    switch (id) {
        case Id::Games: return nvgRGB(0x3F, 0x8F, 0xE0);
        case Id::EmuGames: return nvgRGB(0xC4, 0xA6, 0xF0);
        case Id::EmuData: return nvgRGB(0xA1, 0x7B, 0xE3);
        case Id::EmuPartition: return nvgRGB(0x7A, 0x4C, 0xC8);
        case Id::Saves: return nvgRGB(0xE0, 0x9A, 0x2B);
        case Id::System: return nvgRGB(0xD9, 0x4E, 0x4E);
        case Id::Homebrew: return nvgRGB(0x3F, 0xB5, 0x8B);
        case Id::Backups: return nvgRGB(0x2E, 0xB8, 0xC9);
        case Id::Files: return nvgRGB(0xC7, 0x7D, 0xB4);
        case Id::Other: return nvgRGB(0x8A, 0x93, 0x9F);
        case Id::Free: return nvgRGB(0x33, 0x3B, 0x46);
    }
    return nvgRGB(255, 255, 255);
}

// the emuMMC's own settings: where its files are, where its Nintendo folder is, whether it is a partition
struct EmuConfig {
    std::string path;      // file-based: its folder, "/emuMMC/SD00"
    std::string nintendo;  // its Nintendo folder, "/emuMMC/SD00/Nintendo"; empty = it shares /Nintendo
    bool partition{};      // a non-zero sector: the NAND is a raw partition
};

auto Slashed(std::string p) -> std::string {
    if (p.empty()) {
        return p;
    }
    if (p[0] != '/') {
        p.insert(p.begin(), '/');
    }
    while (p.size() > 1 && p.back() == '/') {
        p.pop_back();
    }
    return p;
}

auto ReadEmuConfig() -> EmuConfig {
    EmuConfig c;
    for (const char* ini : {"/emuMMC/emummc.ini", "/emummc/emummc.ini"}) {
        if (!fs::FileExists(ini)) {
            continue;
        }
        char path[256]{}, sector[64]{}, nintendo[256]{};
        ini_gets("emummc", "path", "", path, sizeof(path), ini);
        ini_gets("emummc", "sector", "", sector, sizeof(sector), ini);
        ini_gets("emummc", "nintendo_path", "", nintendo, sizeof(nintendo), ini);
        c.path = Slashed(path);
        c.nintendo = Slashed(nintendo);
        c.partition = sector[0] && std::strcmp(sector, "0x0") && std::strcmp(sector, "0") && std::strcmp(sector, "0x00000000");
        break;
    }
    // the booted emuMMC knows its own Nintendo folder better than the file does
    if (const auto booted = App::GetEmummcNintendoPath(); !booted.empty()) {
        c.nintendo = booted;
    }
    return c;
}

// a line of the list: a slice, or the emuMMC container that holds some of the slices
struct Row {
    bool group{};   // the emuMMC as a whole (its size is the sum of its parts)
    bool child{};   // a part inside the emuMMC
    size_t slice{}; // index into Data::slices (not for the group)
    std::int64_t bytes{};
};

struct Menu final : MenuBase {
    explicit Menu(std::shared_ptr<Data> data) : MenuBase{"Storage manager"_i18n, MenuFlag_None}, m_data{std::move(data)} {
        // the emuMMC is one container: a line for the whole, then its parts under it (the emuMMC itself, the games in it)
        bool group_done = false;
        for (size_t i = 0; i < m_data->slices.size(); i++) {
            const auto& s = m_data->slices[i];
            if (BelongsToEmummc(s.id) && !group_done) {
                m_rows.push_back({true, false, 0, EmummcTotal(m_data->slices)});
                group_done = true;
            }
            m_rows.push_back({false, BelongsToEmummc(s.id), i, s.bytes});
        }
        SetAction(Button::A, Action{"Open"_i18n, [this]() {
            const auto& r = m_rows[m_index];
            if (r.group) {
                App::Push<OptionBox>("emuMMC"_i18n + ": " + grid::FormatBytes(static_cast<u64>(r.bytes)) + "\n\n" + AboutEmummc(), "OK"_i18n);
                return;
            }
            const auto& s = m_data->slices[r.slice];
            const auto text = Name(s.id) + ": " + grid::FormatBytes(static_cast<u64>(s.bytes)) + "\n\n" + Card(s.id);
            App::Push<OptionBox>(text, "OK"_i18n);
        }});
        SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
    }

    auto GetShortTitle() const -> const char* override { return "Storage manager"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (!controller) {
            return;
        }
        const auto n = static_cast<s64>(m_rows.size());
        if (controller->GotDown(Button::DOWN) && m_index + 1 < n) {
            m_index++;
            App::PlaySoundEffect(SoundEffect_Focus);
        } else if (controller->GotDown(Button::UP) && m_index > 0) {
            m_index--;
            App::PlaySoundEffect(SoundEffect_Focus);
        }
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        const auto& d = *m_data;
        const auto sum = Total(d.slices);
        const float cx = 290.f, cy = 355.f, r_out = 175.f, r_in = 110.f;

        // the ring: a slice is an arc, the chosen one stands out a little
        const float full = 2.f * static_cast<float>(M_PI);
        float angle = -static_cast<float>(M_PI) / 2.f;
        for (size_t i = 0; i < d.slices.size(); i++) {
            const auto& s = d.slices[i];
            const float span = sum > 0 ? full * static_cast<float>(s.bytes) / static_cast<float>(sum) : 0.f;
            if (span <= 0.f) {
                continue;
            }
            const auto& chosen = m_rows[m_index];
            const bool lit = chosen.group ? BelongsToEmummc(s.id) : chosen.slice == i;
            const float out = lit ? r_out + 10.f : r_out;
            nvgBeginPath(vg);
            nvgArc(vg, cx, cy, out, angle, angle + span, NVG_CW);
            nvgArc(vg, cx, cy, r_in, angle + span, angle, NVG_CCW);
            nvgClosePath(vg);
            nvgFillColor(vg, Colour(s.id));
            nvgFill(vg);
            angle += span;
        }
        const auto used = std::max<std::int64_t>(0, d.total - d.free_bytes);
        gfx::drawTextArgs(vg, cx, cy - 14.f, 28.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%s", grid::FormatBytes(static_cast<u64>(used)).c_str());
        gfx::drawTextArgs(vg, cx, cy + 18.f, 17.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s %s", "of"_i18n.c_str(), grid::FormatBytes(static_cast<u64>(d.total)).c_str());

        // the list: a slice per line, the emuMMC as one line with its parts indented under it; A opens a card
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        float y = 138.f;
        for (size_t i = 0; i < m_rows.size(); i++, y += 36.f) {
            const auto& r = m_rows[i];
            const bool sel = static_cast<s64>(i) == m_index;
            if (sel) {
                gfx::drawRect(vg, 560.f, y - 16.f, 650.f, 32.f, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
                gfx::drawRectOutline(vg, theme, 4.f, 560.f, y - 16.f, 650.f, 32.f);
            }
            const auto text = theme->GetColour(sel ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            const float indent = r.child ? 34.f : 0.f;
            if (r.group) {
                // the container: its parts' colours side by side in one swatch (never a box that looks like a checkbox)
                gfx::drawRect(vg, 580.f, y - 9.f, 9.f, 18.f, Colour(Id::EmuPartition), 2.f);
                gfx::drawRect(vg, 589.f, y - 9.f, 9.f, 18.f, Colour(Id::EmuGames), 2.f);
            } else {
                const auto& s = m_data->slices[r.slice];
                gfx::drawRect(vg, 580.f + indent, y - 8.f, r.child ? 16.f : 18.f, r.child ? 16.f : 18.f, Colour(s.id), 4.f);
            }
            const auto label = r.group ? "emuMMC"_i18n : Name(m_data->slices[r.slice].id);
            gfx::drawTextArgs(vg, 612.f + indent, y, r.group ? 19.f : 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text, "%s", label.c_str());
            const auto pm = PerMille(Slice{r.group ? Id::EmuData : m_data->slices[r.slice].id, r.bytes}, sum);
            gfx::drawTextArgs(vg, 1010.f, y, 15.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info, "%d.%d%%", pm / 10, pm % 10);
            gfx::drawTextArgs(vg, 1190.f, y, 17.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, text, "%s", grid::FormatBytes(static_cast<u64>(r.bytes)).c_str());
        }

        // the totals: what the card is, and what the emuMMC takes of it, so no space is a mystery
        const auto outside = std::max<std::int64_t>(0, d.total - d.visible);
        gfx::drawTextArgs(vg, 560.f, 590.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s: %s = %s %s + %s %s",
            "microSD", grid::FormatBytes(static_cast<u64>(d.total)).c_str(),
            grid::FormatBytes(static_cast<u64>(d.visible)).c_str(), "in the file system"_i18n.c_str(),
            grid::FormatBytes(static_cast<u64>(outside)).c_str(), "outside it"_i18n.c_str());
        gfx::drawTextArgs(vg, 560.f, 612.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, info, "%s: %s / %s",
            "System memory"_i18n.c_str(), grid::FormatBytes(static_cast<u64>(std::max<std::int64_t>(0, d.nand_total - d.nand_free))).c_str(),
            grid::FormatBytes(static_cast<u64>(d.nand_total)).c_str());
    }

    std::shared_ptr<Data> m_data;
    std::vector<Row> m_rows;
    s64 m_index{};
};

} // namespace

void Open() {
    auto data = std::make_shared<Data>();
    App::Push<ProgressBox>(0, "Counting"_i18n, "Storage manager"_i18n, [data](auto pbox) -> Result {
        const auto polled = MenuBase::GetPolledData(true);
        data->visible = polled.sd_total;
        data->free_bytes = polled.sd_free;
        data->nand_total = polled.nand_total;
        data->nand_free = polled.nand_free;
        data->booted_emummc = App::IsEmummc();

        // the whole card, when the console tells it: the part beyond the file system is the emuMMC partition
        data->total = data->visible;
        FsDeviceOperator op{};
        if (R_SUCCEEDED(fsOpenDeviceOperator(&op))) {
            s64 user_area = 0;
            if (R_SUCCEEDED(fsDeviceOperatorGetSdCardUserAreaSize(&op, &user_area)) && user_area > data->visible) {
                data->total = user_area;
            }
            fsDeviceOperatorClose(&op);
        }
        // anything under 1 GiB is only the alignment of the partition table
        constexpr std::int64_t ALIGNMENT = 1024LL * 1024 * 1024;
        const auto hidden = data->total - data->visible;
        if (hidden <= ALIGNMENT) {
            data->total = data->visible; // only the alignment of the partition table: not a part of the card to show
        }

        const auto emu = ReadEmuConfig();
        const bool emu_nintendo_own = !emu.nintendo.empty() && emu.nintendo != "/Nintendo";

        struct Part { Id id; std::vector<std::string> paths; };
        std::vector<Part> parts{
            {Id::Games, {"/Nintendo/Contents"}},
            {Id::Saves, {"/Nintendo/save", "/Nintendo/Album"}},
            {Id::System, {"/atmosphere", "/bootloader", "/config", "/kefir", "/sept"}},
            {Id::Homebrew, {"/switch"}},
            {Id::Backups, {"/dumps", "/backups", "/backup", "/JKSV"}},
            {Id::Files, {"/games", "/roms", "/ROMs", "/retroarch"}},
        };
        if (emu_nintendo_own) {
            parts.push_back({Id::EmuGames, {emu.nintendo + "/Contents"}});
        }
        if (!emu.partition && !emu.path.empty()) {
            parts.push_back({Id::EmuData, {emu.path}});
        }

        fs::FsNativeSd fs;
        std::vector<Slice> found;
        for (size_t i = 0; i < parts.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(Name(parts[i].id));
            pbox->SetOverall(static_cast<s64>(i), static_cast<s64>(parts.size()));
            std::int64_t bytes = 0;
            for (const auto& path : parts[i].paths) {
                bytes += DirBytes(fs, fs::FsPath{path}, 0, pbox);
            }
            found.push_back({parts[i].id, bytes});
        }
        // the emuMMC's games sit inside its folder when the Nintendo folder is a subfolder of it: count them once
        std::int64_t emu_games = 0;
        for (const auto& s : found) {
            if (s.id == Id::EmuGames) {
                emu_games = s.bytes;
            }
        }
        for (auto& s : found) {
            if (s.id == Id::EmuData && !emu.nintendo.empty() && emu.nintendo.starts_with(emu.path + "/")) {
                s.bytes = std::max<std::int64_t>(0, s.bytes - emu_games);
            }
        }
        if (hidden > ALIGNMENT) {
            found.push_back({Id::EmuPartition, hidden});
        }
        data->slices = Build(data->total, data->free_bytes, found);
        R_SUCCEED();
    }, [data](Result rc) {
        if (R_FAILED(rc)) {
            return;
        }
        App::Push<Menu>(data);
    });
}

} // namespace sphaira::ui::menu::storage
