#include "ui/menus/console_files_transfer.hpp"

#include "ui/menus/filebrowser.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/popup_multi_select.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include "app.hpp"
#include "app_paths.hpp"
#include "archive_extract.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "files_transfer_plan.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"
#include "net.hpp"
#include "web.hpp"

#include <algorithm>
#include <ctime>
#include <memory>
#include <minizip/zip.h>
#include <yyjson.h>

namespace sphaira::ui::menu::console_files {
namespace {

namespace ft = ::sphaira::files_transfer;

auto SendDir() -> fs::FsPath { return fs::FsPath{paths::DATA_ROOT + "/console_send"}; }
auto RecvDir() -> fs::FsPath { return fs::FsPath{paths::DATA_ROOT + "/console_recv"}; }

auto FileSize(fs::Fs& sd, const fs::FsPath& path) -> s64 {
    fs::File file;
    s64 size{};
    if (R_SUCCEEDED(sd.OpenFile(path, FsOpenMode_Read, &file))) {
        file.GetSize(&size);
    }
    return size;
}

auto FormatSize(s64 bytes) -> std::string {
    char buf[32];
    if (bytes >= 1024LL * 1024 * 1024) {
        std::snprintf(buf, sizeof(buf), "%.2f GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buf;
}

// ---- send ----

struct ZipEntry {
    std::string rel{}; // inside the zip; a folder ends with '/'
    s64 size{};
    bool dir{};
};

// every file (and empty folder) under `root`, relative to it.
void Collect(fs::Fs& sd, const std::string& root, std::vector<ZipEntry>& out, s64& total) {
    std::vector<std::string> stack{""};
    while (!stack.empty()) {
        const auto rel = std::move(stack.back());
        stack.pop_back();

        fs::Dir dir;
        std::vector<FsDirectoryEntry> entries;
        if (R_FAILED(sd.OpenDirectory(rel.empty() ? root : root + "/" + rel, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
            continue;
        }
        dir.ReadAll(entries);

        if (entries.empty() && !rel.empty()) {
            out.push_back({rel + "/", 0, true});
        }
        for (const auto& e : entries) {
            auto child = rel.empty() ? std::string{e.name} : rel + "/" + e.name;
            if (e.type == FsDirEntryType_Dir) {
                stack.push_back(std::move(child));
            } else {
                out.push_back({std::move(child), static_cast<s64>(e.file_size), false});
                total += e.file_size;
            }
        }
    }
}

// the quickest deflate level: the link, not the packing, is meant to be the wait.
Result ZipFolder(ProgressBox* pbox, fs::Fs& sd, const std::string& root, const fs::FsPath& zip_path,
    const std::vector<ZipEntry>& entries, s64& done, s64 grand_total) {
    const auto t = std::time(nullptr);
    const auto tm = std::localtime(&t);
    zip_fileinfo info{};
    if (tm) {
        info.tmz_date.tm_sec = tm->tm_sec;
        info.tmz_date.tm_min = tm->tm_min;
        info.tmz_date.tm_hour = tm->tm_hour;
        info.tmz_date.tm_mday = tm->tm_mday;
        info.tmz_date.tm_mon = tm->tm_mon;
        info.tmz_date.tm_year = tm->tm_year;
    }

    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);
    auto zf = zipOpen2_64(zip_path.s, APPEND_STATUS_CREATE, nullptr, &file_func);
    R_UNLESS(zf, Result_ZipOpen2_64);
    ON_SCOPE_EXIT(zipClose(zf, nullptr));

    std::vector<u8> buf(256 * 1024);
    for (const auto& e : entries) {
        R_UNLESS(!pbox->ShouldExit(), pbox->ShouldExitResult());

        if (e.dir) {
            R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zf, e.rel.c_str(), &info, nullptr, 0, nullptr, 0, nullptr, 0, 0), Result_ZipOpenNewFileInZip);
            zipCloseFileInZip(zf);
            continue;
        }

        pbox->NewTransfer(e.rel);
        fs::File file;
        R_TRY(sd.OpenFile(fs::FsPath{root + "/" + e.rel}, FsOpenMode_Read, &file));

        R_UNLESS(ZIP_OK == zipOpenNewFileInZip64(zf, e.rel.c_str(), &info, nullptr, 0, nullptr, 0, nullptr,
            Z_DEFLATED, Z_BEST_SPEED, e.size >= 0xFFFFFFFFLL ? 1 : 0), Result_ZipOpenNewFileInZip);
        ON_SCOPE_EXIT(zipCloseFileInZip(zf));

        for (s64 off = 0;;) {
            R_UNLESS(!pbox->ShouldExit(), pbox->ShouldExitResult());
            u64 got{};
            R_TRY(file.Read(off, buf.data(), buf.size(), 0, &got));
            if (!got) {
                break;
            }
            R_UNLESS(ZIP_OK == zipWriteInFileInZip(zf, buf.data(), static_cast<unsigned>(got)), Result_ZipOpenNewFileInZip);
            off += got;
            done += got;
            pbox->UpdateTransfer(done, grand_total);
        }
    }
    R_SUCCEED();
}

// the package folder: one payload per source and the manifest that tells the other console what is what.
Result BuildPackage(ProgressBox* pbox, const std::vector<std::string>& sources) {
    fs::FsNativeSd sd;
    const auto dir = SendDir();
    if (sd.DirExists(dir)) {
        R_TRY(sd.DeleteDirectoryRecursively(dir));
    }
    R_TRY(sd.CreateDirectoryRecursively(dir));

    struct Job {
        std::string src{};
        bool dir{};
        std::vector<ZipEntry> entries{};
    };
    std::vector<Job> jobs;
    s64 total{};
    for (const auto& src : sources) {
        Job job{src, sd.DirExists(fs::FsPath{src}), {}};
        if (job.dir) {
            Collect(sd, src, job.entries, total);
        } else {
            total += FileSize(sd, fs::FsPath{src});
        }
        jobs.push_back(std::move(job));
    }

    std::vector<ft::Item> items;
    s64 done{};
    for (size_t i = 0; i < jobs.size(); i++) {
        const auto& job = jobs[i];
        const auto [parent, name] = ft::SplitPath(job.src);
        pbox->SetTitle(name);

        ft::Item item;
        item.dir = job.dir;
        item.payload = ft::PayloadName(i, job.dir);
        item.parent = parent;
        item.name = name;

        const auto out = fs::AppendPath(dir, item.payload);
        if (job.dir) {
            R_TRY(ZipFolder(pbox, sd, job.src, out, job.entries, done, total));
        } else {
            pbox->NewTransfer(name);
            R_TRY(sd.copy_entire_file(out, fs::FsPath{job.src}));
            done += FileSize(sd, out);
            pbox->UpdateTransfer(done, total);
        }
        item.size = FileSize(sd, out);
        items.push_back(std::move(item));
    }

    const auto text = ft::FormatManifest(items);
    R_TRY(sd.write_entire_file(fs::AppendPath(dir, ft::MANIFEST_NAME), std::vector<u8>(text.begin(), text.end())));
    R_SUCCEED();
}

void StartServer() {
    App::SetMountedFolders({SendDir()});

    WebShareResult result;
    const auto rc = WebStartServer("", result);
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to start folder server"_i18n);
        return;
    }

    if (WebGetProgressBox()) {
        nvgDeleteImage(App::GetVg(), result.qr_image);
        App::Notify("Sharing files"_i18n);
        return;
    }

    WebPushServerProgressBox(result.ip_url, result.qr_image, "Send files"_i18n,
        "The other console opens Console Transfer → Receive files and enters this address."_i18n);
}

// ---- receive ----

struct Remote {
    std::string root{};
    std::vector<ft::Item> items{};
};

auto DownloadUrl(const std::string& base_url, const std::string& root, const std::string& name) -> std::string {
    return base_url + "/download?path=" + curl::EscapeString(root + "/" + name);
}

Result FetchRemote(ProgressBox* pbox, const std::string& base_url, Remote& out) {
    curl::Api list_api;
    list_api.SetOption(curl::Url{base_url + "/list"});
    list_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
    const auto list = curl::ToMemory(list_api);
    R_UNLESS(!pbox->ShouldExit(), Result_TransferCancelled);
    R_UNLESS(list.success && list.code == 200 && !list.data.empty(), Result_FsInvalidType);

    yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(list.data.data()), list.data.size(), 0);
    R_UNLESS(doc, Result_FsInvalidType);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const char* root = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "path"));
    R_UNLESS(root && *root, Result_FsInvalidType);
    out.root = root;
    while (out.root.size() > 1 && out.root.back() == '/') {
        out.root.pop_back();
    }

    curl::Api man_api;
    man_api.SetOption(curl::Url{DownloadUrl(base_url, out.root, ft::MANIFEST_NAME)});
    man_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
    const auto man = curl::ToMemory(man_api);
    R_UNLESS(!pbox->ShouldExit(), Result_TransferCancelled);
    R_UNLESS(man.success && man.code == 200 && !man.data.empty(), Result_FsInvalidType);

    out.items = ft::ParseManifest(std::string_view{reinterpret_cast<const char*>(man.data.data()), man.data.size()});
    R_UNLESS(!out.items.empty(), Result_FsEmpty);
    R_SUCCEED();
}

struct ReceiveChoice {
    std::string base_url{};
    Remote remote{};
    std::vector<u8> ticks{};
    std::string folder{}; // empty = back where it was
    bool unpack{true};
};

Result ReceiveItems(ProgressBox* pbox, const ReceiveChoice& choice, const std::vector<ft::Item>& items) {
    fs::FsNativeSd sd;
    const auto tmp_dir = RecvDir();
    if (sd.DirExists(tmp_dir)) {
        sd.DeleteDirectoryRecursively(tmp_dir);
    }
    R_TRY(sd.CreateDirectoryRecursively(tmp_dir));
    ON_SCOPE_EXIT(sd.DeleteDirectoryRecursively(tmp_dir));

    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        pbox->SetTitle(item.name);
        pbox->NewTransfer(item.name);

        const auto tmp = fs::AppendPath(tmp_dir, std::to_string(i) + ".part");
        const auto res = curl::Api().ToFile(
            curl::Url{DownloadUrl(choice.base_url, choice.remote.root, item.payload)},
            curl::Path{tmp},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );
        R_UNLESS(!pbox->ShouldExit(), Result_TransferCancelled);
        R_UNLESS(res.success, Result_CurlFailedEasyInit);

        const auto folder = ft::TargetFolder(item, choice.folder);
        sd.CreateDirectoryRecursively(fs::FsPath{folder});
        const fs::FsPath target{ft::TargetPath(item, choice.folder, choice.unpack)};

        if (item.dir && choice.unpack) {
            R_TRY(sd.CreateDirectoryRecursively(target));
            pbox->NewTransfer("Unpacking"_i18n + " " + item.name);
            R_TRY(archive::Extract(pbox, &sd, tmp, target));
            sd.DeleteFile(tmp);
        } else {
            sd.DeleteFile(target);
            R_TRY(sd.RenameFile(tmp, target));
        }
    }
    R_SUCCEED();
}

void StartReceive(std::shared_ptr<ReceiveChoice> choice) {
    std::vector<ft::Item> items;
    for (size_t i = 0; i < choice->remote.items.size(); i++) {
        if (choice->ticks[i]) {
            items.push_back(choice->remote.items[i]);
        }
    }
    if (items.empty()) {
        App::Notify("Nothing chosen"_i18n);
        return;
    }

    const auto where = choice->folder.empty() ? "the place they came from"_i18n : choice->folder;
    App::Push<OptionBox>(
        "Receive"_i18n + " " + std::to_string(items.size()) + " " + "items"_i18n + "\n" + where + "\n\n"
            + "Items with the same name are replaced."_i18n,
        "Cancel"_i18n, "Receive"_i18n, 1,
        [choice, items](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }
            App::PopToMenu();
            App::Push<ProgressBox>(0, "Receiving"_i18n, items.front().name, [choice, items](ProgressBox* pbox) -> Result {
                return ReceiveItems(pbox, *choice, items);
            }, [count = items.size()](Result rc) {
                if (rc == Result_TransferCancelled) {
                    return;
                }
                App::PushErrorBox(rc, "Receive failed!"_i18n);
                if (R_SUCCEEDED(rc)) {
                    App::Notify("Received"_i18n + " " + std::to_string(count) + " " + "items"_i18n);
                }
            });
        });
}

void ShowReceivePicker(std::shared_ptr<ReceiveChoice> choice) {
    const auto total = choice->remote.items.size();
    choice->ticks.assign(total, 1);

    auto options = std::make_unique<Sidebar>("Receive files"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const auto count_label = [choice, total]() {
        const auto n = std::count(choice->ticks.begin(), choice->ticks.end(), 1);
        return std::to_string(n) + " / " + std::to_string(total);
    };

    const auto pick = options->Add<SidebarEntryTextBase>("Choose files"_i18n, count_label(), SidebarEntryTextBase::Callback{},
        "All offered files and folders are ticked. Untick what you do not want."_i18n);
    pick->SetCallback([choice, pick, count_label](){
        PopupMultiSelect::Items items;
        for (const auto& i : choice->remote.items) {
            items.emplace_back(PopupMultiSelect::Item{
                (i.parent == "/" ? "" : i.parent) + "/" + i.name,
                FormatSize(i.size) + " · " + (i.dir ? "folder"_i18n : "file"_i18n), false});
        }
        App::Push<PopupMultiSelect>("Choose files"_i18n, items, choice->ticks, [pick, count_label](){
            pick->SetValue(count_label());
        });
    });

    const auto place = options->Add<SidebarEntryTextBase>("Save to"_i18n, "Same place as on the other console"_i18n, SidebarEntryTextBase::Callback{},
        "Each item goes back to the folder it was in on the other console, or all go into one folder you pick."_i18n);
    place->SetCallback([choice, place](){
        PopupList::Items items{"Same place as on the other console"_i18n, "Choose folder..."_i18n};
        App::Push<PopupList>("Save to"_i18n, items, [choice, place](auto op_index){
            if (!op_index) {
                return;
            }
            if (*op_index == 0) {
                choice->folder.clear();
                place->SetValue("Same place as on the other console"_i18n);
                return;
            }
            auto browser = std::make_unique<filebrowser::Menu>(MenuFlag_None);
            browser->SetFolderPicker([choice, place](const fs::FsPath& folder){
                choice->folder = folder.toString();
                place->SetValue(choice->folder);
            }, "Select folder to save to"_i18n, "Save the received files to this folder?"_i18n);
            App::Push(std::move(browser));
        });
    });

    options->Add<SidebarEntryBool>("Folders"_i18n, true, [choice](bool& value){
        choice->unpack = value;
    }, "Unpack the received folders, or keep each one as a .zip file."_i18n, "Unpack"_i18n, "Keep packed"_i18n);

    options->Add<SidebarEntryCallback>("Receive"_i18n, [choice](){
        StartReceive(choice);
    }, true, "Download the ticked files and folders."_i18n);
}

} // namespace

void Send(std::vector<std::string> paths) {
    std::erase_if(paths, [](const std::string& p) {
        return !ft::IsSafeName(ft::SplitPath(p).second) || !ft::IsSafeFolder(ft::SplitPath(p).first);
    });
    if (paths.empty()) {
        App::Notify("Nothing to send"_i18n);
        return;
    }

    net::RequireConnection([paths = std::move(paths)]() {
        App::Push<ProgressBox>(0, "Preparing files..."_i18n, "", [paths](ProgressBox* pbox) -> Result {
            return BuildPackage(pbox, paths);
        }, [](Result rc) {
            if (rc == Result_TransferCancelled) {
                return;
            }
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to prepare files"_i18n);
                return;
            }
            StartServer();
        });
    });
}

void PickFolderAndSend() {
    auto browser = std::make_unique<filebrowser::Menu>(MenuFlag_None);
    browser->SetFolderPicker([](const fs::FsPath& folder){
        Send({folder.toString()});
    }, "Select folder to send"_i18n, "Pack this folder and send it to another console?"_i18n);
    App::Push(std::move(browser));
}

void Receive() {
    ConnectConsoleTransfer([](const std::string& base_url) {
        auto choice = std::make_shared<ReceiveChoice>();
        choice->base_url = base_url;
        App::Push<ProgressBox>(0, "Fetching file list..."_i18n, base_url, [choice](ProgressBox* pbox) -> Result {
            return FetchRemote(pbox, choice->base_url, choice->remote);
        }, [choice](Result rc) {
            if (rc == Result_TransferCancelled) {
                return;
            }
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("The other console offers no files.\n\nOn it, open Console Transfer and choose Send files first."_i18n, "OK"_i18n);
                return;
            }
            ShowReceivePicker(choice);
        });
    });
}

} // namespace sphaira::ui::menu::console_files
