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
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <set>

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
    bool restricted{}; // archive.org lets only signed-in users download it
};

auto IsTrue(yyjson_val* v) -> bool {
    const auto s = yyjson_get_str(v);
    return yyjson_is_true(v) || (s && std::string_view{s} == "true");
}

// the Hub cannot sign in to archive.org: such an item (or one file) is not offered, and the user is told why
void WarnRestricted(const std::string& id) {
    App::Push<ui::OptionBox>(id + "\n" + "This item is restricted: archive.org lets only signed-in users download it, and the Hub cannot sign in. Its files are not offered."_i18n, "OK"_i18n);
}

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
        out.restricted = IsTrue(yyjson_obj_get(meta, "access-restricted-item"));
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
        if (!name || !IsContent(source ? source : "", format ? format : "", size) || IsTrue(yyjson_obj_get(f, "private"))) {
            continue;
        }
        out.files.push_back({name, md5 ? md5 : "", size});
    }
    if (out.restricted) {
        out.files.clear();
    }
    log_write("[roms] %s: \"%s\", %zu content files%s\n", id.c_str(), out.title.c_str(), out.files.size(), out.restricted ? ", restricted" : "");
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

// ---- inside a whole-set archive: archive.org lists and serves its files one by one ----

constexpr size_t ARCHIVE_PAGE_ROWS = 1000;

auto InnerJobs(const Collection& c, const std::vector<ArchiveEntry>& entries) -> std::vector<jobs::Job> {
    std::vector<jobs::Job> out;
    std::set<std::string> seen;
    for (const auto& e : entries) {
        auto dest = DestPath(c.console, e.name);
        if (!seen.insert(dest).second) continue; // the same file name in two folders of the set: the first one only
        out.push_back({e.url, std::move(dest), "", hash::Type::Md5, e.size}); // archive.org gives no checksum here: the size is checked
    }
    return out;
}

// the files of the archive that match `words`: A downloads one, X/Y mark several, START downloads the marked ones or all
void ShowArchive(const Collection& c, const RemoteFile& archive, std::shared_ptr<const std::vector<ArchiveEntry>> all, const std::string& words) {
    std::vector<ArchiveEntry> hits;
    for (const auto& e : *all) {
        if (MatchesWords(e.name, words)) hits.push_back(e);
    }
    const size_t shown = std::min(hits.size(), ARCHIVE_PAGE_ROWS);

    auto self = std::make_shared<SystemToolsMenu*>(nullptr);
    const std::function<void(std::string)> redraw = [self, c, archive, all](std::string w) {
        if (*self) (*self)->SetPop();
        ShowArchive(c, archive, all, w);
    };
    const std::function<void()> refresh = [redraw, words]() { redraw(words); };

    const std::string folder = std::string{ROMS_ROOT} + c.console + "/";
    std::vector<SettingsItem> rows;
    rows.push_back(ui::menu::settings::MakeHeader(archive.name + (words.empty() ? "" : " · " + words) + " → " + folder + " (" + jobs::Describe(InnerJobs(c, hits)) + ")"));
    if (hits.empty()) {
        SettingsItem row{"Nothing found"_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    }
    fs::FsNativeSd fs;
    for (size_t i = 0; i < shown; i++) {
        const auto& e = hits[i];
        // the folder inside the set only crowds the name out; the filter still matches the whole path
        const auto slash = e.name.rfind('/');
        SettingsItem row{slash == std::string::npos ? e.name : e.name.substr(slash + 1),
            ui::menu::grid::FormatBytes(e.size) + (fs.FileExists(DestPath(c.console, e.name)) ? " · " + "on the card"_i18n : "")};
        row.selectable = true;
        row.action = [c, archive, e, refresh]() { jobs::Run(archive.name, InnerJobs(c, {e}), refresh); };
        rows.push_back(std::move(row));
    }
    if (shown < hits.size()) {
        SettingsItem row{std::to_string(shown) + " " + "shown. Narrow the list with a filter (START)."_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    }

    auto menu = std::make_unique<SystemToolsMenu>(archive.name, std::move(rows));
    *self = menu.get();
    menu->SetOptions([c, archive, hits, shown, redraw, refresh](const std::vector<size_t>& marked) {
        auto options = std::make_unique<ui::Sidebar>("Options"_i18n, ui::Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));
        std::vector<ArchiveEntry> picked_entries;
        for (auto i : marked) {
            if (i >= 1 && i - 1 < shown) picked_entries.push_back(hits[i - 1]); // row 0 is the header
        }
        if (!picked_entries.empty()) {
            const auto picked = InnerJobs(c, picked_entries);
            options->Add<ui::SidebarEntryCallback>("Download the marked files"_i18n,
                [c, archive, picked, refresh]() { jobs::Run(archive.name, picked, refresh); }, true, jobs::Describe(picked))->SetIcon(ActionIcon::Dump);
        }
        const auto all_hits = InnerJobs(c, hits);
        options->Add<ui::SidebarEntryCallback>("Download all"_i18n,
            [c, archive, all_hits, refresh]() { jobs::Run(archive.name, all_hits, refresh); }, true, jobs::Describe(all_hits))->SetIcon(ActionIcon::Dump);
        options->Add<ui::SidebarEntryCallback>("Filter by words"_i18n, [redraw]() {
            ui::remote_input::Options opts;
            opts.title = "Filter by words"_i18n;
            opts.guide = "Words from the file name, for example a game; empty shows every file"_i18n;
            ui::remote_input::PromptTextInput(opts, [redraw](const std::string& text) { redraw(text); });
        }, true)->SetIcon(ActionIcon::Search);
        const std::vector<jobs::Job> whole{{DownloadUrl(c.item, archive.name), DestPath(c.console, archive.name), "", hash::Type::Md5, archive.size}};
        options->Add<ui::SidebarEntryCallback>("Download the whole archive"_i18n,
            [archive, whole, refresh]() { jobs::Run(archive.name, whole, refresh); }, true, jobs::Describe(whole))->SetIcon(ActionIcon::Dump);
    });
    App::Push(std::move(menu));
}

struct Listing {
    std::vector<ArchiveEntry> entries;
    long code{};
};

// GET <archive url>/: archive.org's "View Archive" page, one row per file inside
auto FetchListing(ui::ProgressBox* pbox, const std::string& url, Listing& out) -> Result {
    pbox->NewTransfer("Reading the archive on archive.org"_i18n);
    const auto result = curl::Api().ToMemory(
        curl::Url{url},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_TRY(pbox->ShouldExitResult());
    out.code = result.code;
    R_UNLESS(result.success && !result.data.empty(), Result_GhdlFailedToDownloadAssetJson);
    out.entries = ParseArchiveListing({reinterpret_cast<const char*>(result.data.data()), result.data.size()});
    log_write("[roms] %s: %zu files inside\n", url.c_str(), out.entries.size());
    R_SUCCEED();
}

void OpenArchive(const Collection& c, const RemoteFile& archive) {
    auto listing = std::make_shared<Listing>();
    App::Push<ui::ProgressBox>(0, archive.name, "", [listing, c, archive](auto pbox) -> Result {
        return FetchListing(pbox, ArchiveListUrl(c.item, archive.name), *listing);
    }, [listing, c, archive](Result rc) {
        if (rc == Result_TransferCancelled) return;
        if (R_FAILED(rc)) {
            if (listing->code == 401 || listing->code == 403) {
                WarnRestricted(c.item);
            } else {
                App::PushErrorBox(rc, "Could not read the archive on archive.org"_i18n);
            }
            return;
        }
        ShowArchive(c, archive, std::make_shared<const std::vector<ArchiveEntry>>(std::move(listing->entries)), "");
    });
}

// the item's files as a page: A downloads one (a big zip, 7z or rar opens instead), X/Y mark several, START downloads the marked ones or all
void OpenCollection(const Collection& c, std::function<void()> reopen) {
    auto item = std::make_shared<Item>();
    App::Push<ui::ProgressBox>(0, c.label, "", [item, c](auto pbox) -> Result {
        return FetchItem(pbox, c.item, *item);
    }, [item, c, reopen](Result rc) {
        if (rc == Result_TransferCancelled) return;
        App::PushErrorBox(rc, "Could not read the item on archive.org"_i18n);
        if (R_FAILED(rc)) return;
        if (item->restricted) WarnRestricted(c.item);

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
            const bool inside = LookInside(f.name, f.size);
            SettingsItem row{f.name, ui::menu::grid::FormatBytes(f.size) + (inside ? " · " + "a set: A picks files inside"_i18n : fs.FileExists(DestPath(c.console, f.name)) ? " · " + "on the card"_i18n : "")};
            row.selectable = true;
            if (inside) {
                row.action = [c, f]() { OpenArchive(c, f); };
            } else {
                row.action = [c, f, refresh]() { jobs::Run(c.label, ToJobs(c, {f}), refresh); };
            }
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

// the item is read; its title, file count and size are shown; Keep saves it as a collection
void ConfirmItem(const std::string& console, const std::string& id, std::function<void()> reopen) {
    auto item = std::make_shared<Item>();
    App::Push<ui::ProgressBox>(0, id, "", [item, id](auto pbox) -> Result {
        return FetchItem(pbox, id, *item);
    }, [item, id, console, reopen](Result rc) {
        if (rc == Result_TransferCancelled) return;
        App::PushErrorBox(rc, "Could not read the item on archive.org"_i18n);
        if (R_FAILED(rc)) return;
        if (item->restricted) {
            WarnRestricted(id);
            return;
        }
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
}

// GET archive.org/advancedsearch.php: the items, most downloaded first
auto FetchSearch(ui::ProgressBox* pbox, const std::string& query, std::vector<SearchHit>& out) -> Result {
    pbox->NewTransfer("Searching archive.org"_i18n);
    const auto result = curl::Api().ToMemory(
        curl::Url{SearchUrl(query)},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_TRY(pbox->ShouldExitResult());
    R_UNLESS(result.success && !result.data.empty(), Result_GhdlFailedToDownloadAssetJson);
    auto doc = yyjson_read(reinterpret_cast<const char*>(result.data.data()), result.data.size(), 0);
    R_UNLESS(doc, Result_GhdlFailedToDownloadAssetJson);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const auto docs = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "response"), "docs");
    size_t idx, max;
    yyjson_val* d;
    yyjson_arr_foreach(docs, idx, max, d) {
        const auto id = yyjson_get_str(yyjson_obj_get(d, "identifier"));
        if (!id) continue;
        const auto title = yyjson_get_str(yyjson_obj_get(d, "title"));
        out.push_back({id, title ? title : id, yyjson_get_uint(yyjson_obj_get(d, "downloads")), yyjson_get_uint(yyjson_obj_get(d, "item_size"))});
    }
    log_write("[roms] search \"%s\": %zu hits\n", query.c_str(), out.size());
    R_SUCCEED();
}

// the search results as a page; A on a row reads that item
void OpenSearch(const std::string& console, const std::string& words, std::function<void()> reopen) {
    auto hits = std::make_shared<std::vector<SearchHit>>();
    const auto query = SearchQuery(console, words);
    const std::string title = words.empty() ? "Popular for "_i18n + ConsoleName(console) : words;
    App::Push<ui::ProgressBox>(0, title, "", [hits, query](auto pbox) -> Result {
        return FetchSearch(pbox, query, *hits);
    }, [hits, title, console, reopen](Result rc) {
        if (rc == Result_TransferCancelled) return;
        App::PushErrorBox(rc, "Could not search archive.org"_i18n);
        if (R_FAILED(rc)) return;
        std::vector<SettingsItem> rows;
        rows.push_back(ui::menu::settings::MakeHeader(title + " → " + ROMS_ROOT + console + "/"));
        if (hits->empty()) {
            SettingsItem row{"Nothing found"_i18n, ""};
            row.disabled = true;
            rows.push_back(std::move(row));
        }
        for (const auto& h : *hits) {
            SettingsItem row{h.title, ui::menu::grid::FormatBytes(h.size) + " · " + std::to_string(h.downloads) + " " + "downloads"_i18n + " · " + h.identifier};
            row.action = [console, id = h.identifier, reopen]() { ConfirmItem(console, id, reopen); };
            rows.push_back(std::move(row));
        }
        App::Push<SystemToolsMenu>(title, std::move(rows));
    });
}

// console folder -> popular items, a search, or an item id -> the item is read and kept
void Add(std::function<void()> reopen) {
    ui::PopupList::Items names;
    for (const auto& c : Consoles()) {
        names.push_back(std::string{c.name} + "  (/roms/" + c.folder + ")");
    }
    App::Push<ui::PopupList>("Which console is the collection for?"_i18n, names, [reopen](auto op) {
        if (!op || *op < 0 || static_cast<size_t>(*op) >= Consoles().size()) return;
        const std::string console = Consoles()[*op].folder;
        ui::PopupList::Items ways{"Popular on archive.org"_i18n, "Search archive.org"_i18n, "Enter an item id"_i18n};
        App::Push<ui::PopupList>(std::string{ConsoleName(console)} + " → " + ROMS_ROOT + console + "/", ways, [console, reopen](auto way) {
            if (!way) return;
            if (*way == 0) {
                OpenSearch(console, "", reopen);
                return;
            }
            ui::remote_input::Options opts;
            opts.title = *way == 1 ? "Search archive.org"_i18n : "archive.org item"_i18n;
            opts.guide = *way == 1 ? "Words from the title, for example a game or a set name"_i18n : "The item id, or its archive.org/details/... address"_i18n;
            opts.placeholder = *way == 1 ? "" : "archive.org/details/...";
            ui::remote_input::PromptTextInput(opts, [console, reopen, search = *way == 1](const std::string& text) {
                if (search) {
                    if (!text.empty()) OpenSearch(console, text, reopen);
                    return;
                }
                const auto id = ItemFromInput(text);
                if (!id.empty()) ConfirmItem(console, id, reopen);
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
