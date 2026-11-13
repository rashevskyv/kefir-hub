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

    // the apps of the Software menu update from their own GitHub release, found by the nro path
    assert(SourceOf("/switch/UAPlay.nro", hub) == AppSource::Github);
    assert(SourceOf("/switch/modcd/MODCD.nro", hub) == AppSource::Github);
    assert(SourceOf("/switch/UAPlay/UAPlay.nro", hub) == AppSource::Store);
    for (const auto& app : GITHUB_APPS) {
        assert(FindGithubApp(app.nro_path) == &app);
        assert(app.nro_path.ends_with(".nro") && app.nro_path.ends_with(app.asset));
    }
    const auto* uaplay = FindGithubApp("/switch/UAPlay.nro");
    assert(uaplay && uaplay->ukrainian_only);
    assert(GithubDownloadUrl(*uaplay) == "https://github.com/Small182/UAPlay-nx/releases/latest/download/UAPlay.nro");
    assert(GithubReleaseApiUrl(*uaplay) == "https://api.github.com/repos/Small182/UAPlay-nx/releases/latest");
    assert(GithubTempPath(*uaplay) == "/switch/UAPlay_new.nro");
    assert(GithubTempPath(*FindGithubApp("/switch/ModCD/ModCD.nro")) == "/switch/ModCD/ModCD_new.nro");

    // release tags as versions: the tags of the four repos today
    assert(ReleaseVersion("UAPlay_0.5.1") == "0.5.1");
    assert(ReleaseVersion("v1.6.0") == "1.6.0");
    assert(ReleaseVersion("2.3.0") == "2.3.0");
    assert(ReleaseVersion("latest").empty());
    assert(AppStateOf("0.5.0", ReleaseVersion("UAPlay_0.5.1")) == AppState::Update);
    assert(AppStateOf("0.5.1", ReleaseVersion("UAPlay_0.5.1")) == AppState::Latest);

    // the row: DBI 912 (Russian build) offers 921, 921 is the newest, a lower tag is never a downgrade offer
    assert(AppStateOf("912", "921") == AppState::Update);
    assert(AppStateOf("921", "921") == AppState::Latest);
    assert(AppStateOf("930", "921") == AppState::Latest);
    assert(AppStateOf("0.14.355", "0.14.356") == AppState::Update);
    assert(AppStateOf("0.14.356", "v0.14.356") == AppState::Latest);
    assert(AppStateOf("0.14.356", "") == AppState::NoInfo); // the release did not answer

    // RetroArch nightly: the buildbot's Last-Modified is the version
    const std::string built = "Mon, 26 Oct 2026 00:11:21 GMT";
    assert(HttpDateDay(built) == "2026-10-26");
    assert(HttpDateDay("Fri, 03 Jan 2025 10:00:00 GMT") == "2025-01-03");
    assert(HttpDateDay("garbage").empty());
    assert(HttpDateDay("").empty());
    assert(NightlyState(built, built) == AppState::Latest);
    assert(NightlyState("", built) == AppState::Update); // installed before the Hub kept a record
    assert(NightlyState("Sun, 25 Oct 2026 00:11:21 GMT", built) == AppState::Update);
    assert(NightlyState(built, "") == AppState::NoInfo); // the buildbot did not answer

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
