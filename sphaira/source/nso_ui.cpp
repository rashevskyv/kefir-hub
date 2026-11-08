// "Add to Nintendo Switch Online": the sidebar entries, the questions to the user and the worker thread.

#include "nso_ui.hpp"
#include "nso_internal.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "rom_collections_ui.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <memory>

namespace sphaira::nso_ui {
namespace {

struct Item {
    std::string path;
    std::string name;
    nso::System sys{};
    std::vector<u8> head; // for the N64 check only
};

struct Pending {
    Item item;
    std::vector<nso::System> candidates;
};

struct Plan {
    std::string console;
    std::vector<Item> items;
    std::vector<Pending> pending;
    std::vector<std::string> errors;
    size_t next{};
};

struct Outcome {
    int added{};
    int total{};
    std::vector<std::string> errors;
};

auto Tr(const std::string& s) -> std::string {
    return i18n::get(s);
}

void ShowText(const std::string& text) {
    App::Push<ui::OptionBox>(text, "OK"_i18n);
}

auto ErrorText(const std::vector<std::string>& errors) -> std::string {
    std::string o;
    for (size_t i = 0; i < errors.size() && i < 4; i++) o += (i ? "\n" : "") + errors[i];
    if (errors.size() > 4) o += "\n... " + std::to_string(errors.size() - 4);
    return o;
}

bool ReadHead(const std::string& path, std::vector<u8>& head, u64& size) {
    fs::FsNativeSd sd;
    fs::File f;
    s64 sz{};
    if (R_FAILED(sd.OpenFile(fs::FsPath{path.c_str()}, FsOpenMode_Read, &f)) || R_FAILED(f.GetSize(&sz)) || sz < 0) return false;
    size = static_cast<u64>(sz);
    head.resize(static_cast<size_t>(std::min<u64>(size, nso::kHeadSize)));
    u64 n{};
    if (R_FAILED(f.Read(0, head.data(), head.size(), FsReadOption_None, &n))) return false;
    head.resize(static_cast<size_t>(n));
    return true;
}

// ---------------------------------------------------------------- worker
auto Run(ui::ProgressBox* pbox, const std::shared_ptr<Plan>& plan, const std::shared_ptr<Outcome>& out) -> Result {
    std::array<SystemState, nso::kSystemCount> states;
    fs::FsNativeSd sd;
    const auto total = plan->items.size();
    for (size_t i = 0; i < total; i++) {
        R_TRY(pbox->ShouldExitResult()); // what is done so far is kept
        const auto& item = plan->items[i];
        pbox->SetTitle(item.name);
        pbox->UpdateTransfer(static_cast<s64>(i + 1), static_cast<s64>(total));
        const auto fail = [&](const std::string& msg) { out->errors.push_back(item.name + ": " + Tr(msg)); };

        auto& st = states[size_t(item.sys)];
        pbox->NewTransfer("Reading the game list"_i18n);
        if (!LoadState(item.sys, st)) { fail(st.error); continue; }

        std::vector<u8> bytes;
        if (R_FAILED(sd.read_entire_file(fs::FsPath{item.path.c_str()}, bytes))) { fail("Cannot read the ROM"); continue; }

        nso::AddRequest req;
        req.system = item.sys;
        req.rom = bytes;
        req.file_name = item.name;
        req.db_text = st.db_text;
        req.marker_text = st.marker_text;
        req.mtime = static_cast<u32>(std::time(nullptr));
        const auto add = nso::BuildAdd(req);
        if (!add.ok) { fail(add.error); continue; }

        ImageResult cover{};
        if (!plan->console.empty()) {
            const auto png = rom::FetchCover(pbox, plan->console, item.name);
            if (!png.empty()) cover = ImageLoadFromMemory(png);
        }

        pbox->NewTransfer("Writing the files"_i18n);
        std::string error;
        const auto rc = WriteAdd(add, bytes, cover, st, error);
        if (R_FAILED(rc)) { fail(error); continue; }
        out->added++;
    }
    R_SUCCEED();
}

void Finish(const std::shared_ptr<Outcome>& out, Result rc) {
    if (R_FAILED(rc) && rc != Result_TransferCancelled) {
        App::PushErrorBox(rc, "Add failed!"_i18n);
        return;
    }
    if (out->added > 0) {
        App::Notify("Added to Nintendo Switch Online"_i18n + ": " + std::to_string(out->added) + " / " + std::to_string(out->total));
    } else if (out->errors.empty()) {
        App::Notify("Nothing was added"_i18n);
    }
    if (!out->errors.empty()) ShowText(ErrorText(out->errors));
}

void StartWork(const std::shared_ptr<Plan>& plan) {
    if (plan->items.empty()) {
        if (!plan->errors.empty()) ShowText(ErrorText(plan->errors));
        return;
    }
    auto out = std::make_shared<Outcome>();
    out->total = static_cast<int>(plan->items.size());
    out->errors = plan->errors;
    for (auto& it : plan->items) it.head = {};
    App::Push<ui::ProgressBox>(0, "Adding to Nintendo Switch Online"_i18n, "",
        [plan, out](auto pbox) -> Result { return Run(pbox, plan, out); },
        [out](Result rc) { Finish(out, rc); });
}

bool NeedsWarning(const Item& it) {
    return (it.sys == nso::System::N64 || it.sys == nso::System::N64Mature) && nso::N64NeedsWarning(it.head);
}

// N64 games without a tested emulator config may not start: one question for all of them.
void AskExperimental(const std::shared_ptr<Plan>& plan) {
    std::string names;
    int count = 0;
    for (const auto& it : plan->items) {
        if (NeedsWarning(it) && count++ < 3) names += "\n" + it.name;
    }
    if (!count) return StartWork(plan);
    App::Push<ui::OptionBox>("This game has no tested configuration. It may not start. Please send feedback."_i18n + "\n" + names,
        "Back"_i18n, "Add anyway"_i18n, 0, [plan](std::optional<s64> op) {
            if (!(op && *op == 1)) std::erase_if(plan->items, NeedsWarning);
            StartWork(plan);
        });
}

// one question per ROM that fits several installed apps (a .bin: Genesis, SNES or Virtual Boy; an N64 ROM: normal or Mature)
void AskNext(const std::shared_ptr<Plan>& plan) {
    if (plan->next >= plan->pending.size()) return AskExperimental(plan);
    const auto& p = plan->pending[plan->next];
    const auto& c = p.candidates;
    const auto msg = "Which Nintendo Switch Online app is this game for?"_i18n + "\n" + p.item.name;
    const auto done = [plan, n = c.size()](std::optional<s64> op) {
        auto& pend = plan->pending[plan->next];
        if (op && *op >= 0 && size_t(*op) < n) {
            pend.item.sys = pend.candidates[size_t(*op)];
            plan->items.push_back(std::move(pend.item));
        }
        plan->next++;
        AskNext(plan);
    };
    const auto name = [&](size_t i) { return std::string{nso::Info(c[i]).name}; };
    if (c.size() >= 3) App::Push<ui::OptionBox>(msg, name(0), name(1), name(2), 0, done);
    else App::Push<ui::OptionBox>(msg, name(0), name(1), 0, done);
}

void StartAdd(const std::string& dir, const std::string& console, const std::vector<std::string>& names, const nso::Installed& installed) {
    auto plan = std::make_shared<Plan>();
    plan->console = console;
    for (const auto& name : names) {
        Item item;
        item.name = name;
        item.path = nso::JoinPath(dir, name);
        u64 size{};
        if (!ReadHead(item.path, item.head, size)) { plan->errors.push_back(name + ": " + "Cannot read the ROM"_i18n); continue; }
        if (size > nso::kMaxRomSize) { plan->errors.push_back(name + ": " + "The ROM is bigger than 64 MiB"_i18n); continue; }
        const auto t = nso::PlanTarget(name, item.head, size, installed);
        switch (t.status) {
            case nso::TargetPlan::Status::Error:
                plan->errors.push_back(name + ": " + Tr(t.error));
                break;
            case nso::TargetPlan::Status::Chosen:
                item.sys = t.chosen;
                plan->items.push_back(std::move(item));
                break;
            case nso::TargetPlan::Status::NeedChoice:
                plan->pending.push_back({std::move(item), t.candidates});
                break;
        }
    }
    AskNext(plan);
}

// ---------------------------------------------------------------- remove
void ConfirmRemove(nso::System s, const std::string& code, const std::string& title) {
    App::Push<ui::OptionBox>("Remove this game from Nintendo Switch Online?"_i18n + "\n" + title, "Back"_i18n, "Remove"_i18n, 0,
        [s, code](std::optional<s64> op) {
            if (!(op && *op == 1)) return;
            std::string error;
            const auto rc = RemoveAdded(s, code, error);
            if (R_SUCCEEDED(rc)) App::Notify("Removed"_i18n);
            else ShowText(Tr(error.empty() ? "Remove failed!" : error));
        });
}

void StartRemove(const nso::Installed& installed) {
    auto sidebar = std::make_unique<ui::Sidebar>("Remove from Nintendo Switch Online"_i18n, ui::Sidebar::Side::RIGHT);
    int count = 0;
    for (const auto& si : nso::kSystems) {
        SystemState st;
        if (!installed[size_t(si.id)] || !LoadOverlayOnly(si.id, st)) continue;
        const auto db = nso::titlesdb::Scan(st.db_text);
        for (const auto& code : nso::ListAdded(st.db_text, st.marker_text)) {
            auto title = nso::EntryTitle(nso::titlesdb::EntryText(st.db_text, db, code));
            if (title.empty()) title = code;
            sidebar->Add<ui::SidebarEntryCallback>(title, [s = si.id, code, title]() {
                ConfirmRemove(s, code, title);
            }, true, si.name);
            count++;
        }
    }
    if (!count) return App::Notify("Nothing to remove"_i18n);
    App::Push(std::move(sidebar));
}

} // namespace

void AddOptions(ui::Sidebar* sidebar, const std::string& dir, const std::string& console, const std::vector<std::string>& names) {
    if (names.empty()) return;
    const auto installed = DetectInstalled();
    if (!nso::AnyInstalled(installed)) return;
    const bool any = std::ranges::any_of(names, [&](const auto& n) { return nso::CanTakeExt(nso::ExtOf(n), installed); });
    if (!any) return;

    sidebar->Add<ui::SidebarEntryCallback>("Add to Nintendo Switch Online"_i18n, [=]() {
        StartAdd(dir, console, names, installed);
    }, true, std::to_string(names.size()) + " " + "files"_i18n);

    fs::FsNativeSd sd;
    const bool has_added = std::ranges::any_of(nso::kSystems, [&](const auto& s) {
        return installed[size_t(s.id)] && sd.FileExists(OverlayPath(s.id, nso::MarkerRelPath()));
    });
    if (has_added) {
        sidebar->Add<ui::SidebarEntryCallback>("Remove from Nintendo Switch Online"_i18n, [=]() {
            StartRemove(installed);
        }, true, "Games that the Hub added"_i18n);
    }
}

} // namespace sphaira::nso_ui
