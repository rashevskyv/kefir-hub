#include "ui/menus/ownfoil_internal.hpp"

#include <algorithm>

namespace sphaira::ui::menu::ownfoil {

void Menu::DrawDiscoverButton(NVGcontext* vg, Theme* theme) const {
    gfx::drawRect(vg, DISCOVER_BUTTON, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);

    if (m_focus_discover) {
        gfx::drawRectOutline(vg, theme, 4.f, DISCOVER_BUTTON);
    }

    // greyed out for the duration of the scan: it can't be started twice.
    const auto colour_id = m_discovering ? ThemeEntryID_TEXT_INFO : (m_focus_discover ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
    const auto text = m_discovering ? "Searching the network..."_i18n : "Discover local servers"_i18n;

    gfx::drawTextArgs(vg, DISCOVER_BUTTON.x + DISCOVER_BUTTON.w / 2.f, DISCOVER_BUTTON.y + DISCOVER_BUTTON.h / 2.f, 22.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(colour_id), "%s", text.c_str());
}

void Menu::ReloadSaved() {
    sphaira::ownfoil::LoadConfigs(m_saved);
    RebuildCandidates();
}

void Menu::StartDiscovery() {
    if (m_discovering) {
        return;
    }

    if (!GetPolledData().ip) {
        return;
    }

    m_discovering = true;

    // owned by the menu rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_discovery = std::make_unique<utils::Async>([this](){
        // qualified from the root: this file's own namespace otherwise wins.
        auto found = sphaira::ownfoil::discovery::Discover(this->GetToken());

        evman::push(evman::CallbackEventData{
            [this, found = std::move(found)]() mutable {
                m_discovered = std::move(found);
                m_discovering = false;
                RebuildCandidates();
            },
            this->GetToken()
        }, false);
    });
}

void Menu::RebuildCandidates() {
    m_candidates.clear();

    for (const auto& config : m_saved) {
        Candidate candidate{};
        candidate.config = config;
        candidate.saved = true;

        // the case the handshake can't cover: a local-only shop whose ip moved is
        // unreachable, so no handshake ever happens to correct the entry.
        // discovery's fresher address gets the connect through, which saves it.
        if (const auto d = FindDiscovered(config.uid)) {
            candidate.version = d->version;
            candidate.is_public = d->is_public;
            if (!d->local.empty()) {
                candidate.config.local_address = d->local;
            }
            if (!d->remote.empty()) {
                candidate.config.remote_address = d->remote;
            }
        }

        m_candidates.emplace_back(std::move(candidate));
    }

    for (const auto& d : m_discovered) {
        // matched on the server-issued uid alone, so a hand-added entry still
        // holding a placeholder is listed a second time until its first connect.
        if (sphaira::ownfoil::FindByUid(m_saved, d.uid)) {
            continue;
        }

        Candidate candidate{};
        candidate.config.uid = d.uid;
        candidate.config.name = d.name;
        candidate.config.local_address = d.local;
        candidate.config.remote_address = d.remote;
        candidate.version = d.version;
        candidate.is_public = d.is_public;
        m_candidates.emplace_back(std::move(candidate));
    }

    if (m_index >= static_cast<s64>(m_candidates.size())) {
        m_index = 0;
    }

    // an empty list has nothing to focus, so the button is all there is.
    if (m_candidates.empty()) {
        m_focus_discover = true;
    }
}

auto Menu::FindDiscovered(const std::string& uid) const -> const sphaira::ownfoil::discovery::DiscoveredServer* {
    if (uid.empty()) {
        return nullptr;
    }

    const auto it = std::find_if(m_discovered.begin(), m_discovered.end(), [&](const auto& d) {
        return d.uid == uid;
    });
    return it == m_discovered.end() ? nullptr : &*it;
}

void Menu::OnServerSelected(s64 index) {
    if (index < 0 || index >= static_cast<s64>(m_candidates.size())) {
        return;
    }

    const auto candidate = m_candidates[index];
    const auto config = candidate.config;

    // already a saved location: connect straight away.
    if (candidate.saved) {
        ConnectTo(config);
        return;
    }

    // a discovered server not saved yet: say whether it needs a login before
    // asking whether to set one up.
    const auto message = candidate.is_public
        ? "This shop is public. Set up a login anyway?"_i18n
        : "This shop is private. Set up a login now?"_i18n;

    App::Push<OptionBox>(message, "No"_i18n, "Yes"_i18n, 1, [this, config](std::optional<s64> op_index) {
        if (op_index && *op_index) {
            App::Push<sphaira::ownfoil::OwnfoilForm>(config, [this](const sphaira::ownfoil::Config& saved) {
                ReloadSaved();
                ConnectTo(saved);
            });
        } else {
            // SaveConfig assigns the uid, so it needs a mutable copy.
            auto to_save = config;
            sphaira::ownfoil::SaveConfig(to_save);
            ReloadSaved();
            ConnectTo(to_save);
        }
    });
}

void Menu::ConnectTo(const sphaira::ownfoil::Config& config) {
    if (m_connecting) {
        return;
    }

    // opening a shop asks what it can add to this console, so that is what it
    // opens on rather than wherever the last one was left.
    m_category = 0;
    m_search.clear();
    m_installs_shown = g_installs;

    // read here rather than on the worker: option::Get() fills its cache on the
    // first call, which has no business happening off the main thread.
    const auto query = BuildQuery(0);

    // B cancels this connect alone, and its result is dropped with it. a
    // cancelled worker may still be unwinding when the next connect replaces it,
    // which joins it, but curl notices a stop within a progress tick.
    m_connect_stop = {};
    const auto token = m_connect_stop.get_token();

    m_connecting = true;
    m_connecting_name = config.name;

    // owned by the menu rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_connect_async = std::make_unique<utils::Async>([this, config, query, token](){
        auto connect_result = sphaira::ownfoil::api::Connect(config, token);
        auto connected_config = config;
        bool saved_changed{};
        std::vector<sphaira::ownfoil::api::ShopApp> fetched;
        std::string fetch_error;
        s64 total{};
        std::string base_url;

        if (connect_result.success) {
            // the server owns its identity and its name; empty values are
            // ignored, so a server reporting none can't erase what discovery
            // gave.
            const auto& info = connect_result.info;
            if (!info.uid.empty()) {
                connected_config.uid = info.uid;
            }
            if (!info.name.empty()) {
                connected_config.name = info.name;
            }
            // no local address: that is discovery's alone (RebuildCandidates).
            if (!info.remote_address.empty()) {
                connected_config.remote_address = info.remote_address;
            }
            // so the next launch goes straight to the address that just worked.
            connected_config.resolved_url = connect_result.base_url;

            // written here, on the worker: minIni rewrites the whole file once
            // per key, so saving an entry is half a dozen SD writes and they
            // have no business landing on the main thread as the grid appears.
            if (connected_config.uid != config.uid || connected_config.name != config.name
                || connected_config.remote_address != config.remote_address
                || connected_config.resolved_url != config.resolved_url) {
                // the old uid re-keys the entry: a placeholder promoted on first
                // contact, or an entry re-pointed at a different server.
                sphaira::ownfoil::SaveConfig(connected_config, config.uid);
                saved_changed = true;
            }

            // the first page rides the same worker, so the grid appears full.
            if (info.features.shop) {
                base_url = connect_result.base_url;
                // made here rather than in the constructor, which runs at startup
                // for every user whether or not they ever open this tab.
                fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);
                auto page_query = query;
                if (PrepareQuery(page_query, base_url, config, token, fetch_error)) {
                    sphaira::ownfoil::api::FetchApps(base_url, config, token, page_query, fetched, total, fetch_error);
                }
            }
        }

        evman::push(evman::CallbackEventData{
            [this, config, connect_result = std::move(connect_result), connected_config = std::move(connected_config),
                saved_changed, fetched = std::move(fetched), fetch_error = std::move(fetch_error), total, base_url = std::move(base_url)]() mutable {
                m_connecting = false;

                if (!connect_result.success) {
                    App::Notify(config.name + ": " + connect_result.error);
                    return;
                }

                const auto& info = connect_result.info;
                if (!info.motd.empty()) {
                    App::Notify(info.name + ": " + info.motd);
                }
                App::Notify(info.name + (connect_result.used_remote ? ": connected via remote address"_i18n : ": connected via local network"_i18n));

                // the entry on disk moved under the list, so pick the change up.
                if (saved_changed) {
                    ReloadSaved();
                }

                m_config = connected_config;

                // the handshake says what this caller may actually do; without
                // shop access there is no catalog to show.
                if (!info.features.shop) {
                    App::Notify(info.name + ": " + "This account has no shop access"_i18n);
                    return;
                }

                if (!fetch_error.empty()) {
                    App::Notify(info.name + ": " + fetch_error);
                    return;
                }

                m_base_url = base_url;
                SetMode(Mode::Home);
                ApplyPage(0, fetched, total);
            },
            token
        }, false);
    });
}

void Menu::EditSelected() {
    // the discover button has focus, or there is nothing saved to edit yet.
    if (m_focus_discover || m_index >= static_cast<s64>(m_candidates.size())) {
        return;
    }

    // a discovered row that was never saved is edited as what it will become: the
    // form writes the entry, addresses and all, on its first save.
    App::Push<sphaira::ownfoil::OwnfoilForm>(m_candidates[m_index].config, [this](const auto&){
        ReloadSaved();
    });
}

void Menu::DeleteSelected() {
    if (m_focus_discover || m_index >= static_cast<s64>(m_candidates.size())) {
        return;
    }

    const auto& candidate = m_candidates[m_index];
    if (!candidate.saved) {
        return;
    }

    const auto uid = candidate.config.uid;
    App::Push<OptionBox>(
        i18n::Reorder("Delete ", candidate.config.name) + '?',
        "No"_i18n, "Yes"_i18n, 0, [this, uid](std::optional<s64> op_index) {
            if (op_index && *op_index) {
                sphaira::ownfoil::DeleteConfig(uid);
                ReloadSaved();
            }
        }
    );
}

} // namespace sphaira::ui::menu::ownfoil
