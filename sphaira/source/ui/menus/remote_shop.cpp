#include "ui/menus/remote_shop.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "rom_library.hpp"
#include "yati/yati.hpp"
#include "yati/source/http.hpp"
#include "ui/install_queue.hpp"
#include "ui/remote_input.hpp"
#include "ui/sidebar.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include <algorithm>
#include <functional>
#include <memory>

namespace sphaira::ui::menu::shop {
namespace {

using ui::menu::settings::SettingsItem;
using ui::menu::tools::SystemToolsMenu;
using sphaira::shop::Item;
using sphaira::shop::Listing;

// the list is a screen of rows, not a database: a filter finds the rest
constexpr size_t PAGE_ROWS = 1000;
// rows of the list: 0 is the header, 1 the filter row, the files follow
constexpr size_t FIRST_FILE_ROW = 2;

// one install, as the queue runs it: yati reads the file over http, the extension says which container it is
void Queue(const Item& item, const std::string& user, const std::string& pass) {
    ui::InstallQueue::Get().Add(item.name, 0, [item, user, pass](ProgressBox* pbox) -> Result {
        sphaira::yati::source::Http source{item.url, user, pass};
        return sphaira::yati::InstallFromSource(pbox, &source, fs::FsPath{item.name.c_str()});
    });
}

auto Describe(const std::vector<Item>& items) -> std::string {
    std::uint64_t bytes{};
    for (const auto& i : items) bytes += i.size;
    return std::to_string(items.size()) + " " + "files"_i18n + (bytes ? ", " + grid::FormatBytes(bytes) : std::string{});
}

auto FilterRow(const std::string& words, std::function<void(std::string)> redraw) -> SettingsItem {
    SettingsItem row{"Filter by words"_i18n, words.empty() ? "Type words from the name to narrow the list"_i18n : "[" + words + "]"};
    row.action = [redraw]() {
        ui::remote_input::Options opts;
        opts.title = "Filter by words"_i18n;
        opts.guide = "Words from the file name, for example a game; empty shows every file"_i18n;
        ui::remote_input::PromptTextInput(opts, [redraw](const std::string& text) { redraw(text); });
    };
    return row;
}

auto KindLabel(const Item& item) -> std::string {
    if (!item.title_id) return {};
    switch (sphaira::shop::KindOf(item.title_id)) {
        case sphaira::shop::Kind::Base: return "Game"_i18n;
        case sphaira::shop::Kind::Update: return "Update"_i18n;
        case sphaira::shop::Kind::Dlc: return "DLC"_i18n;
    }
    return {};
}

void Show(const std::string& name, std::shared_ptr<const Listing> listing, const std::string& user, const std::string& pass, const std::string& words) {
    std::vector<Item> hits;
    for (const auto& item : listing->items) {
        if (sphaira::rom::MatchesWords(item.name, words)) hits.push_back(item);
    }
    std::sort(hits.begin(), hits.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
    const size_t shown = std::min(hits.size(), PAGE_ROWS);

    // the page is drawn again, with the words typed, in place of this one
    auto self = std::make_shared<SystemToolsMenu*>(nullptr);
    const std::function<void(std::string)> redraw = [self, name, listing, user, pass](std::string w) {
        if (*self) (*self)->SetPop();
        Show(name, listing, user, pass, w);
    };

    std::vector<SettingsItem> rows;
    auto header = name + (words.empty() ? "" : " · " + words) + " (" + Describe(hits) + ")";
    if (!listing->motd.empty()) header += " · " + listing->motd;
    rows.push_back(ui::menu::settings::MakeHeader(header));
    rows.push_back(FilterRow(words, redraw));
    if (hits.empty()) {
        SettingsItem row{"Nothing found"_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    }
    for (size_t i = 0; i < shown; i++) {
        const auto& item = hits[i];
        auto info = item.size ? grid::FormatBytes(item.size) : std::string{};
        if (const auto kind = KindLabel(item); !kind.empty()) info += (info.empty() ? "" : " · ") + kind;
        SettingsItem row{item.name, info};
        row.selectable = true;
        row.action = [item, user, pass]() { App::ConfirmInstall([item, user, pass]() { Queue(item, user, pass); }); };
        rows.push_back(std::move(row));
    }
    if (shown < hits.size()) {
        SettingsItem row{std::to_string(shown) + " " + "shown. Narrow the list with the filter above."_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    }

    auto menu = std::make_unique<SystemToolsMenu>(name, std::move(rows));
    *self = menu.get();
    menu->SetOptions([hits, shown, user, pass](const std::vector<size_t>& marked) {
        auto options = std::make_unique<ui::Sidebar>("Options"_i18n, ui::Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        std::vector<Item> picked;
        for (auto i : marked) {
            if (i >= FIRST_FILE_ROW && i - FIRST_FILE_ROW < shown) picked.push_back(hits[i - FIRST_FILE_ROW]);
        }
        const auto add = [&](const std::string& title, const std::vector<Item>& items) {
            options->Add<ui::SidebarEntryCallback>(title, [items, user, pass]() {
                App::ConfirmInstall([items, user, pass]() {
                    for (const auto& item : items) Queue(item, user, pass);
                });
            }, true, Describe(items))->SetIcon(ActionIcon::Dump);
        };
        if (!picked.empty()) add("Install the marked files"_i18n, picked);
        add("Install all shown"_i18n, std::vector<Item>(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(shown)));
    });
    App::Push(std::move(menu));
}

} // namespace

void OpenShop(const std::string& name, std::shared_ptr<const Listing> listing, const std::string& user, const std::string& pass) {
    Show(name, std::move(listing), user, pass, "");
}

} // namespace sphaira::ui::menu::shop
