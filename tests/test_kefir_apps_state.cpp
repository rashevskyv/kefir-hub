#include "ui/menus/kefir/kefir_apps_state.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace sphaira::ui::menu::kefir;

    // the updater of each app: the Hub and DBI keep their own, case does not matter on the card
    const std::string hub = "/switch/kefir-hub/kefir-hub.nro";
    assert(SourceOf("/switch/kefir-hub/kefir-hub.nro", hub) == AppSource::Hub);
    assert(SourceOf("/switch/Kefir-Hub/KEFIR-HUB.nro", hub) == AppSource::Hub);
    assert(SourceOf("/switch/DBI/DBI.nro", hub) == AppSource::Dbi);
    assert(SourceOf("/switch/dbi/dbi.nro", hub) == AppSource::Dbi);
    assert(SourceOf("/switch/DBI/DBI_old.nro", hub) == AppSource::Store);
    assert(SourceOf("/switch/ftpd/ftpd.nro", hub) == AppSource::Store);
    assert(SourceOf("/hbmenu.nro", "/hbmenu.nro") == AppSource::Hub);

    // the row: DBI 912 (Russian build) offers 921, 921 is the newest, a lower tag is never a downgrade offer
    assert(AppStateOf("912", "921") == AppState::Update);
    assert(AppStateOf("921", "921") == AppState::Latest);
    assert(AppStateOf("930", "921") == AppState::Latest);
    assert(AppStateOf("0.14.355", "0.14.356") == AppState::Update);
    assert(AppStateOf("0.14.356", "v0.14.356") == AppState::Latest);
    assert(AppStateOf("0.14.356", "") == AppState::NoInfo); // the release did not answer

    // the DBI language record
    assert(DbiLanguageAsset("translation_ua.bin") == "translation_ua.bin");
    assert(DbiLanguageAsset("translation_ptbr.bin\r\n") == "translation_ptbr.bin");
    assert(DbiLanguageAsset("").empty());
    assert(DbiLanguageAsset("ua").empty());
    assert(DbiLanguageAsset("translation_../x.bin").empty());
    assert(DbiLanguageAsset("translation_ua.bin.zip").empty());

    std::cout << "test_kefir_apps_state: ok\n";
    return 0;
}
