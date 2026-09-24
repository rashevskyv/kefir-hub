#include "ui/menus/ghdl_internal.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/filebrowser.hpp"

#include "ui/sidebar.hpp"
#include "ui/remote_input.hpp"
#include "nro.hpp"
#include "swkbd.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/zip_extract_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"

#include "log.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "image.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include "threaded_file_transfer.hpp"
#include "path_util.hpp"
#include "zip_extract_plan.hpp"
#include "minizip_helper.hpp"

#include <minizip/unzip.h>
#include <minIni.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>
#include <optional>
#include <functional>

namespace sphaira::ui::menu::gh {


auto GenerateApiUrl(const Entry& e) -> std::string {
    if (e.tag.empty()) {
        return "https://api.github.com/repos/" + e.owner + "/" + e.repo + "/releases";
    } else if (e.tag == "latest") {
        return "https://api.github.com/repos/" + e.owner + "/" + e.repo + "/releases/latest";
    } else {
        return "https://api.github.com/repos/" + e.owner + "/" + e.repo + "/releases/tags/" + e.tag;
    }
}

auto apiBuildAssetCache(const std::string& url) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%u.json", CACHE_PATH, crc32Calculate(url.data(), url.size()));
    return path;
}

void from_json(yyjson_val* json, AssetEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(name);
        JSON_SET_STR(path);
        JSON_SET_STR(pre_install_message);
        JSON_SET_STR(post_install_message);
    );
}

void from_json(const fs::FsPath& path, Entry& e) {
    JSON_INIT_VEC_FILE(path, nullptr, nullptr);
    JSON_OBJ_ITR(
        JSON_SET_STR(url);
        JSON_SET_STR(owner);
        JSON_SET_STR(repo);
        JSON_SET_STR(tag);
        JSON_SET_STR(pre_install_message);
        JSON_SET_STR(post_install_message);
        JSON_SET_ARR_OBJ(assets);
        JSON_SET_STR(direct_url);
    );
}

void from_json(yyjson_val* json, GhApiAsset& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(name);
        JSON_SET_STR(content_type);
        JSON_SET_UINT(size);
        JSON_SET_UINT(download_count);
        JSON_SET_STR(updated_at);
        JSON_SET_STR(browser_download_url);
    );
}

void from_json(yyjson_val* json, GhApiEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(tag_name);
        JSON_SET_STR(name);
        JSON_SET_STR(published_at);
        JSON_SET_BOOL(prerelease);
        JSON_SET_ARR_OBJ(assets);
    );
}

void from_json(const fs::FsPath& path, std::vector<GhApiEntry>& e) {
    JSON_INIT_VEC_FILE(path, nullptr, nullptr);
    if (yyjson_is_arr(json)) {
        JSON_ARR_ITR(e);
    } else {
        e.resize(1);
        from_json(json, e[0]);
    }
}

auto DownloadApp(ProgressBox* pbox, const GhApiAsset& gh_asset, const AssetEntry* entry) -> Result {
    static const fs::FsPath temp_file{"/switch/sphaira/cache/github/ghdl.temp"};

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    // Clean stale temp file before starting and ensure cleanup on exit
    fs.DeleteFile(temp_file);
    ON_SCOPE_EXIT(fs.DeleteFile(temp_file));

    R_UNLESS(!gh_asset.browser_download_url.empty(), Result_GhdlEmptyAsset);

    // Gate 1: Check cancellation before starting network transfer
    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }

    // 2. download the asset
    pbox->NewTransfer("Downloading "_i18n + gh_asset.name);
    log_write("starting download: %s\n", gh_asset.browser_download_url.c_str());

    const auto result = curl::Api().ToFile(
        curl::Url{gh_asset.browser_download_url},
        curl::Path{temp_file},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );

    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }
    R_UNLESS(result.success, Result_GhdlFailedToDownloadAsset);

    // Gate 2: Check cancellation after download before touching destination files
    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }

    const bool is_zip = path::IsZipAsset(gh_asset.content_type, gh_asset.name, gh_asset.browser_download_url);

    // 3. extract the zip / install non-zip file
    if (is_zip) {
        log_write("found zip\n");
        pbox->NewTransfer("Extracting..."_i18n);
        fs::FsPath root_path{"/"};
        if (entry && !entry->path.empty()) {
            if (auto norm = path::NormalizeAbsoluteSdPath(entry->path)) {
                root_path = *norm;
            }
        }
        R_TRY(thread::TransferUnzipAll(pbox, temp_file, &fs, root_path));
    } else {
        std::string_view basename = gh_asset.name;
        if (!path::IsSafeFilename(basename)) {
            basename = path::ExtractBasename(gh_asset.browser_download_url);
            R_UNLESS(path::IsSafeFilename(basename), Result_GhdlEmptyAsset);
        }

        fs::FsPath target_path;
        if (entry && !entry->path.empty()) {
            if (auto norm = path::NormalizeAbsoluteSdPath(entry->path)) {
                if (entry->path.back() == '/' || *norm == "/") {
                    target_path = fs::AppendPath(*norm, std::string(basename));
                } else {
                    target_path = *norm;
                }
            } else {
                target_path = fs::AppendPath("/switch", std::string(basename));
            }
        } else {
            target_path = fs::AppendPath("/switch", std::string(basename));
        }

        log_write("installing non-zip asset to: %s\n", target_path.s);
        fs.CreateDirectoryRecursivelyWithPath(target_path);
        fs.DeleteFile(target_path);
        R_TRY(fs.RenameFile(temp_file, target_path));
    }

    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }

    log_write("success\n");
    R_SUCCEED();
}

auto DownloadReleaseJsonJson(ProgressBox* pbox, const std::string& url, std::vector<GhApiEntry>& out) -> Result {
    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }

    pbox->NewTransfer("Downloading json"_i18n);
    log_write("starting download\n");

    const auto path = apiBuildAssetCache(url);

    const auto result = curl::Api().ToFile(
        curl::Url{url},
        curl::Path{path},
        curl::OnProgress{pbox->OnDownloadProgressCallback()},
        curl::Flags{curl::Flag_Cache},
        curl::Header{
            { "Accept", "application/vnd.github+json" },
        }
    );

    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }
    R_UNLESS(result.success, Result_GhdlFailedToDownloadAssetJson);
    from_json(result.path, out);

    R_UNLESS(!out.empty(), Result_GhdlEmptyAsset);
    R_SUCCEED();
}

constexpr s64 MAX_DIRECT_LINK_SIZE = 20 * 1024 * 1024; // 20MB soft limit

auto UrlFilename(const std::string& url, bool is_nro) -> std::string {
    auto name = std::string{path::ExtractBasename(url)};
    if (!path::IsSafeFilename(name)) {
        return is_nro ? "downloaded.nro" : "downloaded.zip";
    }
    return name;
}

auto ListZipEntryNames(const fs::FsPath& zip_path) -> std::vector<std::string> {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    auto zfile = unzOpen2_64(zip_path, &file_func);
    if (!zfile) {
        return {};
    }
    ON_SCOPE_EXIT(unzClose(zfile));

    std::vector<std::string> names;
    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        return names;
    }
    do {
        char name_buf[1024]{};
        unz_file_info64 info{};
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name_buf, sizeof(name_buf), nullptr, 0, nullptr, 0)) {
            continue;
        }
        names.emplace_back(name_buf);
    } while (UNZ_OK == unzGoToNextFile(zfile));
    return names;
}

void OpenSdBrowser(const fs::FsPath& path) {
    const filebrowser::FsEntry sd{"microSD card", "/", filebrowser::FsType::Sd};
    App::Push<filebrowser::Menu>(MenuFlag_None, sd, path);
}

void AskOpenExtractedFolder(const fs::FsPath& path) {
    const char* shown = (path.s[0] != '\0') ? path.s : "/";
    App::Push<OptionBox>(
        "Extracted to: "_i18n + shown,
        "No"_i18n, "Open in file browser"_i18n, 1, [path](auto op_index){
            if (op_index && *op_index) {
                OpenSdBrowser(path);
            }
        }
    );
}

void ExtractDownloadedZip(fs::FsPath zip_path, fs::FsPath extract_path, std::string nro_zip_name = {}, std::vector<std::string> include_files = {});
void BrowseExtractFolder(fs::FsPath zip_path, std::vector<std::string> include_files = {}, std::function<void()> on_picked = {});
void PromptExtractPath(fs::FsPath zip_path, std::string filename);

void ExtractDownloadedZip(fs::FsPath zip_path, fs::FsPath extract_path, std::string nro_zip_name, std::vector<std::string> include_files) {
    const bool nro_only = !nro_zip_name.empty();
    const bool filtered = !nro_only && !include_files.empty();
    fs::FsPath open_dir = extract_path;
    if (nro_only) {
        extract_path = zip_extract::NroInstallDest(nro_zip_name).c_str();
        const auto slash = std::string_view{extract_path.s}.find_last_of('/');
        open_dir = (slash != std::string_view::npos && slash > 0)
            ? fs::FsPath{std::string{extract_path.s, extract_path.s + slash}}
            : fs::FsPath{zip_extract::kSwitchDir.data()};
    } else if (auto norm = path::NormalizeAbsoluteSdPath(extract_path.s)) {
        extract_path = norm->c_str();
        open_dir = extract_path;
    } else {
        extract_path = zip_extract::kDownloadsDir.data();
        open_dir = extract_path;
    }

    App::Push<ProgressBox>(0, "Extracting..."_i18n, extract_path.s, [zip_path, extract_path, nro_zip_name, nro_only, include_files, filtered](auto pbox) -> Result {
        fs::FsNativeSd fs;
        R_TRY(fs.GetFsOpenResult());
        if (nro_only) {
            const auto slash = std::string_view{extract_path.s}.find_last_of('/');
            if (slash != std::string_view::npos && slash > 0) {
                fs.CreateDirectoryRecursively(std::string{extract_path.s, extract_path.s + slash});
            }
            pbox->NewTransfer("Extracting..."_i18n);
            R_TRY(thread::TransferUnzipAll(pbox, zip_path, &fs, "/",
                [nro_zip_name, extract_path](const fs::FsPath& name, fs::FsPath& out) {
                    const auto n = zip_extract::NormalizeZipEntry(name.s);
                    const auto want = zip_extract::NormalizeZipEntry(nro_zip_name);
                    if (!path::EqualsIC(n, want)) {
                        return false;
                    }
                    out = extract_path;
                    return true;
                }));
        } else {
            if (std::strcmp(extract_path.s, "/") != 0) {
                fs.CreateDirectoryRecursively(extract_path);
            }
            pbox->NewTransfer("Extracting..."_i18n);
            if (filtered) {
                R_TRY(thread::TransferUnzipAll(pbox, zip_path, &fs, extract_path,
                    [include_files](const fs::FsPath& name, fs::FsPath&) {
                        return zip_extract::EntryMatchesSelection(name.s, include_files);
                    }));
            } else {
                R_TRY(thread::TransferUnzipAll(pbox, zip_path, &fs, extract_path));
            }
        }
        R_SUCCEED();
    }, [zip_path, open_dir, nro_only, extract_path](Result rc){
        if (rc == Result_TransferCancelled) {
            App::Push<OptionBox>("Download was cancelled."_i18n, "OK"_i18n);
            return;
        }
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Extract failed!"_i18n);
            return;
        }

        homebrew::SignalChange();
        App::Push<OptionBox>(
            "Download and extract completed!\nDelete ZIP file?"_i18n,
            "Keep"_i18n, "Delete"_i18n, 1, [zip_path, open_dir, nro_only, extract_path](auto op_index){
                if (op_index && *op_index) {
                    fs::FsNativeSd fs;
                    fs.DeleteFile(zip_path);
                }
                if (nro_only) {
                    App::Push<OptionBox>(
                        "Launch now?"_i18n,
                        "No"_i18n, "Launch"_i18n, 1, [extract_path](auto launch_index){
                            if (launch_index && *launch_index) {
                                nro_launch(extract_path);
                            }
                        }
                    );
                    return;
                }
                AskOpenExtractedFolder(open_dir);
            }
        );
    });
}

void BrowseExtractFolder(fs::FsPath zip_path, std::vector<std::string> include_files, std::function<void()> on_picked) {
    auto browser = std::make_unique<filebrowser::Menu>(MenuFlag_None);
    browser->SetFolderPicker(
        [zip_path, include_files, on_picked](const fs::FsPath& folder) {
            if (on_picked) {
                on_picked();
            }
            ExtractDownloadedZip(zip_path, folder, {}, include_files);
        },
        "Select folder"_i18n,
        "Extract ZIP to this folder?"_i18n,
        zip_extract::SafeFolderName(zip_extract::FileStem(zip_path.s)));
    App::Push(std::move(browser));
}

void PromptExtractPath(fs::FsPath zip_path, std::string filename) {
    const auto names = ListZipEntryNames(zip_path);
    App::Push<ZipExtractBox>(
        filename.empty() ? "Extract Options"_i18n : filename,
        names,
        [zip_path](fs::FsPath dest, std::string nro_only, std::vector<std::string> files) {
            ExtractDownloadedZip(zip_path, dest, std::move(nro_only), std::move(files));
        },
        [zip_path](std::vector<std::string> files, std::function<void()> on_picked) {
            BrowseExtractFolder(zip_path, std::move(files), std::move(on_picked));
        });
}

void OpenDirectLinkPrompt(std::string filled);

void OfferFixDirectUrl(std::string url, const std::string& message) {
    App::Push<OptionBox>(
        message,
        "OK"_i18n, "Edit URL"_i18n, 1, [url = std::move(url)](auto op_index){
            if (op_index && *op_index) {
                OpenDirectLinkPrompt(url);
            }
        }
    );
}

void DoDirectLinkDownload(const std::string& url_in) {
    std::string url = path::CollapseRepeatedHttpSchemes(url_in);
    const bool is_nro = path::IsValidDirectNroUrl(url);
    const auto filename = UrlFilename(url, is_nro);
    const fs::FsPath dest_file = is_nro
        ? fs::FsPath{zip_extract::SuggestNakedNroPath(filename)}
        : fs::FsPath{std::string{zip_extract::kDownloadsDir} + "/" + filename};

    App::Push<ProgressBox>(0, "Downloading..."_i18n, filename, [url, is_nro, dest_file, filename](auto pbox) -> Result {
        fs::FsNativeSd fs;
        R_TRY(fs.GetFsOpenResult());

        if (is_nro) {
            const auto slash = std::string_view{dest_file.s}.find_last_of('/');
            if (slash != std::string_view::npos && slash > 0) {
                fs.CreateDirectoryRecursively(std::string{dest_file.s, dest_file.s + slash});
            }
        } else {
            fs.CreateDirectoryRecursively(std::string{zip_extract::kDownloadsDir});
            fs.DeleteFile(dest_file);
        }

        if (pbox->ShouldExit()) {
            return Result_TransferCancelled;
        }

        pbox->NewTransfer("Downloading "_i18n + filename);
        const auto result = curl::Api().ToFile(
            curl::Url{url},
            curl::Path{dest_file},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        if (pbox->ShouldExit()) {
            return Result_TransferCancelled;
        }
        R_UNLESS(result.success, Result_GhdlFailedToDownloadAsset);
        R_SUCCEED();
    }, [is_nro, dest_file, filename, url](Result rc){
        if (rc == Result_TransferCancelled) {
            App::Push<OptionBox>("Download was cancelled."_i18n, "OK"_i18n);
            return;
        }
        if (R_FAILED(rc)) {
            OfferFixDirectUrl(url, "Couldn't download that file.\nThe address may be wrong, or the server didn't respond. Edit the URL and try again."_i18n);
            return;
        }

        homebrew::SignalChange();

        if (is_nro) {
            App::Notify("Downloaded "_i18n + filename);
            App::Push<OptionBox>(
                "Downloaded "_i18n + filename + " to " + dest_file.s + "\n" + "Launch now?"_i18n,
                "No"_i18n, "Launch"_i18n, 1, [dest_file](auto op_index){
                    if (op_index && *op_index) {
                        nro_launch(dest_file);
                    }
                }
            );
            return;
        }

        PromptExtractPath(dest_file, filename);
    });
}

void ProcessDirectLinkUrl(std::string url) {
    url = path::CollapseRepeatedHttpSchemes(url);
    if (url.empty()) {
        return;
    }

    if (!path::IsValidDirectDownloadUrl(url)) {
        OfferFixDirectUrl(url, "This isn't a direct link to a .zip or .nro file.\nCheck the address (it should start with http and end with .zip or .nro) and try again."_i18n);
        return;
    }

    // Check file size via HEAD request
    const auto head_result = curl::Api().ToMemory(
        curl::Url{url},
        curl::Flags{curl::Flag_NoBody}
    );

    if (head_result.success) {
        auto it = head_result.header.Find("content-length");
        if (it != head_result.header.m_map.end()) {
            s64 size = std::atoll(it->second.c_str());
            if (size > MAX_DIRECT_LINK_SIZE) {
                // File is larger than 20MB - warn user
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                    "File is %.1f MB (limit: 20 MB)\nLarge files may cause issues.\nForce download?",
                    (double)size / (1024.0 * 1024.0));

                App::Push<OptionBox>(msg, "Cancel"_i18n, "Force"_i18n, 0, [url](auto op_index){
                    if (op_index && *op_index) {
                        DoDirectLinkDownload(url);
                    }
                });
                return;
            }
        }
    }

    // Size OK or unknown - proceed with download
    DoDirectLinkDownload(url);
}

void OpenDirectLinkPrompt(std::string filled) {
    ui::remote_input::Options opts{
        .title = "Direct Download"_i18n,
        .guide = "Enter direct link to a .zip archive or .nro file"_i18n,
        .default_text = filled.empty() ? "https://" : std::move(filled),
        .placeholder = "https://example.com/app.nro or app.zip",
        .multiline = false,
    };

    ui::remote_input::PromptTextInput(opts, [](const std::string& url){
        ProcessDirectLinkUrl(url);
    });
}

} // namespace sphaira::ui::menu::gh
