// Host test for sphaira/include/release_plan.hpp and release_choice.hpp.
// The cases are DBI Backend Qt's tests/test_release_analyzer.py and test_release_bundle.py
// (layouts copied from real releases), so the console and the PC agree on a folder.

#include "release_choice.hpp"

#include <cassert>
#include <cstdio>
#include <map>

using namespace sphaira::release;
using Paths = std::vector<std::string>;

static const std::string BASE = "Game [0100B7C00933A000][v0].nsp";
static const std::string UPD4 = "Game [0100B7C00933A800][v262144].nsp";
static constexpr uint64_t TID = 0x0100B7C00933A000;

static auto Archives(std::map<std::string, Paths> table) -> ListArchive {
    return [table](const std::string& path) -> std::optional<Paths> {
        const auto it = table.find(path);
        return it == table.end() ? std::nullopt : std::optional{it->second};
    };
}

static auto OnlyGame(const Release& rel) -> const Game& {
    assert(rel.games.size() == 1);
    return rel.games[0];
}

static auto Opt(const Game& g, std::string_view name) -> const Option& {
    for (const auto& o : g.options) {
        if (o.name == name) return o;
    }
    std::printf("no option %.*s\n", static_cast<int>(name.size()), name.data());
    assert(false);
    return g.options[0];
}

static auto Names(const std::vector<Package>& packages) -> Paths {
    Paths out;
    for (const auto& p : packages) out.push_back(p.name());
    return out;
}

static auto UpdateVersions(const Plan& plan) -> std::vector<uint32_t> {
    std::vector<uint32_t> out;
    for (const auto& p : plan.packages) {
        if (p.type() == Package::Type::Update) out.push_back(p.version);
    }
    return out;
}

static auto AllKeys(const Game& g) -> Keys {
    Keys out;
    for (const auto& o : g.options) out.insert(o.key);
    return out;
}

static void TestPackageNames() {
    assert(ParsePackage("a/X [0100F7E00C70F001][v0x10000].nsp").version == 0x10000);
    assert(ParsePackage("X [010055D009F79004]v196608].nsp").version == 196608);
    const auto p = ParsePackage("Pokemon Scarlet [DLC Set] [0100A3D008C5D001][v0].nsp");
    assert(p.type() == Package::Type::Dlc && p.game_tid() == 0x0100A3D008C5C000);
    assert(ParsePackage("Upd [0100A3D008C5C800][v786432].nsp").type() == Package::Type::Update);
    assert(ParsePackage("Port [01909cd6d76c0000].nsp").type() == Package::Type::Base);
    assert(!ParsePackage("Taiko DLC.nsp").tid);
    // Simpsons Russian port stub: a random title ID that ends in D99 is a base, not a DLC
    assert(ParsePackage("Simpsons [05ab6d81a77b6d99].nsp").type() == Package::Type::Base);

    assert(DetectLang("Russian version") == "ru" && DetectLang("switch_RUSS") == "ru");
    assert(DetectLang("Українська (UKR)") == "uk" && DetectLang("РУССКАЯ версия") == "ru");
    assert(DetectLang("Addon - English Voiceover") == "en" && DetectLang("Trust").empty());
}

static void TestBaseUpdateDlc() {
    const auto rel = Analyze({BASE, "Game [0100B7C00933A800][v65536].nsp", UPD4, "DLC/Game [0100B7C00933B001][v0].nsp"});
    const auto& g = OnlyGame(rel);
    assert(g.base->name() == BASE && g.dlcs.size() == 1 && g.name() == "Game" && !IsSmart(rel));
    const auto p = MakePlan(g, DefaultChoice(g));
    assert((Names(p.packages) == Paths{BASE, UPD4, "Game [0100B7C00933B001][v0].nsp"}));
}

static void TestModWithOwnUpdateReplacesRootUpdate() {
    // Pikmin 4: root update v4, the translation needs v2
    const std::string mod = "Russian Machine Translation (26.03.2025)";
    const auto rel = Analyze({BASE, UPD4, mod + "/atmosphere/contents/0100B7C00933A000/romfs/a.bin",
                              mod + "/Game [0100B7C00933A800][v131072].nsp", mod + "/ReadMe_Rus.txt"});
    const auto& g = OnlyGame(rel);
    const auto& m = Opt(g, mod);
    assert(m.kind == Kind::Mod && m.lang == "ru" && m.updates.size() == 1 && m.updates[0].version == 131072);
    assert(DefaultChoice(g) == Keys{m.key} && IsSmart(rel));
    const auto with_mod = MakePlan(g, {m.key});
    assert(UpdateVersions(with_mod) == std::vector<uint32_t>{131072});
    assert(with_mod.sd_files[0].dst == "atmosphere/contents/0100B7C00933A000/romfs/a.bin");
    const auto without = MakePlan(g, {});
    assert(UpdateVersions(without) == std::vector<uint32_t>{262144} && without.sd_files.empty());
    // the queue gets the mod's update, never both
    Choice choice{{TID, {m.key}}};
    assert((Packages(rel, choice) == Paths{BASE, mod + "/Game [0100B7C00933A800][v131072].nsp"}));
}

static void TestSeveralModsNoneByDefaultAndUpdateConflict() {
    // Star Wars Dark Forces: three mods; Pokemon Z-A: two mods needing different updates
    Paths files{BASE, UPD4};
    for (const auto& [name, ver] : {std::pair{"Text Mod 1", 65536}, std::pair{"Voice Mod", 65536}, std::pair{"HD Textures", 196608}}) {
        files.push_back(std::string{name} + "/atmosphere/contents/0100B7C00933A000/romfs/" + name + ".bin");
        files.push_back(std::string{name} + "/Game [0100B7C00933A800][v" + std::to_string(ver) + "].nsp");
    }
    const auto rel = Analyze(files);
    const auto& g = OnlyGame(rel);
    assert(DefaultChoice(g).empty());
    const Keys same_keys{Opt(g, "Text Mod 1").key, Opt(g, "Voice Mod").key};
    const auto same = MakePlan(g, same_keys);
    assert(same.conflicts.empty() && same.sd_files.size() == 2);
    const Keys clash_keys{Opt(g, "Text Mod 1").key, Opt(g, "HD Textures").key};
    assert(MakePlan(g, clash_keys).conflicts[0].type == Conflict::Type::DifferentUpdates);
    assert(!Blocking(rel, {{TID, same_keys}}) && Blocking(rel, {{TID, clash_keys}}));
}

static void TestGameGroupsAndSharedModFolder() {
    // Pokemon Scarlet and Violet: a folder per game, one translation folder for both
    const std::string mod = "Russian Machine Translation";
    const auto rel = Analyze({
        "Pokemon Scarlet/Pokemon Scarlet [0100A3D008C5C000][v0].nsp",
        "Pokemon Scarlet/Pokemon Scarlet [0100A3D008C5C800][v786432].nsp",
        "Pokemon Violet/Pokemon Violet [01008F6008C5E000][v0].nsp",
        "Pokemon Violet/Pokemon Violet [01008F6008C5E800][v786432].nsp",
        mod + "/atmosphere/contents/0100A3D008C5C000/romfs/s.bin",
        mod + "/atmosphere/contents/01008F6008C5E000/romfs/v.bin",
        mod + "/Pokemon Scarlet [0100A3D008C5C800][v720896].nsp",
        mod + "/Pokemon Violet [01008F6008C5E800][v720896].nsp",
    });
    assert(rel.games.size() == 2 && rel.games[0].name() == "Pokemon Scarlet" && rel.games[1].name() == "Pokemon Violet");
    const char* own[] = {"s.bin", "v.bin"};
    for (size_t i = 0; i < 2; i++) {
        const auto& g = rel.games[i];
        const auto& m = Opt(g, mod);
        assert(m.files.size() == 1 && m.files[0].second.ends_with(own[i]));
        assert(m.updates.size() == 1 && m.updates[0].game_tid() == g.tid);
        assert(MakePlan(g, {m.key}).packages[1].version == 720896);
    }
}

static void TestPorts() {
    // Def Jam: stub NSP + defjamx/ with the NRO, copied to switch/defjamx/
    {
        const auto rel = Analyze({"Def Jam [01909cd6d76c0000].nsp", "defjamx/defjamx.nro", "defjamx/defjamx_env.txt", "defjamx/game/assets/a.viv"});
        const auto& g = OnlyGame(rel);
        const auto& port = Opt(g, "defjamx");
        assert(port.kind == Kind::Port);
        assert((port.files == std::vector<FilePair>{{"defjamx/defjamx.nro", "switch/defjamx/defjamx.nro"},
            {"defjamx/defjamx_env.txt", "switch/defjamx/defjamx_env.txt"}, {"defjamx/game/assets/a.viv", "switch/defjamx/game/assets/a.viv"}}));
        assert(DefaultChoice(g) == Keys{port.key});
    }
    // the user picked the port's own folder: the NRO is at the top, the folder name is the port
    {
        const auto rel = Analyze({"defjamx.nro", "game/a.viv"}, {}, "defjamx");
        assert(rel.games.empty() && rel.orphans.size() == 1 && rel.orphans[0].kind == Kind::Port && rel.orphans[0].name == "defjamx");
        assert((rel.orphans[0].files == std::vector<FilePair>{{"defjamx.nro", "switch/defjamx/defjamx.nro"}, {"game/a.viv", "switch/defjamx/game/a.viv"}}));
        assert(Defaults(rel)[ORPHANS] == Keys{"."});
        assert(Analyze({"defjamx.nro"}).orphans.empty()); // no name: nowhere to put it
    }
    // NFS Underground 2: Homebrew (0.5.6)/switch/... + switch.7z + switch_russmod.7z
    {
        const Paths data{"switch/nfsu2x/game/B3/Data/a.bin", "switch/nfsu2x/game/B3/Data/b.bin"};
        const auto rel = Analyze({"NFSU2 [03DB1E5631154000][v0].nsp", "Homebrew (0.5.6)/switch/nfsu2x/nfsu2x.nro",
            "Homebrew (0.5.6)/switch/nfsu2x/nfsu2x_env.txt", "switch.7z", "switch_russmod.7z"},
            Archives({{"switch.7z", data}, {"switch_russmod.7z", data}}));
        const auto& g = OnlyGame(rel);
        assert(g.options.size() == 3 && g.options[0].name == "Homebrew (0.5.6)" && g.options[1].name == "switch" && g.options[2].name == "switch_russmod");
        assert((DefaultChoice(g, "uk") == Keys{"Homebrew (0.5.6)", "switch.7z"}));
        // Russian: its archive writes every file of switch.7z, so switch.7z is not copied first
        assert((DefaultChoice(g, "ru") == Keys{"Homebrew (0.5.6)", "switch_russmod.7z"}));
    }
    // Professor Layton: switch.7z has all data, switch_rus.zip replaces part of it
    {
        const auto rel = Analyze({"Layton [01d07340e2190000].nsp", "Homebrew (1.0.4)/switch/layton/layton_nx.nro", "switch.7z", "switch_rus.zip"},
            Archives({{"switch.7z", {"switch/layton/a.png", "switch/layton/b.png"}}, {"switch_rus.zip", {"switch/layton/a.png"}}}));
        const auto& g = OnlyGame(rel);
        const auto chosen = DefaultChoice(g, "ru");
        assert((chosen == Keys{"Homebrew (1.0.4)", "switch.7z", "switch_rus.zip"}));
        Paths order;
        for (const auto& f : MakePlan(g, chosen).sd_files) order.push_back(f.option->name);
        assert((order == Paths{"Homebrew (1.0.4)", "switch", "switch_rus"})); // the overlay is copied last
        assert((DefaultChoice(g, "uk") == Keys{"Homebrew (1.0.4)", "switch.7z"}));
    }
    // Call of Duty 4 / Simpsons: English version/ and Russian version/
    {
        const auto rel = Analyze({"CoD4 [01B7F96B70C31000][v0].nsp", "Homebrew (12.10.2026)/switch/codmw/codmw.nro",
            "English version/switch/codmw/game/en.pak", "Russian version/switch/codmw/game/ru.pak"});
        const auto& g = OnlyGame(rel);
        assert((DefaultChoice(g, "uk") == Keys{"Homebrew (12.10.2026)", "English version"}));
        assert((DefaultChoice(g, "ru") == Keys{"Homebrew (12.10.2026)", "Russian version"}));
    }
    // Silent Hill 2: the port's data exists only as switch_RUSS.7z; it must still be copied
    {
        const auto rel = Analyze({"SH2 [057DA68233EAB000][v0].nsp", "Homebrew (v1)/switch/sh2/sh2.nro", "switch_RUSS.7z"},
            Archives({{"switch_RUSS.7z", {"switch/sh2/data/a.bin"}}}));
        assert((DefaultChoice(OnlyGame(rel), "uk") == Keys{"Homebrew (v1)", "switch_RUSS.7z"}));
    }
    // Dusk: textures archive has dusk/... at its top, the port folder is dusk/
    {
        const auto rel = Analyze({"Dusk [01a43f49196b0000].nsp", "dusk/dusk.nro", "Optional HD Textures.7z"},
            Archives({{"Optional HD Textures.7z", {"dusk/texture_replacements/UI/a.png"}}}));
        const auto& g = OnlyGame(rel);
        const auto& hd = Opt(g, "Optional HD Textures");
        assert((hd.files == std::vector<FilePair>{{"dusk/texture_replacements/UI/a.png", "switch/dusk/texture_replacements/UI/a.png"}}));
        assert(!DefaultChoice(g).contains(hd.key));
    }
}

static void TestModArchivesAndAddons() {
    // Omori: Russian Language Mod (5.05.2023)/atmosphere.7z + update NSP beside it
    {
        const std::string mod = "Russian Language Mod (5.05.2023)";
        const auto rel = Analyze({BASE, UPD4, mod + "/atmosphere.7z", mod + "/Game [0100B7C00933A800][v196608].nsp"},
            Archives({{mod + "/atmosphere.7z", {"atmosphere/contents/0100B7C00933A000/romfs/x"}}}));
        const auto& m = Opt(OnlyGame(rel), mod);
        assert(m.archive == mod + "/atmosphere.7z" && m.updates.size() == 1 && m.updates[0].version == 196608);
    }
    // Phoenix Wright: main mod archive + update, add-on folders below it, <TID>/romfs without atmosphere
    {
        const std::string mod = "Russian Language Mod";
        const auto rel = Analyze({
            BASE, UPD4, mod + "/atmosphere.zip", mod + "/Game [0100B7C00933A800][v131072].nsp",
            mod + "/Addon - English Voiceover/atmosphere/contents/0100B7C00933A000/romfs/v.bin",
            mod + "/Addon - Unlock/Modified scripts/0100B7C00933A000/romfs/s.bin",
            mod + "/Addon - Gamesave/systemdata",
        }, Archives({{mod + "/atmosphere.zip", {"atmosphere/contents/0100B7C00933A000/romfs/t.bin"}}}));
        const auto& g = OnlyGame(rel);
        const auto& addon = Opt(g, "Addon - English Voiceover");
        assert(addon.lang == "en" && addon.updates.size() == 1 && addon.updates[0].version == 131072);
        const auto& main = Opt(g, mod);
        assert(addon.parent == main.key && main.parent.empty());
        assert(DefaultChoice(g) == Keys{main.key}); // one main mod; add-ons stay off
        const auto& save = Opt(g, "Addon - Gamesave"); // a console save, the mod's add-on
        assert(save.kind == Kind::Save && save.parent == main.key && save.files[0].second == "@save/systemdata");
        assert(MakePlan(g, {addon.key}).conflicts[0].type == Conflict::Type::NeedsParent);
        const auto both = MakePlan(g, {main.key, addon.key});
        assert(both.conflicts.empty() && both.sd_files.back().option == &addon); // add-on copied last
        const auto& scripts = Opt(g, "Modified scripts");
        assert(scripts.files[0].second == "atmosphere/contents/0100B7C00933A000/romfs/s.bin");
        assert(scripts.updates.size() == 1 && scripts.updates[0].version == 131072);

        // ticking an add-on pulls its mod in; unticking the mod drops the add-on
        Choice choice;
        Toggle(rel, choice, TID, addon.key, true);
        assert((choice[TID] == Keys{main.key, addon.key}));
        Toggle(rel, choice, TID, main.key, false);
        assert(choice[TID].empty());
        assert(BundleName(g.name(), g.tid, save) == "Game - Addon - Gamesave 0100B7C00933A000.kefir-save.tar");
        assert(BundleName(g.name(), g.tid, main) == "Game - Russian Language Mod.kefir-sd.tar");
        assert(BundleName("Shared", 0, save).empty());
    }
    // DQ XI: an archive with another pakchunk0-Switch_p.pak for the mod beside it
    {
        const std::string mod = "Russian Language Mod";
        const std::string pak = "atmosphere/contents/0100B7C00933A000/romfs/JackGame/Content/Paks/pakchunk0-Switch_p.pak";
        const std::string zip = mod + "/better_russian_font_pakchunk0-Switch_p.pak_duckbill.zip";
        const auto rel = Analyze({BASE, mod + "/" + pak, zip}, Archives({{zip, {"pakchunk0-Switch_p.pak"}}}));
        const auto& g = OnlyGame(rel);
        const auto& font = Opt(g, "better_russian_font_pakchunk0-Switch_p.pak_duckbill");
        assert((font.files == std::vector<FilePair>{{"pakchunk0-Switch_p.pak", pak}}) && font.parent == mod);
        assert(DefaultChoice(g) == Keys{mod});
        const auto sd = MakePlan(g, {mod, font.key}).sd_files;
        assert(sd.size() == 1 && sd[0].option == &font && sd[0].src == "pakchunk0-Switch_p.pak" && sd[0].dst == pak);
    }
}

static void TestModLayouts() {
    {
        const auto rel = Analyze({BASE, "Mod/atmosphere/titles/0100B7C00933A000/romfs/a"});
        const auto& g = OnlyGame(rel);
        assert(g.options[0].kind == Kind::Mod && g.options[0].tids == std::set<uint64_t>{TID});
    }
    {   // Yakuza Kiwami: an .nro inside the game's romfs does not make the mod a port
        const auto rel = Analyze({BASE, "Mod/atmosphere/contents/0100B7C00933A000/romfs/module/m.nro"});
        assert(OnlyGame(rel).options[0].kind == Kind::Mod);
    }
    {   // N.O.V.A. 3: exefs.nsp under atmosphere/contents is an SD file, not a package to install
        const auto rel = Analyze({"NOVA [0570F3D9B545F000][v0].nsp", "Homebrew (1.0.1)/atmosphere/contents/0570F3D9B545F000/exefs.nsp",
                                  "Homebrew (1.0.1)/switch/nova3/nova3.nro"});
        const auto& g = OnlyGame(rel);
        assert(g.options[0].kind == Kind::Port && g.options[0].files.size() == 2 && g.updates.empty() && g.dlcs.empty());
    }
    {   // Metroid Prime: the base is still downloading (.!qB), the mod is complete
        const auto rel = Analyze({"Metroid [010012101468C000][v0] (6.76 GB).nsz.!qB", "Russian Language Mod/atmosphere/contents/010012101468C000/romfs/a"});
        const auto& g = OnlyGame(rel);
        assert(!g.base && g.name() == "010012101468C000" && rel.skipped[0].second == Skip::Partial);
    }
    {   // a mod for a title that is not in the release is flagged and not chosen
        const auto rel = Analyze({BASE, "Mod/atmosphere/contents/0100000000001000/romfs/a"});
        const auto& g = OnlyGame(rel);
        const auto& m = g.options[0];
        assert(m.tid_mismatch && DefaultChoice(g).empty());
        assert(MakePlan(g, {m.key}).conflicts[0].type == Conflict::Type::TidMismatch);
    }
    {   // a mod still downloading is not chosen, and cannot be installed
        const auto rel = Analyze({BASE, "Mod/atmosphere/contents/0100B7C00933A000/romfs/a", "Mod/atmosphere/contents/0100B7C00933A000/romfs/b.pak.!qB"});
        const auto& g = OnlyGame(rel);
        assert(g.options[0].incomplete && DefaultChoice(g).empty() && Blocking(rel, {{TID, AllKeys(g)}}));
    }
    {   // the user picked a bare <TID> folder's parent: romfs without atmosphere/contents above it
        const auto rel = Analyze({"0100B7C00933A000/romfs/a.bin"});
        const auto& g = OnlyGame(rel);
        assert(g.options[0].files[0].second == "atmosphere/contents/0100B7C00933A000/romfs/a.bin" && DefaultChoice(g) == AllKeys(g));
    }
}

static void TestSharedPortsSkippedUnknown() {
    {   // Donkey Kong Country Trilogy: three stubs and one switch/ with three NRO folders
        const auto rel = Analyze({"DKC 1 [03DB1DA917792000][v0].nsp", "DKC 2 [03DB1CAB43D78000][v0].nsp", "switch/dkc1/dkc1.nro", "switch/dkc2/dkc2.nro"});
        assert(rel.games.size() == 2 && rel.games[0].options.empty() && rel.games[1].options.empty());
        assert(rel.orphans.size() == 2 && rel.orphans[0].name == "dkc1" && rel.orphans[1].name == "dkc2" && rel.orphans[1].kind == Kind::Port);
        assert((rel.orphans[1].files == std::vector<FilePair>{{"switch/dkc2/dkc2.nro", "switch/dkc2/dkc2.nro"}}));
        assert(Defaults(rel)[ORPHANS].size() == 2);
    }
    {
        const auto rel = Analyze({BASE, "setup.exe", "SwitchEmu/Firmware 21.0.0.zip", "Keys 21.0.0.7z", "font.zip", "x.7z", "Taiko DLC.nsp", "monkeys/a.txt"},
            Archives({{"font.zip", {"pakchunk0-Switch_p.pak"}}, {"x.7z", {"Game [0100B7C00933A800][v0].nsp"}}}));
        std::map<std::string, Skip> reasons(rel.skipped.begin(), rel.skipped.end());
        assert(reasons.at("setup.exe") == Skip::NotForConsole && reasons.at("Keys 21.0.0.7z") == Skip::NotForConsole);
        assert(reasons.at("SwitchEmu/Firmware 21.0.0.zip") == Skip::NotForConsole && !reasons.contains("monkeys/a.txt"));
        assert(reasons.at("font.zip") == Skip::UnknownLayout && reasons.at("x.7z") == Skip::PackageInArchive);
        assert(rel.unknown == Paths{"Taiko DLC.nsp"});
        assert(Analyze({"m.7z"}).skipped[0].second == Skip::ArchiveUnreadable);
    }
    {   // a later option overwrites the same file
        const auto rel = Analyze({"P [01d07340e2190000].nsp", "A/switch/p/x.bin", "B (rus)/switch/p/x.bin"});
        const auto& g = OnlyGame(rel);
        const auto sd = MakePlan(g, AllKeys(g)).sd_files;
        assert(sd.size() == 1 && sd[0].src == "B (rus)/switch/p/x.bin");
    }
    {   // NFSU2 with a DLC still downloading next to the port data
        const auto rel = Analyze({"P [03DB1E5631154000][v0].nsp", "P [03DB1E5631155001][v0].nsp.!qB", "Homebrew/switch/p/p.nro", "switch.7z"},
            Archives({{"switch.7z", {"switch/p/data.bin"}}}));
        const auto& g = OnlyGame(rel);
        assert(!g.options[0].incomplete && !g.options[1].incomplete);
        assert((DefaultChoice(g) == Keys{"Homebrew", "switch.7z"}));
    }
    {   // Simpsons: two Russian voice mods for the port write the same files
        const auto rel = Analyze({"S [05ab6d81a77b6d99].nsp", "switch/simpsons/simpsons.nro",
            "Russian Voice Mod (PC_Bkmz)/switch/simpsons/dialog.rcf", "Russian Voice Mod (PS2_Vector)/switch/simpsons/dialog.rcf"});
        assert(DefaultChoice(OnlyGame(rel), "ru") == Keys{"."});
        const auto one = Analyze({"S [05ab6d81a77b6d99].nsp", "switch/simpsons/simpsons.nro", "Russian Voice Mod (PC_Bkmz)/switch/simpsons/dialog.rcf"});
        assert((DefaultChoice(OnlyGame(one), "ru") == Keys{".", "Russian Voice Mod (PC_Bkmz)"}));
        assert(DefaultChoice(OnlyGame(one), "uk") == Keys{"."});
    }
    {   // Simpsons Hit & Run: the English archive holds the same files as the newer "Homebrew Update"
        const auto rel = Analyze({"Port [018fcc923d8d0000].nsp", "switch (v0.4 Stable) [ENG].7z", "Homebrew Update (v0.6.1)/switch/simpsons/simpsons.nro"},
            Archives({{"switch (v0.4 Stable) [ENG].7z", {"switch/simpsons/simpsons.nro", "switch/simpsons/data/a.rcf"}}}));
        const auto& g = OnlyGame(rel);
        const auto& upd = Opt(g, "Homebrew Update (v0.6.1)");
        assert(DefaultChoice(g).contains(upd.key));
        assert(MakePlan(g, DefaultChoice(g)).sd_files.back().option == &upd); // copied over the older archive
    }
}

static void TestBundles() {
    using sphaira::sd_bundle::Rule;
    const auto same = [](const std::vector<Rule>& a, const std::vector<std::pair<std::string, std::string>>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](auto& r, auto& p) { return r.src == p.first && r.dst == p.second; });
    };
    assert(same(ArchiveRules({{"sdcard/switch/p/a.nro", "switch/p/a.nro"}, {"sdcard/switch/p/d/b", "switch/p/d/b"}}), {{"sdcard/switch/", "switch/"}}));
    assert(same(ArchiveRules({{"font.pak_duckbill", "atmosphere/contents/X/romfs/Paks/p.pak"}}), {{"font.pak_duckbill", "atmosphere/contents/X/romfs/Paks/p.pak"}}));
    assert(same(ArchiveRules({{"a\\b.bin", "atmosphere/contents/X/romfs/a/b.bin"}}), {{"a/", "atmosphere/contents/X/romfs/a/"}}));
    assert(same(ArchiveRules({{"switch/p/a.nro", "switch/p/a.nro"}}), {{"switch/", "switch/"}})); // never "" -> ""
    // the rules place the entries where the analysis said
    const auto rules = ArchiveRules({{"dusk/texture_replacements/UI/a.png", "switch/dusk/texture_replacements/UI/a.png"}});
    assert(sphaira::sd_bundle::MapEntry("dusk/texture_replacements/UI/a.png", rules) == "switch/dusk/texture_replacements/UI/a.png");

    // Just Dance 2025 offline mod: SD_Cache.NNNN folders and a game save folder go into the
    // console's save storages with the game id, not to the microSD
    const std::map<std::string, int64_t> sizes{
        {"Cache Data/SD_Cache.0000/g1/a.bin", 100}, {"Cache Data/SD_Cache.0000/g1/b.bin", 0x4001},
        {"Cache Data/SD_Cache.0008/g1/a.bin", 10}, {"Offline Gamesave Data/OfflineSaveNx.txt", 4},
    };
    Paths files{"JD [0100BEE017FC0000][v0].nsp"};
    for (const auto& [path, size] : sizes) files.push_back(path);
    const auto rel = Analyze(files);
    const auto& g = OnlyGame(rel);
    const auto size_of = [&](const std::string& src) { return sizes.at(src); };
    const auto& cache = Opt(g, "Cache Data");
    const auto& save = Opt(g, "Offline Gamesave Data");
    assert(cache.kind == Kind::Cache && save.kind == Kind::Save && Defaults(rel)[g.tid] == AllKeys(g));
    assert(BundleName(g.name(), g.tid, cache) == "JD - Cache Data 0100BEE017FC0000.kefir-cache.tar");
    assert(sphaira::save_bundle::TitleOf(BundleName(g.name(), g.tid, save)) == 0x0100BEE017FC0000);
    const auto members = BundleMembers(cache, size_of);
    assert(members.size() == 4 && members[0].name == ".kefir/sizes" && members[0].data == "0\t49152\n8\t16384\n");
    assert(members[1].name == "0/g1/a.bin" && members[2].name == "0/g1/b.bin" && members[3].name == "8/g1/a.bin");
    assert(members[3].src == "Cache Data/SD_Cache.0008/g1/a.bin" && members[3].size == 10);
    const auto save_members = BundleMembers(save, size_of);
    assert(save_members.size() == 1 && save_members[0].name == "OfflineSaveNx.txt" && save_members[0].size == 4);

    // a folder that is already in place must not be written over itself
    assert(IsSamePath("/switch", "defjamx/a.nro", "switch/defjamx/a.nro"));
    assert(IsSamePath("/", "Switch/defjamx/a.nro", "switch/defjamx/a.nro"));
    assert(!IsSamePath("/Download/Def Jam", "defjamx/a.nro", "switch/defjamx/a.nro"));
}

int main() {
    TestPackageNames();
    TestBaseUpdateDlc();
    TestModWithOwnUpdateReplacesRootUpdate();
    TestSeveralModsNoneByDefaultAndUpdateConflict();
    TestGameGroupsAndSharedModFolder();
    TestPorts();
    TestModArchivesAndAddons();
    TestModLayouts();
    TestSharedPortsSkippedUnknown();
    TestBundles();
    std::puts("test_release_plan: ok");
    return 0;
}
