#include "ui/menus/l_button.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include "app.hpp"
#include "hats_version.hpp"
#include "i18n.hpp"
#include "l_button_targets.hpp"
#include "option.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sphaira::ui::menu::l_button {
namespace {

using sphaira::l_button::Target;
using sphaira::l_button::TARGETS;

option::OptionString g_choice{"main_menu", "l_button", ""}; // "<group>/<en.json key>", "" = nothing

struct Row {
    std::string label;
    std::string description;
    std::function<void()> action;
};

// the live rows of a group, built the way its own menu builds them
auto Rows(std::string_view group) -> std::vector<Row> {
    std::vector<Row> out;
    const auto add = [&out](const auto& items) {
        for (const auto& i : items) {
            out.push_back({i.label, i.description, i.action});
        }
    };
    if (group == "tile") {
        add(tools::TileItems());
    } else if (group == "game") {
        add(tools::GameToolItems());
    } else if (group == "software") {
        add(settings::BuildSoftwareItems());
    } else if (group == "system") {
        add(tools::SystemToolItems());
    } else if (group == "transfer") {
        add(ConsoleTransferItems());
    } else if (group == "share") {
        add(InstallShareItems());
    }
    return out;
}

// ponytail: rebuilds the group's list per lookup; fine for ~50 rows on a page open or an L press (Label() caches)
auto Resolve(const Target& t) -> std::optional<Row> {
    if (t.key == "Kefir Settings" && sphaira::hats::getKefirVersion().empty()) {
        return std::nullopt; // the Tools tab hides it off Kefir too
    }
    const auto label = i18n::get(t.key);
    for (auto& r : Rows(t.group)) {
        if (r.label == label) {
            return std::move(r);
        }
    }
    return std::nullopt;
}

auto Chosen() -> const Target* {
    return sphaira::l_button::Find(g_choice.Get());
}

auto Choice(std::string label, std::string description, bool done, std::function<void()> action) -> settings::SettingsItem {
    settings::SettingsItem item{std::move(label), std::move(description), {}, std::move(action)};
    item.done = done;
    return item;
}

// the picker pages on screen, popped together once a choice is made
using Pages = std::shared_ptr<std::vector<Widget*>>;

void Pick(const Target* t, const Pages& pages) {
    g_choice.Set(t ? sphaira::l_button::MakeId(*t) : std::string{});
    for (auto* p : *pages) {
        p->SetPop();
    }
}

void PushPage(const std::string& title, std::vector<settings::SettingsItem> items, const Pages& pages) {
    auto page = std::make_unique<tools::SystemToolsMenu>(title, std::move(items));
    pages->push_back(page.get());
    App::Push(std::move(page));
}

// a Tools tile that is a menu: the whole menu first, then each of its rows
void OpenSub(const Target& tile, const Row& tile_row, const Pages& parent) {
    const auto* chosen = Chosen();
    auto pages = std::make_shared<std::vector<Widget*>>(*parent);
    std::vector<settings::SettingsItem> items;
    items.push_back(Choice("The whole menu"_i18n, tile_row.description, chosen == &tile, [&tile, pages]() { Pick(&tile, pages); }));
    for (const auto& t : TARGETS) {
        if (t.group != tile.sub) {
            continue;
        }
        if (auto row = Resolve(t)) {
            items.push_back(Choice(row->label, row->description, chosen == &t, [&t, pages]() { Pick(&t, pages); }));
        }
    }
    PushPage(tile_row.label, std::move(items), pages);
}

void AddGroup(std::vector<settings::SettingsItem>& items, std::string_view group, const Pages& pages) {
    const auto* chosen = Chosen();
    for (const auto& t : TARGETS) {
        if (t.group != group) {
            continue;
        }
        auto row = Resolve(t);
        if (!row) {
            continue;
        }
        if (t.sub.empty()) {
            items.push_back(Choice(row->label, row->description, chosen == &t, [&t, pages]() { Pick(&t, pages); }));
            continue;
        }
        const bool inside = chosen && (chosen == &t || chosen->group == t.sub);
        auto open = [&t, r = *row, pages]() { OpenSub(t, r, pages); };
        auto item = Choice(row->label + "  ›", row->description, inside, std::move(open));
        items.push_back(std::move(item));
    }
}

} // namespace

auto Label() -> std::string {
    const auto* t = Chosen();
    if (!t) {
        return {};
    }
    // the Settings row draws this every frame: resolve once per choice and language
    static std::string cached_key, cached_label;
    auto key = g_choice.Get() + '\n' + i18n::get(t->key);
    if (key != cached_key) {
        const auto row = Resolve(*t);
        cached_label = row ? row->label : std::string{};
        cached_key = std::move(key);
    }
    return cached_label;
}

void Run() {
    if (const auto* t = Chosen()) {
        if (auto row = Resolve(*t); row && row->action) {
            row->action();
        }
    }
}

void OpenPicker() {
    auto pages = std::make_shared<std::vector<Widget*>>();
    std::vector<settings::SettingsItem> items;
    items.push_back(Choice("Off"_i18n, "L turns the page in the list, as before."_i18n, Chosen() == nullptr, [pages]() { Pick(nullptr, pages); }));
    items.push_back(settings::MakeHeader("Tools"_i18n));
    AddGroup(items, "tile", pages);
    items.push_back(settings::MakeHeader("Install & Share"_i18n));
    AddGroup(items, "share", pages);
    PushPage("L button on the main screen"_i18n, std::move(items), pages);
}

} // namespace sphaira::ui::menu::l_button
