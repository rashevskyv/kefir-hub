#include "ui/menus/ownfoil_title_internal.hpp"

namespace sphaira::ui::menu::ownfoil {

void TitleMenu::UpdateActions() {
    const auto focused = m_focus >= 0;

    // A follows what is on screen: a highlighted row opens that dlc's page, and
    // anything else the install menu, which waits on the shop's answer.
    const auto action = focused ? MainAction::View : (m_loaded ? MainAction::Install : MainAction::None);
    if (action != m_action) {
        m_action = action;
        switch (action) {
            case MainAction::None:
                RemoveAction(Button::A);
                break;

            case MainAction::Install:
                SetAction(Button::A, Action{"Install"_i18n, [this]{
                    ShowInstallOptions();
                }});
                break;

            case MainAction::View:
                SetAction(Button::A, Action{"Open"_i18n, [this]{
                    OpenDlc(m_focus);
                }});
                break;
        }
    }

    // the viewer needs the full-size copies, which only the shop's answer names.
    const auto images = m_loaded && GetImageCount() > 0 && ImagesInView() && !focused;
    if (images != HasAction(Button::Y)) {
        if (images) {
            SetAction(Button::Y, Action{"Full screen"_i18n, [this]{
                OpenViewer();
            }});
        } else {
            RemoveAction(Button::Y);
        }
    }

    // the counter is for whatever left and right, or up and down, step through.
    char counter[32]{};
    if (focused && m_rows.size() > 1) {
        std::snprintf(counter, sizeof(counter), "%ld / %zu", m_focus + 1, m_rows.size());
    } else if (!focused && ImagesInView() && GetImageCount() > 1) {
        std::snprintf(counter, sizeof(counter), "%ld / %ld", m_image_index + 1, GetImageCount());
    }

    if (m_counter != counter) {
        m_counter = counter;
        SetSubHeading(m_counter);
    }
}

void TitleMenu::ShowInstallOptions() {
    auto options = std::make_unique<Sidebar>("Install"_i18n, GetName(), Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>("Install options"_i18n, [](){
        App::DisplayInstallOptions(false);
    });

    // Install names what it would do and the rows above change that, but they are
    // added first, so they reach it through this, filled in once it exists.
    const auto install = std::make_shared<SidebarEntryCallback*>();
    const auto relabel = [this, install](){
        if (*install) {
            (*install)->SetTitle(GetInstallLabel());
        }
    };

    // the shop answers with no apps at all under a dlc's own id, so a dlc's page
    // has nothing to choose between: it installs the one thing the shop holds.
    if (!m_title.versions.empty()) {
        SidebarEntryArray::Items items;
        for (const auto& version : m_title.versions) {
            auto label = FormatInstallVersion(version);
            // the one the console holds: picking it installs nothing.
            if (m_console_loaded && m_installed.installed && version.version == m_installed.version) {
                label += " • " + "Installed"_i18n;
            }
            items.emplace_back(std::move(label));
        }

        const auto picker = options->Add<SidebarEntryArray>("Version"_i18n, items, [this, relabel](s64& index){
            m_install_version = index;
            relabel();
        }, m_install_version);

        // asked as the list is drawn, so turning Allow downgrade on under Install
        // options lights the older updates up the next time the list is opened.
        picker->SetDisabled([this](s64 index){
            return index >= 0 && index < static_cast<s64>(m_title.versions.size()) && !IsVersionOffered(m_title.versions[index].version);
        });
    }

    // every add-on the console hasn't got starts ticked, which is what a shop is
    // opened for; after that they are the user's to choose. the console can answer
    // after the menu was first opened, so the installed half is cleared each time
    // and only the defaults are settled once.
    if (!m_rows.empty()) {
        const auto first = m_install_dlc.size() != m_rows.size();
        m_install_dlc.resize(m_rows.size());
        for (size_t i = 0; i < m_rows.size(); i++) {
            if (m_rows[i].installed) {
                m_install_dlc[i] = 0;
            } else if (first) {
                m_install_dlc[i] = 1;
            }
        }

        const auto addons = options->Add<SidebarEntryTextBase>("Add-on content"_i18n, GetAddonCount(), SidebarEntryTextBase::Callback{});
        addons->SetCallback([this, addons, relabel](){
            ShowAddons([this, addons, relabel](){
                addons->SetValue(GetAddonCount());
                relabel();
            });
        });
    }

    *install = options->Add<SidebarEntryCallback>(GetInstallLabel(), [this](){
        Install();
    }, "Download and install selection."_i18n);

    // asked as the menu draws, so the version, the add-ons and Allow downgrade all
    // move it. the reason is set once, so it holds for every version it greys on.
    (*install)->Depends([this](){
        return HasSomethingToInstall();
    }, m_page.dlc
        ? "This add-on is already installed."_i18n
        : "Nothing to install: the console already has this version or a newer one, and no add-on is ticked."_i18n);

    // the menu opens on Install, which is what it is opened for: everything
    // above it only qualifies that press.
    options->SetDefaultEntry(*install);
}

void TitleMenu::Install() {
    if (!App::GetInstallEnable()) {
        App::ShowEnableInstallPrompt();
        return;
    }

    // one file can carry several of the chosen apps, so they are gathered by
    // download: each file is fetched once and installs only what was chosen of it.
    struct Target {
        api::ShopDownload download{};
        std::vector<u64> ids{};
    };
    std::vector<Target> targets;
    bool missing{};

    const auto add = [&targets, &missing](const api::ShopDownload& download, u64 id) {
        // the download serves no file name, so without the extension there is
        // no telling which container to read.
        if (download.url.empty() || download.extension.empty()) {
            missing = true;
            return;
        }

        auto it = std::find_if(targets.begin(), targets.end(), [&download](const auto& e) {
            return e.download.url == download.url;
        });
        if (it == targets.end()) {
            it = targets.emplace(targets.end(), Target{download});
        }
        it->ids.emplace_back(id);
    };

    if (m_page.dlc) {
        add(m_page.download, ParseId(m_page.id));
    } else {
        if (m_install_version >= 0 && m_install_version < static_cast<s64>(m_title.versions.size())) {
            const auto game_id = ParseId(m_page.game_id);
            const auto& chosen = m_title.versions[m_install_version];

            // a game the console hasn't got needs its base game under any update.
            const auto& base = m_title.versions.front();
            if (!m_installed.installed && base.version == 0) {
                add(base.download, game_id);
            }

            // an update's id is its game's with 0x800 set. the installed version
            // puts nothing new on the console: chosen, it installs the add-ons alone.
            if (chosen.version > 0 && !(m_installed.installed && chosen.version == m_installed.version)) {
                add(chosen.download, game_id ^ 0x800);
            }
        }

        for (size_t i = 0; i < m_install_dlc.size() && i < m_title.dlc.size(); i++) {
            if (m_install_dlc[i]) {
                add(m_title.dlc[i].download, ParseId(m_title.dlc[i].app_id));
            }
        }
    }

    if (missing) {
        App::Notify(GetName() + ": " + "The shop didn't say how to download this"_i18n);
        return;
    }

    if (targets.empty()) {
        return;
    }

    App::PopToMenu();
    App::Push<ProgressBox>(0, "Installing "_i18n, GetName(), [config = m_config, targets](ProgressBox* pbox) -> Result {
        for (const auto& target : targets) {
            yati::source::Http source{target.download.url, config.user, config.pass};

            yati::ConfigOverride config_override{};
            config_override.title_ids = target.ids;

            // yati picks the container by the path's extension.
            const auto path = "download." + target.download.extension;
            R_TRY(yati::InstallFromSource(pbox, &source, path, config_override));
        }

        R_SUCCEED();
    }, [this](Result rc) {
        App::PushErrorBox(rc, "Install failed!"_i18n);
        if (R_SUCCEEDED(rc)) {
            App::Notify(i18n::Reorder("Installed ", GetName()));
        }

        // even a failed install can have put some of it on the console, so the
        // page and the catalog behind it ask the console again either way.
        SignalInstalled();
        const auto token = m_stop.get_token();
        m_console = std::make_unique<utils::Async>([this, token](){
            LoadInstalled(token);
        });
    }, ProgressBoxOption::ScreenToggle);
}

void TitleMenu::ShowAddons(const std::function<void()>& changed) {
    PopupMultiSelect::Items items;
    for (const auto& row : m_rows) {
        // the name the page's own row carries, and the console's answer about it:
        // one it already holds is listed, greyed out, rather than offered.
        items.emplace_back(PopupMultiSelect::Item{row.name, row.installed ? "Installed"_i18n : std::string{}, row.installed});
    }

    // the popup ticks the page's own vector in place and calls back on every
    // change, so the row behind it keeps its count while the popup is still up.
    App::Push<PopupMultiSelect>("Add-on content"_i18n, items, m_install_dlc, changed);
}

auto TitleMenu::GetInstallLabel() const -> std::string {
    // a dlc, an add-on, or a game the console hasn't got is always an install.
    const auto ticked = std::find(m_install_dlc.begin(), m_install_dlc.end(), 1) != m_install_dlc.end();
    if (m_page.dlc || ticked || !m_installed.installed || m_install_version < 0 || m_install_version >= static_cast<s64>(m_title.versions.size())) {
        return "Install"_i18n;
    }

    // the chosen version alone. the installed one installs nothing, and is left
    // called Install for the greyed-out entry to explain.
    const auto chosen = m_title.versions[m_install_version].version;
    if (chosen > m_installed.version) {
        return "Update"_i18n;
    }
    if (chosen < m_installed.version) {
        return "Downgrade"_i18n;
    }
    return "Install"_i18n;
}

auto TitleMenu::HasSomethingToInstall() const -> bool {
    // a dlc's page installs the one thing: the dlc.
    if (m_page.dlc) {
        return !m_dlc_installed;
    }

    // installed add-ons are never ticked, so any tick is something new.
    if (std::find(m_install_dlc.begin(), m_install_dlc.end(), 1) != m_install_dlc.end()) {
        return true;
    }

    if (m_install_version < 0 || m_install_version >= static_cast<s64>(m_title.versions.size())) {
        return false;
    }

    if (!m_installed.installed) {
        return true;
    }

    // the installed version is offered - it is what installs add-ons alone -
    // but puts nothing new on the console by itself.
    const auto chosen = m_title.versions[m_install_version].version;
    return chosen != m_installed.version && IsVersionOffered(chosen);
}

auto TitleMenu::IsVersionOffered(s64 version) const -> bool {
    // a game the console hasn't got - or hasn't answered for yet - takes any
    // version, and so does any version from the installed one up.
    if (!m_installed.installed || version >= m_installed.version) {
        return true;
    }

    // yati only treats an older *update* as a downgrade; the base game under an
    // installed update is skipped outright by "Skip if already installed".
    return version > 0 && App::GetApp()->m_allow_downgrade.Get();
}

auto TitleMenu::GetAddonCount() const -> std::string {
    const auto selected = std::count(m_install_dlc.begin(), m_install_dlc.end(), 1);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zu / %zu", static_cast<size_t>(selected), m_rows.size());
    return buf;
}

void TitleMenu::OpenDlc(s64 index) {
    if (index < 0 || index >= static_cast<s64>(m_title.dlc.size())) {
        return;
    }
    const auto& dlc = m_title.dlc[index];

    // no card stands behind this page, so it opens on what the row knows and
    // fills in the rest when the shop answers.
    TitlePage page{};
    page.id = dlc.app_id;
    page.dlc = true;
    page.game_id = m_page.game_id;
    page.name = dlc.name;
    page.publisher = GetPublisher();
    page.game_name = GetName();
    page.banner = dlc.banner;
    page.icon = m_page.icon;
    page.download = dlc.download;

    App::Push<TitleMenu>(m_config, m_base_url, page);
}

void TitleMenu::OpenViewer() {
    // the same images the page steps through, in the same order, each at full
    // size where the shop keeps a bigger copy and as the page has it where not.
    std::vector<api::ShopImage> images;
    std::vector<int> previews;

    if (!GetBanner().url.empty()) {
        images.emplace_back(m_title.full_banner.url.empty() ? GetBanner() : m_title.full_banner);
        previews.emplace_back(m_banner);
    }

    for (size_t i = 0; i < m_screenshots.size() && i < m_title.screenshots.size(); i++) {
        const auto full = i < m_title.full_screenshots.size() ? m_title.full_screenshots[i] : api::ShopImage{};
        images.emplace_back(full.url.empty() ? m_title.screenshots[i] : full);
        previews.emplace_back(m_screenshots[i]);
    }

    if (images.empty()) {
        return;
    }

    App::Push<ScreenshotViewer>(m_config, m_page.id, std::move(images), std::move(previews), m_image_index);
}

} // namespace sphaira::ui::menu::ownfoil
