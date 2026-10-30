#include "rom_download.hpp"
#include "rom_library.hpp"
#include "download_jobs.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/remote_input.hpp"
#include "ui/sidebar.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include <yyjson.h>
#include <cstdlib>
#include <memory>

namespace sphaira::rom {
namespace {

using ui::menu::settings::SettingsItem;
using ui::menu::tools::SystemToolsMenu;

auto Load() -> std::vector<Collection> {
    std::vector<u8> data;
    fs::FsNativeSd().read_entire_file(COLLECTIONS_PATH, data);
    return Parse(std::string_view{reinterpret_cast<const char*>(data.data()), data.size()});
}

void Save(const std::vector<Collection>& list) {
    const auto text = Format(list);
    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursivelyWithPath(COLLECTIONS_PATH);
    fs.write_entire_file(COLLECTIONS_PATH, std::vector<u8>(text.begin(), text.end()));
}

struct Item {
    std::string title;
    std::vector<RemoteFile> files;
};

// GET archive.org/metadata/<id>: the item's title and its content files
auto FetchItem(ui::ProgressBox* pbox, const std::string& id, Item& out) -> Result {
    pbox->NewTransfer("Reading the item on archive.org"_i18n);
    const auto result = curl::Api().ToMemory(
        curl::Url{MetadataUrl(id)},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_TRY(pbox->ShouldExitResult());
    R_UNLESS(result.success && !result.data.empty(), Result_GhdlFailedToDownloadAssetJson);
    auto doc = yyjson_read(reinterpret_cast<const char*>(result.data.data()), result.data.size(), 0);
    R_UNLESS(doc, Result_GhdlFailedToDownloadAssetJson);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const auto root = yyjson_doc_get_root(doc);

    if (const auto meta = yyjson_obj_get(root, "metadata")) {
        if (const auto t = yyjson_get_str(yyjson_obj_get(meta, "title"))) {
            out.title = t;
        }
    }
    size_t idx, max;
    yyjson_val* f;
    yyjson_arr_foreach(yyjson_obj_get(root, "files"), idx, max, f) {
        const auto name = yyjson_get_str(yyjson_obj_get(f, "name"));
        const auto source = yyjson_get_str(yyjson_obj_get(f, "source"));
        const auto format = yyjson_get_str(yyjson_obj_get(f, "format"));
        const auto md5 = yyjson_get_str(yyjson_obj_get(f, "md5"));
        const auto size_v = yyjson_obj_get(f, "size"); // a string in this api
        const u64 size = yyjson_is_str(size_v) ? std::strtoull(yyjson_get_str(size_v), nullptr, 10) : yyjson_get_uint(size_v);
        if (!name || !IsContent(source ? source : "", format ? format : "", size)) {
            continue;
        }
        out.files.push_back({name, md5 ? md5 : "", size});
    }
    log_write("[roms] %s: \"%s\", %zu content files\n", id.c_str(), out.title.c_str(), out.files.size());
    R_SUCCEED();
}

auto ToJobs(const Collection& c, const std::vector<RemoteFile>& files) -> std::vector<jobs::Job> {
    std::vector<jobs::Job> out;
    for (const auto& f : files) {
        out.push_back({DownloadUrl(c.item, f.name), DestPath(c.console, f.name), f.md5, hash::Type::Md5, f.size});
    }
    return out;
}

void Remove(const Collection& c, std::function<void()> reopen) {
    App::Push<ui::OptionBox>("Remove the collection "_i18n + c.label + "?\n" + "The downloaded files stay in "_i18n + ROMS_ROOT + c.console + "/",
        "Back"_i18n, "Remove"_i18n, 0, [c, reopen](auto op) {
            if (!op || *op != 1) return;
            auto list = Load();
            std::erase_if(list, [&](const Collection& e) { return e.console == c.console && e.item == c.item; });
            Save(list);
            reopen();
        });
}

// the item's files as a page: A downloads one, X/Y mark several, START downloads the marked ones or all
void OpenCollection(const Collection& c, std::function<void()> reopen) {
    auto item = std::make_shared<Item>();
    App::Push<ui::ProgressBox>(0, c.label, "", [item, c](auto pbox) -> Result {
        return FetchItem(pbox, c.item, *item);
    }, [item, c, reopen](Result rc) {
        if (rc == Result_TransferCancelled) return;
        App::PushErrorBox(rc, "Could not read the item on archive.org"_i18n);
        if (R_FAILED(rc)) return;

        // after a download the page is rebuilt, so "on the card" is current
        auto self = std::make_shared<SystemToolsMenu*>(nullptr);
        const std::function<void()> refresh = [self, c, reopen]() {
            if (*self) (*self)->SetPop();
            OpenCollection(c, reopen);
        };
        const std::string folder = std::string{ROMS_ROOT} + c.console + "/";
        std::vector<SettingsItem> rows;
        rows.push_back(ui::menu::settings::MakeHeader(c.label + " → " + folder + " (" + jobs::Describe(ToJobs(c, item->files)) + ")"));
        if (item->files.empty()) {
            SettingsItem row{"The item has no files to download"_i18n, ""};
            row.disabled = true;
            rows.push_back(std::move(row));
        }
        fs::FsNativeSd fs;
        for (const auto& f : item->files) {
            SettingsItem row{f.name, ui::menu::grid::FormatBytes(f.size) + (fs.FileExists(DestPath(c.console, f.name)) ? " · " + "on the card"_i18n : "")};
            row.selectable = true;
            row.action = [c, f, refresh]() { jobs::Run(c.label, ToJobs(c, {f}), refresh); };
            rows.push_back(std::move(row));
        }
        auto menu = std::make_unique<SystemToolsMenu>(c.label, std::move(rows));
        *self = menu.get();
        menu->SetOptions([c, item, reopen, refresh](const std::vector<size_t>& marked) {
            auto options = std::make_unique<ui::Sidebar>("Options"_i18n, ui::Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));
            if (!marked.empty()) {
                std::vector<RemoteFile> files;
                for (auto i : marked) {
                    if (i >= 1 && i - 1 < item->files.size()) files.push_back(item->files[i - 1]); // row 0 is the header
                }
                const auto picked = ToJobs(c, files);
                options->Add<ui::SidebarEntryCallback>("Download the marked files"_i18n,
                    [c, picked, refresh]() { jobs::Run(c.label, picked, refresh); }, true, jobs::Describe(picked))->SetIcon(ActionIcon::Dump);
            }
            const auto all = ToJobs(c, item->files);
            options->Add<ui::SidebarEntryCallback>("Download all"_i18n,
                [c, all, refresh]() { jobs::Run(c.label, all, refresh); }, true, jobs::Describe(all))->SetIcon(ActionIcon::Dump);
            options->Add<ui::SidebarEntryCallback>("Remove the collection"_i18n, [c, reopen]() { Remove(c, reopen); }, true)->SetIcon(ActionIcon::Delete);
        });
        App::Push(std::move(menu));
    });
}

// console folder -> item id (typed, or sent from a phone) -> the item is read and kept
void Add(std::function<void()> reopen) {
    ui::PopupList::Items names;
    for (const auto& c : Consoles()) {
        names.push_back(std::string{c.name} + "  (/roms/" + c.folder + ")");
    }
    App::Push<ui::PopupList>("Which console is the collection for?"_i18n, names, [reopen](auto op) {
        if (!op || *op < 0 || static_cast<size_t>(*op) >= Consoles().size()) return;
        const std::string console = Consoles()[*op].folder;
        ui::remote_input::Options opts;
        opts.title = "archive.org item"_i18n;
        opts.guide = "The item id, or its archive.org/details/... address"_i18n;
        opts.placeholder = "archive.org/details/...";
        ui::remote_input::PromptTextInput(opts, [console, reopen](const std::string& text) {
            const auto id = ItemFromInput(text);
            if (id.empty()) return;
            auto item = std::make_shared<Item>();
            App::Push<ui::ProgressBox>(0, id, "", [item, id](auto pbox) -> Result {
                return FetchItem(pbox, id, *item);
            }, [item, id, console, reopen](Result rc) {
                if (rc == Result_TransferCancelled) return;
                App::PushErrorBox(rc, "Could not read the item on archive.org"_i18n);
                if (R_FAILED(rc)) return;
                if (item->files.empty()) {
                    App::Push<ui::OptionBox>("The item "_i18n + id + " has no files to download."_i18n, "OK"_i18n);
                    return;
                }
                Collection c{console, id, item->title.empty() ? id : item->title};
                const auto plan = ToJobs(c, item->files);
                App::Push<ui::OptionBox>(c.label + "\n" + jobs::Describe(plan) + " → " + ROMS_ROOT + console + "/",
                    "Back"_i18n, "Keep the collection"_i18n, 1, [c, reopen](auto op) {
                        if (!op || *op != 1) return;
                        auto list = Load();
                        std::erase_if(list, [&](const Collection& e) { return e.console == c.console && e.item == c.item; });
                        list.push_back(c);
                        Save(list);
                        reopen();
                    });
            });
        });
    });
}

} // namespace

void AddRows(std::vector<SettingsItem>& rows, std::function<void()> reopen) {
    rows.push_back(ui::menu::settings::MakeHeader("ROMS"_i18n));
    for (const auto& c : Load()) {
        SettingsItem row{c.label, std::string{ConsoleName(c.console)} + " · archive.org/details/" + c.item + " → " + ROMS_ROOT + c.console + "/"};
        row.action = [c, reopen]() { OpenCollection(c, reopen); };
        rows.push_back(std::move(row));
    }
    SettingsItem add{"Add a collection"_i18n, "An archive.org item you choose; its files go to /roms/<console>/. The Hub lists no sources."_i18n};
    add.action = [reopen]() { Add(reopen); };
    rows.push_back(std::move(add));
}

} // namespace sphaira::rom
