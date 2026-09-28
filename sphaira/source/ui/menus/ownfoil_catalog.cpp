#include "ui/menus/ownfoil_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace sphaira::ui::menu::ownfoil {

auto Menu::GetPageMax() -> s64 {
    const auto size = m_page_size.Get();
    if (m_total <= 0 || size <= 0) {
        return 1;
    }
    return (m_total + size - 1) / size;
}

auto Menu::BuildQuery(s64 page) -> sphaira::ownfoil::api::CatalogQuery {
    auto sort = m_sort.Get();
    if (sort < 0 || sort >= static_cast<s64>(std::size(SORT_MODES))) {
        sort = 0;
    }

    sphaira::ownfoil::api::CatalogQuery query{};
    // the shop pages from 1; m_page counts from 0 like every index here.
    query.page = page + 1;
    query.page_size = m_page_size.Get();
    query.order_field = SORT_MODES[sort].field;
    query.descending = m_sort_desc.Get();
    // the id lists this leaves empty are the worker's to fill, since the console
    // has to be scanned for them.
    query.category = CATEGORIES[m_category].category;
    query.search = m_search;
    return query;
}

auto Menu::PrepareQuery(sphaira::ownfoil::api::CatalogQuery& query, const std::string& base_url, const sphaira::ownfoil::Config& config, std::stop_token token, std::string& error) -> bool {
    using Category = sphaira::ownfoil::api::Category;

    // neither asks anything about this console, so the shop answers both alone.
    if (query.category == Category::All || query.category == Category::Search) {
        return true;
    }

    std::scoped_lock lock{m_installed_mutex};

    // the scan describes the console, so it outlives any one page: done once, and
    // again only when a category needs more of it, or an install changed it.
    // walking each title's content costs an ns query per title.
    const auto content = query.category != Category::NewGames;
    const auto installs = g_installs.load();
    const auto rescan = installs != m_installs_scanned;
    if (!m_installed_scanned || rescan || (content && !m_installed_content)) {
        m_installed = sphaira::ownfoil::installed::Scan(content);
        m_installed_scanned = true;
        m_installed_content = content;
        m_installs_scanned = installs;
    }

    switch (query.category) {
        case Category::All:
        case Category::Search:
            break;

        case Category::NewGames:
            query.app_ids = sphaira::ownfoil::installed::TitleIds(m_installed);
            break;

        case Category::Dlc:
            query.title_ids = sphaira::ownfoil::installed::TitleIds(m_installed);
            query.app_ids = m_installed.dlc_ids;
            break;

        case Category::Updates: {
            // a round trip whose answer is the same for every page, so it is
            // asked on page 1 - where the category is entered and a changed sort
            // lands - and after an install.
            if (query.page <= 1 || rescan) {
                m_outdated.clear();

                std::vector<sphaira::ownfoil::api::ShopUpdate> updates;
                if (!sphaira::ownfoil::api::FetchUpdates(base_url, config, token, sphaira::ownfoil::installed::TitleIds(m_installed), updates, error)) {
                    return false;
                }

                for (const auto& update : updates) {
                    if (update.version > sphaira::ownfoil::installed::UpdateVersionOf(m_installed, update.title_id)) {
                        m_outdated.emplace_back(update.app_id);
                    }
                }
            }

            query.app_ids = m_outdated;
        }   break;
    }

    return true;
}

void Menu::LoadPage(s64 page) {
    // the page on screen stays up until the next lands, with no modal: a fetch is
    // a few tens of milliseconds, and a dialog that appears and vanishes inside
    // that reads as slower than none. a request arriving meanwhile is held rather
    // than dropped, since `Order` can be flipped faster than the shop answers.
    if (m_loading) {
        m_pending_page = page;
        return;
    }

    // option::Get() fills its cache on first call, so the query is built here
    // rather than on the worker.
    const auto query = BuildQuery(page);
    const auto config = m_config;
    const auto base_url = m_base_url;
    // the page on screen owns the fetch that would replace it, so leaving it
    // abandons the request.
    const auto token = m_page_stop.get_token();

    m_loading = true;

    // owned by the menu rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_loader = std::make_unique<utils::Async>([this, config, base_url, query, page, token](){
        // the result travels with the event rather than through a member: no
        // dialog is holding the main thread off one this time.
        std::vector<sphaira::ownfoil::api::ShopApp> apps;
        s64 total{};
        std::string error;
        auto page_query = query;
        if (PrepareQuery(page_query, base_url, config, token, error)) {
            sphaira::ownfoil::api::FetchApps(base_url, config, token, page_query, apps, total, error);
        }

        evman::push(evman::CallbackEventData{
            [this, page, total, apps = std::move(apps), error = std::move(error)]() mutable {
                if (!error.empty()) {
                    m_loading = false;
                    App::Notify(m_config.name + ": " + error);
                } else {
                    // ApplyPage clears the flag by way of FreeEntries.
                    ApplyPage(page, apps, total);
                }

                // whatever was asked for mid-flight, issued now against the
                // options as they stand.
                if (m_pending_page) {
                    const auto next = *m_pending_page;
                    m_pending_page.reset();
                    LoadPage(next);
                }
            },
            token
        }, false);
    });
}

void Menu::ApplyPage(s64 page, std::vector<sphaira::ownfoil::api::ShopApp>& apps, s64 total) {
    m_page = page;
    m_total = total;

    FreeEntries();
    m_entries.reserve(apps.size());
    for (auto& app : apps) {
        Entry entry{};
        entry.app = std::move(app);
        m_entries.emplace_back(std::move(entry));
    }
    apps.clear();

    // a fresh page starts at the top.
    m_entry_index = 0;
    m_list->SetYoff(0);
    m_banner_name.Reset();
    m_banner_publisher.Reset();

    char subheading[64];
    std::snprintf(subheading, sizeof(subheading), "Page %ld / %ld"_i18n.c_str(),
        m_page + 1, GetPageMax());
    SetSubHeading(subheading);

    // the count belongs beside what it counts, in the title row.
    SetCatalogTitle();
}

void Menu::SetCatalogTitle() {
    // what is shown and how much of it, beside the shop's name. what orders it is
    // left to the sidebar: the row is bounded by the status text to its right,
    // and a third part is what tips it into scrolling.
    auto title = i18n::get(CATEGORIES[m_category].name);

    // "Search" alone says nothing about what is on screen, so the term shares the
    // same slot rather than taking another.
    if (CATEGORIES[m_category].category == sphaira::ownfoil::api::Category::Search && !m_search.empty()) {
        title += ": " + m_search;
    }

    // until a page answers, "0 titles" would read as a claim about the shop
    // rather than about the wait.
    if (m_total > 0) {
        char total[64];
        std::snprintf(total, sizeof(total), "%ld titles"_i18n.c_str(), m_total);
        title += SEPARATOR;
        title += total;
    }

    SetTitleSubHeading(title);
}

void Menu::ShowCategories() {
    // a panel down the left rather than an options entry: this is the one choice
    // that says what the whole screen is about, and is made often enough for a
    // button of its own.
    auto panel = std::make_unique<Sidebar>("Show"_i18n, i18n::get(CATEGORIES[m_category].name), Sidebar::Side::LEFT);
    ON_SCOPE_EXIT(App::Push(std::move(panel)));

    for (s64 i = 0; i < static_cast<s64>(std::size(CATEGORIES)); i++) {
        const auto& mode = CATEGORIES[i];
        panel->Add<SidebarEntryCallback>(i18n::get(mode.name), [this, i](){
            // Search says nothing on its own, so picking it always asks for a
            // term - re-picking it is how a different search is started.
            if (CATEGORIES[i].category == sphaira::ownfoil::api::Category::Search) {
                ShowSearch(i);
                return;
            }

            if (i == m_category) {
                return;
            }

            m_category = i;
            SetCatalogTitle();
            // a different catalog, so there is no page of the old one to stay on.
            LoadPage(0);
        }, true);
    }
}

void Menu::ShowSearch(s64 index) {
    std::string term;

    // the term on screen is the initial text: narrowing a search that came back
    // too wide is far more common than starting an unrelated one.
    if (R_FAILED(swkbd::ShowText(term, "Search"_i18n.c_str(), "Name or id"_i18n.c_str(), m_search.c_str(), 1, SEARCH_MAX)) || term.empty()) {
        // backing out of the keyboard leaves the catalog that was up, up.
        return;
    }

    // the same term against the same catalog would come back the same.
    if (index == m_category && term == m_search) {
        return;
    }

    m_search = term;
    m_category = index;
    SetCatalogTitle();
    LoadPage(0);
}

void Menu::ShowOptions() {
    auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (m_mode == Mode::ServerList) {
        options->Add<SidebarEntryCallback>("Add server"_i18n, [this](){
            App::Push<sphaira::ownfoil::OwnfoilForm>(sphaira::ownfoil::Config{}, [this](const auto&){
                ReloadSaved();
            });
        }, true, "Add a server."_i18n);

        // not on Y: MainMenu claims it for its menu picker while this is a tab.
        if (!m_focus_discover && m_index < static_cast<s64>(m_candidates.size())) {
            options->Add<SidebarEntryCallback>("Edit server"_i18n, [this](){
                EditSelected();
            }, true, "Change the selected server's name, addresses or login."_i18n);
        }

        if (!m_focus_discover && m_index < static_cast<s64>(m_candidates.size()) && m_candidates[m_index].saved) {
            options->Add<SidebarEntryCallback>("Delete server"_i18n, [this](){
                DeleteSelected();
            }, true, "Forget the selected server."_i18n);
        }
    } else {
        SidebarEntryArray::Items view_items;
        view_items.push_back("Icon"_i18n);
        view_items.push_back("Banner"_i18n);
        view_items.push_back("Detail"_i18n);

        options->Add<SidebarEntryArray>("View"_i18n, view_items, [this](s64& index_out){
            m_view.Set(index_out);
            OnViewChange();
        }, m_view.Get(), "Change titles view. Also cycled with -."_i18n);

        SidebarEntryArray::Items page_items;
        s64 page_index{};
        for (size_t i = 0; i < std::size(PAGE_SIZES); i++) {
            page_items.emplace_back(std::to_string(PAGE_SIZES[i]));
            if (PAGE_SIZES[i] == m_page_size.Get()) {
                page_index = static_cast<s64>(i);
            }
        }

        options->Add<SidebarEntryArray>("Per page"_i18n, page_items, [this](s64& index_out){
            const auto size = PAGE_SIZES[index_out];
            if (size == m_page_size.Get()) {
                return;
            }
            m_page_size.Set(size);
            // the page boundaries have moved, so there is no page to stay on.
            LoadPage(0);
        }, page_index, "How many titles displayed per page."_i18n);

        SidebarEntryArray::Items sort_items;
        for (const auto& mode : SORT_MODES) {
            sort_items.emplace_back(i18n::get(mode.name));
        }

        // the shop does the sorting, and a different ordering is a different
        // catalog, so either of these refetches from page 1.
        options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
            if (index_out == m_sort.Get()) {
                return;
            }
            m_sort.Set(index_out);
            LoadPage(0);
        }, m_sort.Get());

        // two states, so A flips it in place rather than opening a picker. stored
        // here, not through the OptionBool overload, which runs its callback
        // before the store and would refetch the old order.
        options->Add<SidebarEntryBool>("Order"_i18n, m_sort_desc.Get(), [this](bool& descending){
            m_sort_desc.Set(descending);
            LoadPage(0);
        }, "", "Descending"_i18n, "Ascending"_i18n);

        // last: it leaves the catalog, where everything above adjusts it.
        options->Add<SidebarEntryCallback>("Servers list"_i18n, [this](){
            RebuildCandidates();
            SetMode(Mode::ServerList);
        }, true, "Return to the server list."_i18n);
    }

    // on both lists: every shop's artwork shares the one cache.
    options->Add<SidebarEntryCallback>("Clear cache"_i18n, [](){
        App::Push<OptionBox>(
            "Remove all cached artwork from the SD card?"_i18n,
            "No"_i18n, "Yes"_i18n, 0, [](std::optional<s64> op_index) {
                if (!op_index || !*op_index) {
                    return;
                }

                // one delete however much is cached, but still an sd write not to
                // wait on from the main thread.
                App::Push<ProgressBox>(0, "Clear cache"_i18n, "Ownfoil"_i18n, [](ProgressBox*) -> Result {
                    const auto rc = fs::FsNativeSd().DeleteDirectoryRecursively(CACHE_PATH);
                    // nothing cached yet is already clear.
                    R_UNLESS(R_SUCCEEDED(rc) || rc == FsError_PathNotFound, rc);
                    R_SUCCEED();
                }, [](Result rc) {
                    if (R_SUCCEEDED(App::PushErrorBox(rc, "Failed to clear the cache."_i18n))) {
                        App::Notify("Cache cleared."_i18n);
                    }
                });
            }
        );
    }, true, "Remove all cached shop artwork from the SD card."_i18n);
}

void Menu::OpenSelected() {
    if (m_entry_index < 0 || m_entry_index >= static_cast<s64>(m_entries.size())) {
        return;
    }

    using AppType = sphaira::ownfoil::api::AppType;
    const auto& app = m_entries[m_entry_index].app;

    // an update has no page of its own, so its card opens its game's - which is
    // whose name, publisher and artwork the card already carries.
    TitlePage page{};
    page.dlc = app.type == AppType::Dlc;
    page.id = page.dlc ? app.app_id : app.title_id;
    page.game_id = app.title_id;
    page.name = app.name;
    page.publisher = app.publisher;
    page.game_name = app.game_name;
    page.banner = app.banner;
    page.icon = app.icon;
    page.download = app.download;

    App::Push<TitleMenu>(m_config, m_base_url, page);
}

void SignalInstalled() {
    g_installs++;
}

} // namespace sphaira::ui::menu::ownfoil
