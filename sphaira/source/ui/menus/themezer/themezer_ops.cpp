#include "ui/menus/themezer.hpp"
#include "ui/menus/themezer/themezer_internal.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "nro.hpp"
#include "threaded_file_transfer.hpp"
#include "title_info.hpp"
#include "ui/menus/ghdl.hpp"
#include "i18n.hpp"
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::themezer {
auto BuildScreenshotTitle(const PackListEntry& pack, const ThemeEntry& theme) -> std::string {
    std::string title = ThemeTargetLabel(theme);
    if (!theme.details.name.empty() && theme.details.name != title) {
        title += " - ";
        title += theme.details.name;
    } else if (title.empty()) {
        title = pack.details.name.empty() ? "Screenshot"_i18n : pack.details.name;
    }

    return title;
}

auto SanitizedPathPart(const std::string& value, const char* fallback) -> fs::FsPath {
    fs::FsPath out{value.empty() ? fallback : value.c_str()};
    title::utilsReplaceIllegalCharacters(out, false);
    return out;
}

auto BuildThemePath(const PackListEntry& entry, const ThemeEntry& theme) -> fs::FsPath {
    const auto pack_name = SanitizedPathPart(entry.details.name, entry.id.empty() ? "Themezer Pack" : entry.id.c_str());
    const auto pack_author = SanitizedPathPart(entry.creator.display_name, "Unknown");
    const auto theme_name = SanitizedPathPart(theme.details.name, theme.id.empty() ? "Theme" : theme.id.c_str());
    const auto target = SanitizedPathPart(ThemeTargetLabel(theme), "Theme");
    const auto id = SanitizedPathPart(theme.id, "theme");

    fs::FsPath out;
    std::snprintf(out, sizeof(out), "%s/%s - By %s/%s (%s-%s).nxtheme", THEME_FOLDER.s, pack_name.s, pack_author.s, theme_name.s, target.s, id.s);
    return out;
}


auto GetNroPath() -> const char* {
    fs::FsNativeSd fs;
    for (auto& path : NRO_PATHS) {
        if (fs.FileExists(path)) {
            return path;
        }
    }

    return nullptr;
}

auto HasNro() -> bool {
    return GetNroPath() != nullptr;
}

void PromptInstallTheme(const std::vector<std::string>& nxtheme_paths) {
    if (HasNro()) {
        App::Push<OptionBox>(
            "Theme downloaded, install now?"_i18n,
            "Back"_i18n, "Install"_i18n, 1, [nxtheme_paths](auto op_index){
                if (op_index && *op_index) {
                    std::string args;

                    for (const auto& paths : nxtheme_paths) {
                        if (!args.empty()) {
                            args += ' ';
                        }

                        args += nro_add_arg_file(paths);
                    }

                    log_write("themezer nro: %s\n", GetNroPath());
                    log_write("themezer args: %s\n", args.c_str());

                    const auto rc = nro_launch(GetNroPath(), args);
                    App::PushErrorBox(rc, "Failed to launch NXthemes_Installer.nro"_i18n);
                }
            }
        );
    } else {
        App::Push<OptionBox>(
            "NXthemes_Installer.nro not found, download now?"_i18n,
            "Back"_i18n, "Download"_i18n, 1, [](auto op_index){
                if (op_index && *op_index) {
                    const gh::AssetEntry asset{
                        .name = "NXThemesInstaller.nro",
                        .path = "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
                    };

                    gh::Download(NRO_URL, asset);
                }
            }
        );
    }
}

auto InstallTheme(sphaira::ui::ProgressBox* pbox, const PackListEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    std::vector<std::string> nxtheme_paths;
    for (const auto& theme : entry.themes) {
        if (pbox->ShouldExit()) {
            break;
        }
        if (theme.download_url.empty()) {
            continue;
        }

        const auto theme_label = theme.details.name.empty() ? entry.details.name : theme.details.name;
        pbox->NewTransfer("Downloading "_i18n + theme_label);

        const auto out_path = BuildThemePath(entry, theme);
        log_write("starting themezer download: %s -> %s\n", theme.download_url.c_str(), out_path.s);

        const auto result = curl::Api().ToFile(
            curl::Url{theme.download_url},
            curl::Path{out_path},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        R_UNLESS(result.success, Result_ThemezerFailedToDownloadTheme);
        nxtheme_paths.emplace_back(out_path);
    }

    // ensure that we actually downloaded the theme.
    R_UNLESS(!nxtheme_paths.empty(), Result_ThemezerFailedToDownloadTheme);

    PromptInstallTheme(nxtheme_paths);

    log_write("finished install :)\n");
    R_SUCCEED();
}

auto InstallThemePackage(sphaira::ui::ProgressBox* pbox, const std::string& name, const std::string& url) -> Result {
    const auto zip_path = paths::DOWNLOADS + "/theme.zip";
    pbox->NewTransfer("Downloading "_i18n + name);

    const auto result = curl::Api().ToFile(
        curl::Url{url},
        curl::Path{zip_path},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, Result_ThemezerFailedToDownloadTheme);

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    R_TRY(fs.CreateDirectoryRecursively("/themes/"));

    pbox->NewTransfer("Extracting " + name);

    std::vector<std::string> nxtheme_paths;
    auto filter = [&](const fs::FsPath& /*entry_name*/, fs::FsPath& path) -> bool {
        std::string_view sv{path.s};
        const std::string_view suffix{".nxtheme"};
        if (sv.size() >= suffix.size()) {
            auto str_end = sv.substr(sv.size() - suffix.size());
            if (std::equal(str_end.begin(), str_end.end(), suffix.begin(), [](char a, char b){
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
                nxtheme_paths.emplace_back(path.s);
            }
        }
        return true;
    };

    R_TRY(thread::TransferUnzipAll(pbox, zip_path, &fs, "/themes/", filter));
    fs.DeleteFile(zip_path);

    PromptInstallTheme(nxtheme_paths);

    log_write("finished theme package install :)\n");
    R_SUCCEED();
}


} // namespace sphaira::ui::menu::themezer
