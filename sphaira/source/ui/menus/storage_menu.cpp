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

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::storage {
namespace {

using namespace sphaira::storage;

struct Source {
    Id id;
    std::vector<const char*> paths;
};

const Source SOURCES[] = {
    {Id::Games, {"/Nintendo/Contents"}},
    {Id::Saves, {"/Nintendo/save", "/Nintendo/Album"}},
    {Id::Emummc, {"/emuMMC"}},
    {Id::System, {"/atmosphere", "/bootloader", "/config", "/kefir", "/sept"}},
    {Id::Homebrew, {"/switch"}},
    {Id::Backups, {"/dumps", "/backups", "/backup", "/JKSV"}},
    {Id::Files, {"/games", "/roms", "/ROMs", "/retroarch"}},
};

struct Data {
    std::int64_t total{};
    std::int64_t free_bytes{};
    std::int64_t nand_total{};
    std::int64_t nand_free{};
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
        case Id::Games: return "Installed games"_i18n;
        case Id::Saves: return "Saves and album"_i18n;
        case Id::Emummc: return "emuMMC (files)"_i18n;
        case Id::System: return "Atmosphère and Kefir"_i18n;
        case Id::Homebrew: return "Homebrew"_i18n;
        case Id::Backups: return "Dumps and backups"_i18n;
        case Id::Files: return "Game files and ROMs"_i18n;
        case Id::Other: return "Other files"_i18n;
        case Id::Free: return "Free space"_i18n;
    }
    return {};
}

// what it is, whether it can go, where it is, why it is there
auto Card(Id id) -> std::string {
    switch (id) {
        case Id::Games: return "What: the games installed to the microSD card (the content of every installed title).\n\nDelete: yes, from Tools → Games (Delete). The game's saves stay. The same game can be installed again later.\n\nWhere: /Nintendo/Contents on the card. The card also holds the records of the installed games in each system (sysMMC and emuMMC keep their own list).\n\nWhy: installing to the card keeps the small system memory free."_i18n;
        case Id::Saves: return "What: game saves kept on the card and the album (screenshots and videos).\n\nDelete: the album can go once copied to a computer. Delete saves only from the Saves menu: a save cannot be recovered.\n\nWhere: /Nintendo/save and /Nintendo/Album.\n\nWhy: the console writes the album here; saves land here when a game uses card-based saves."_i18n;
        case Id::Emummc: return "What: the emuMMC when it is a file or folder on the card (a partition emuMMC does not show here: it is outside the card's file system).\n\nDelete: no, unless you give up the emuMMC. It holds a whole copy of the system with its games' records and saves.\n\nWhere: /emuMMC.\n\nWhy: a separate system to play and install in without touching the original one."_i18n;
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
        case Id::Saves: return nvgRGB(0xE0, 0x9A, 0x2B);
        case Id::Emummc: return nvgRGB(0x8E, 0x5B, 0xD6);
        case Id::System: return nvgRGB(0xD9, 0x4E, 0x4E);
        case Id::Homebrew: return nvgRGB(0x3F, 0xB5, 0x8B);
        case Id::Backups: return nvgRGB(0x2E, 0xB8, 0xC9);
        case Id::Files: return nvgRGB(0xC7, 0x7D, 0xB4);
        case Id::Other: return nvgRGB(0x8A, 0x93, 0x9F);
        case Id::Free: return nvgRGB(0x33, 0x3B, 0x46);
    }
    return nvgRGB(255, 255, 255);
}

struct Menu final : MenuBase {
    explicit Menu(std::shared_ptr<Data> data) : MenuBase{"Storage manager"_i18n, MenuFlag_None}, m_data{std::move(data)} {
        SetAction(Button::A, Action{"Open"_i18n, [this]() {
            const auto& s = m_data->slices[m_index];
            const auto text = Name(s.id) + ": " + grid::FormatBytes(static_cast<u64>(s.bytes)) + "\n\n" + Card(s.id);
            App::Push<OptionBox>(text, "OK"_i18n);
        }});
        SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (!controller) {
            return;
        }
        const auto n = static_cast<s64>(m_data->slices.size());
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
        const float cx = 300.f, cy = 370.f, r_out = 190.f, r_in = 120.f;

        // the ring: a slice is an arc, the chosen one stands out a little
        const float full = 2.f * static_cast<float>(M_PI);
        float angle = -static_cast<float>(M_PI) / 2.f;
        for (size_t i = 0; i < d.slices.size(); i++) {
            const auto& s = d.slices[i];
            const float span = sum > 0 ? full * static_cast<float>(s.bytes) / static_cast<float>(sum) : 0.f;
            if (span <= 0.f) {
                continue;
            }
            const float out = static_cast<s64>(i) == m_index ? r_out + 10.f : r_out;
            nvgBeginPath(vg);
            nvgArc(vg, cx, cy, out, angle, angle + span, NVG_CW);
            nvgArc(vg, cx, cy, r_in, angle + span, angle, NVG_CCW);
            nvgClosePath(vg);
            nvgFillColor(vg, Colour(s.id));
            nvgFill(vg);
            angle += span;
        }
        const auto used = std::max<std::int64_t>(0, d.total - d.free_bytes);
        gfx::drawTextArgs(vg, cx, cy - 14.f, 30.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%s", grid::FormatBytes(static_cast<u64>(used)).c_str());
        gfx::drawTextArgs(vg, cx, cy + 20.f, 18.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s %s", "of"_i18n.c_str(), grid::FormatBytes(static_cast<u64>(d.total)).c_str());

        // the slices as a list; A on one opens its card
        float y = 150.f;
        for (size_t i = 0; i < d.slices.size(); i++, y += 50.f) {
            const bool sel = static_cast<s64>(i) == m_index;
            if (sel) {
                gfx::drawRect(vg, 560.f, y - 22.f, 650.f, 44.f, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
                gfx::drawRectOutline(vg, theme, 4.f, 560.f, y - 22.f, 650.f, 44.f);
            }
            gfx::drawRect(vg, 580.f, y - 10.f, 20.f, 20.f, Colour(d.slices[i].id), 4.f);
            gfx::drawTextArgs(vg, 615.f, y, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(sel ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), "%s", Name(d.slices[i].id).c_str());
            const auto pm = PerMille(d.slices[i], sum);
            gfx::drawTextArgs(vg, 1010.f, y, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%d.%d%%", pm / 10, pm % 10);
            gfx::drawTextArgs(vg, 1190.f, y, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%s", grid::FormatBytes(static_cast<u64>(d.slices[i].bytes)).c_str());
        }

        // the system memory has no file system to walk: its numbers are the console's own
        const auto nand_used = std::max<std::int64_t>(0, d.nand_total - d.nand_free);
        gfx::drawTextArgs(vg, 560.f, 600.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s / %s",
            "System memory"_i18n.c_str(), grid::FormatBytes(static_cast<u64>(nand_used)).c_str(), grid::FormatBytes(static_cast<u64>(d.nand_total)).c_str());
    }

    std::shared_ptr<Data> m_data;
    s64 m_index{};
};

} // namespace

void Open() {
    auto data = std::make_shared<Data>();
    App::Push<ProgressBox>(0, "Counting"_i18n, "Storage manager"_i18n, [data](auto pbox) -> Result {
        const auto polled = MenuBase::GetPolledData(true);
        data->total = polled.sd_total;
        data->free_bytes = polled.sd_free;
        data->nand_total = polled.nand_total;
        data->nand_free = polled.nand_free;

        fs::FsNativeSd fs;
        std::vector<Slice> parts;
        const auto count = std::size(SOURCES);
        for (size_t i = 0; i < count; i++) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(Name(SOURCES[i].id));
            pbox->SetOverall(static_cast<s64>(i), static_cast<s64>(count));
            std::int64_t bytes = 0;
            for (const auto* path : SOURCES[i].paths) {
                bytes += DirBytes(fs, path, 0, pbox);
            }
            parts.push_back({SOURCES[i].id, bytes});
        }
        data->slices = Build(data->total, data->free_bytes, parts);
        R_SUCCEED();
    }, [data](Result rc) {
        if (R_FAILED(rc)) {
            return;
        }
        App::Push<Menu>(data);
    });
}

} // namespace sphaira::ui::menu::storage
