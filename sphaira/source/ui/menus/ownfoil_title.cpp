#include "ui/menus/ownfoil_title_internal.hpp"

namespace sphaira::ui::menu::ownfoil {

auto BuildImageCache(const std::string& app_id, const std::string& url, const char* kind) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s_%s_%016lx.jpg", CACHE_PATH, app_id.c_str(), kind, std::hash<std::string>{}(url));
    return path;
}

TitleMenu::TitleMenu(const sphaira::ownfoil::Config& config, const std::string& base_url, const TitlePage& page)
: MenuBase{config.name, MenuFlag_None}
, m_config{config}
, m_base_url{base_url}
, m_page{page} {
    // the heading row names the shop, as the catalog's does. the title's own
    // name heads the right column instead: a long one scrolled in the heading.
    SetAction(Button::B, Action{"Back"_i18n, [this]{
        SetPop();
    }});

    Layout();

    // pushed out of sight: the catalog stays on screen until the page can open
    // whole, which Update decides.
    SetHidden(true);

    const auto token = m_stop.get_token();
    // everything the page's threads find is only put on the page from the main
    // thread, which is the one nanovg belongs to.
    const auto send = [token](std::function<void()>&& callback) {
        evman::push(evman::CallbackEventData{std::move(callback), token}, false);
    };
    // the log says how long after opening each part landed.
    const auto opened = m_opened;
    // a card with no banner opens a page with none either, which opens on the
    // card's icon instead.
    const auto icon_for_banner = m_page.banner.url.empty();

    // owned by the page rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_loader = std::make_unique<utils::Async>([this, token, send, opened, icon_for_banner](){
        api::ShopTitle title{};
        std::string error;
        if (!api::FetchTitle(m_base_url, m_config, token, m_page.id, title, error)) {
            if (!token.stop_requested()) {
                send([this, error]() {
                    App::Notify(m_page.name + ": " + error);
                    // nothing more is coming from the shop for the page to wait on.
                    m_failed = true;
                    m_artwork_done = true;
                });
            }
            return;
        }

        log_write("[OWNFOIL] title %s fetched after %zums\n", m_page.id.c_str(), opened.GetMs());

        const auto banner = title.banner.url.empty() ? m_page.banner : title.banner;
        const auto screenshots = title.screenshots;
        const auto dlc = title.dlc;

        send([this, title = std::move(title)]() mutable {
            m_title = std::move(title);
            m_screenshots.resize(m_title.screenshots.size());

            // blank lines after the text would only lengthen the page.
            auto& description = m_title.description;
            while (!description.empty() && IsSpace(description.back())) {
                description.pop_back();
            }
            m_intro = Flatten(m_title.intro);

            m_rows.clear();
            for (const auto& content : m_title.dlc) {
                Row row{};
                row.name = StripGameName(content.name, GetName());
                row.tagline = Flatten(content.intro);
                m_rows.emplace_back(std::move(row));
            }
            MarkInstalled();

            // the newest: what a shop is opened to be asked for.
            m_install_version = std::max<s64>(0, static_cast<s64>(m_title.versions.size()) - 1);

            m_loaded = true;
            BuildFacts();
            SetImageIndex(m_image_index);
            Layout();
        });

        // ahead of the screenshots rather than beside them: fetched with them, an
        // uncached banner took ~150ms to download and ~107ms to decode, twice what
        // it takes alone. sent even when nothing was read, since the page waits.
        if (!banner.url.empty()) {
            auto image = FetchImage(m_config, m_page.id, banner, "banner", token);
            if (!image.data.empty()) {
                log_write("[OWNFOIL] banner for %s decoded after %zums\n", m_page.id.c_str(), opened.GetMs());
            }
            send([this, image = std::move(image)]() {
                if (!image.data.empty()) {
                    m_banner = CreateTexture(image);
                }
                m_artwork_done = true;
            });
        }

        // in order, so the thumbnails fill in left to right. these queue behind
        // the title above, so the slot each one lands in already exists.
        for (size_t i = 0; i < screenshots.size() && !token.stop_requested(); i++) {
            auto image = FetchImage(m_config, m_page.id, screenshots[i], "screenshot", token);
            if (image.data.empty()) {
                continue;
            }

            send([this, i, image = std::move(image)]() {
                if (i < m_screenshots.size() && !m_screenshots[i]) {
                    m_screenshots[i] = CreateTexture(image);
                }
            });
        }

        // last, since the rows are the furthest down the page. cached under the
        // same name a dlc card uses, so one the catalog has shown is read back.
        for (size_t i = 0; i < dlc.size() && !token.stop_requested(); i++) {
            auto image = FetchImage(m_config, dlc[i].app_id, dlc[i].banner, "banner", token);
            if (image.data.empty()) {
                continue;
            }

            send([this, i, image = std::move(image)]() {
                if (i < m_rows.size() && !m_rows[i].image) {
                    m_rows[i].image = CreateTexture(image);
                }
            });
        }
    });

    // the installed lookup mounts the game's control nca, so it runs beside the
    // shop's request rather than after it. the card's icon follows it here.
    m_console = std::make_unique<utils::Async>([this, token, send, icon_for_banner](){
        LoadInstalled(token);

        // a page with no banner opens on the card's icon instead, which the grid
        // has usually fetched. sent even when nothing was read, as above.
        if (icon_for_banner) {
            auto icon = FetchImage(m_config, m_page.id, m_page.icon, "icon", token);
            send([this, icon = std::move(icon)]() {
                if (!icon.data.empty()) {
                    m_icon = CreateTexture(icon);
                }
                m_artwork_done = true;
            });
            return;
        }

        // a dlc names its game beside this icon, which the page doesn't wait for:
        // it can be a download, and on the console one held a page to OPEN_WAIT_MS.
        if (m_page.dlc) {
            if (auto image = FetchImage(m_config, m_page.id, m_page.icon, "icon", token); !image.data.empty()) {
                send([this, image = std::move(image)]() {
                    m_icon = CreateTexture(image);
                });
            }
        }
    });
}

TitleMenu::~TitleMenu() {
    // abandon whatever the threads are still waiting on, then wait for them -
    // before the textures they were filling in go.
    m_stop.request_stop();
    m_loader.reset();
    m_console.reset();

    DeleteTexture(m_banner);
    DeleteTexture(m_icon);
    for (const auto image : m_screenshots) {
        DeleteTexture(image);
    }
    for (const auto& row : m_rows) {
        DeleteTexture(row.image);
    }
}

void TitleMenu::Update(Controller* controller, TouchInfo* touch) {
    // the page opens whole: hidden until the shop, the console and its artwork have
    // answered, or OPEN_WAIT_MS has passed. presses meanwhile are swallowed.
    if (IsHidden()) {
        const auto ready = (m_loaded || m_failed) && m_console_loaded && m_artwork_done;
        if (!ready && m_opened.GetMs() < OPEN_WAIT_MS) {
            return;
        }

        log_write("[OWNFOIL] page for %s shown after %zums%s\n", m_page.id.c_str(), m_opened.GetMs(), ready ? "" : " (waited out)");
        SetHidden(false);
    }

    MenuBase::Update(controller, touch);

    // the right stick moves the page freely. the d-pad and left stick move it a
    // part at a time, onto the rows, and - while the images are up - through them.
    if (controller->GotHeld(Button::RS_DOWN)) {
        m_stick_scroll = true;
        ScrollTo(m_scroll_target + STICK_STEP);
    } else if (controller->GotHeld(Button::RS_UP)) {
        m_stick_scroll = true;
        ScrollTo(m_scroll_target - STICK_STEP);
    } else if (controller->GotDown(Button::DPAD_DOWN | Button::LS_DOWN)) {
        StepDown();
    } else if (controller->GotDown(Button::DPAD_UP | Button::LS_UP)) {
        StepUp();
    } else if (m_focus < 0 && ImagesInView() && controller->GotDown(Button::DPAD_LEFT | Button::LS_LEFT)) {
        if (m_image_index > 0) {
            App::PlaySoundEffect(SoundEffect::Scroll);
            SetImageIndex(m_image_index - 1);
        }
    } else if (m_focus < 0 && ImagesInView() && controller->GotDown(Button::DPAD_RIGHT | Button::LS_RIGHT)) {
        if (m_image_index + 1 < GetImageCount()) {
            App::PlaySoundEffect(SoundEffect::Scroll);
            SetImageIndex(m_image_index + 1);
        }
    } else if (touch->is_scroll && (m_dragging || (touch->initial.x >= PAGE_X && touch->initial.x <= PAGE_X + PAGE_W && touch->initial.y >= CLIP_Y && touch->initial.y <= CLIP_BOTTOM))) {
        // a drag that starts on the page moves it with the finger, the way a
        // list's does: from where it was when the drag began, and without a glide.
        if (!m_dragging) {
            m_dragging = true;
            m_drag_from = m_scroll_target;
        }
        ScrollTo(m_drag_from + static_cast<float>(touch->initial.y) - static_cast<float>(touch->cur.y));
        m_scroll = m_scroll_target;
    } else if (touch->is_clicked && touch->in_range(Vec4{PAGE_X, CLIP_Y, PAGE_W, CLIP_BOTTOM - CLIP_Y})) {
        // a tap on the highlighted row opens it, like A does.
        for (s64 i = 0; i < static_cast<s64>(m_rows.size()); i++) {
            if (touch->in_range(GetRowRect(i))) {
                if (i == m_focus) {
                    OpenDlc(i);
                } else {
                    SetFocus(i);
                }
                break;
            }
        }
    }

    if (!touch->is_scroll) {
        m_dragging = false;
    }

    // glide toward the target, unless touch has it.
    if (m_scroll != m_scroll_target) {
        const auto ease = m_stick_scroll ? STICK_EASE : STEP_EASE;
        m_scroll += (m_scroll_target - m_scroll) * ease;
        if (std::abs(m_scroll_target - m_scroll) < 0.5f) {
            m_scroll = m_scroll_target;
        }
    }

    UpdateActions();
}

auto TitleMenu::GetBanner() const -> const api::ShopImage& {
    return m_title.banner.url.empty() ? m_page.banner : m_title.banner;
}

auto TitleMenu::GetName() const -> const std::string& {
    return m_title.name.empty() ? m_page.name : m_title.name;
}

auto TitleMenu::GetPublisher() const -> const std::string& {
    return m_title.publisher.empty() ? m_page.publisher : m_title.publisher;
}

auto TitleMenu::GetImageCount() const -> s64 {
    return (GetBanner().url.empty() ? 0 : 1) + static_cast<s64>(m_screenshots.size());
}

auto TitleMenu::GetImage(s64 index) const -> int {
    if (!GetBanner().url.empty()) {
        if (index == 0) {
            return m_banner;
        }
        index--;
    }

    if (index < 0 || index >= static_cast<s64>(m_screenshots.size())) {
        return 0;
    }
    return m_screenshots[index];
}

void TitleMenu::SetImageIndex(s64 index) {
    m_image_index = std::clamp<s64>(index, 0, std::max<s64>(0, GetImageCount() - 1));
}

void TitleMenu::BuildFacts() {
    m_facts.clear();

    // a row the shop has nothing for is left out rather than drawn empty.
    const auto add = [this](const char* label, const std::string& value) {
        if (!value.empty()) {
            m_facts.emplace_back(Fact{i18n::get(label), value});
        }
    };

    const auto& t = m_title;

    if (!t.release_date.empty()) {
        add("Release date", FormatDate(t.release_date));
    }
    add("Genre", t.genre);
    add("Players", t.players);
    add("Age rating", FormatRating(t.rating, t.region));

    // the catalogue gives every dlc the same placeholder size, so a dlc page
    // has no size worth drawing; a game it doesn't size is given its file's.
    if (!m_page.dlc) {
        if (t.size > 0) {
            add("Required space", utils::formatSizeStorage(t.size));
        } else if (t.download_size > 0) {
            add("Download size", utils::formatSizeStorage(t.download_size));
        }
    }

    // what the console holds opens the group, ahead of what there is to get. it can
    // answer after the shop, so until it has the row holds its place empty rather
    // than moving the rows under it. a dlc has no version string, only a yes or no.
    std::string held{};
    if (m_console_loaded && m_page.dlc) {
        held = m_dlc_installed ? "Yes"_i18n : std::string{"-"};
    } else if (m_console_loaded) {
        // installed but unreadable is still installed: the patch level stands
        // in for the string rather than the row claiming nothing is there.
        held = "-";
        if (m_installed.installed) {
            held = m_installed.display.empty() ? FormatVersion({}, m_installed.version) : m_installed.display;
        }
    }
    m_facts.emplace_back(Fact{i18n::get(m_page.dlc ? "Installed" : "Installed version"), std::move(held), !m_facts.empty()});

    // the shop holds no base game under a dlc's id, so the rest are a game's alone.
    if (t.available_version >= 0) {
        const auto available = FormatVersion(t.available_display, t.available_version);

        // when the shop is behind, the newest version has no string of its own,
        // so its release date stands in for one.
        auto latest = available;
        if (t.latest_version > t.available_version) {
            latest = FormatVersion(FormatDate(t.latest_date), t.latest_version);
        }

        add("Available version", available);
        add("Latest version", latest);
    }
}

void TitleMenu::LoadInstalled(std::stop_token token) {
    installed::InstalledVersion version{};
    std::vector<std::string> installed_dlc;
    bool dlc_installed{};
    if (m_page.dlc) {
        dlc_installed = installed::HasDlc(m_page.game_id, m_page.id);
    } else {
        version = installed::InstalledVersionOf(m_page.game_id);
        // asked before the shop has said whether there is any dlc: one ns
        // query is cheaper than waiting to find out.
        installed_dlc = installed::InstalledDlcIds(m_page.game_id);
    }
    log_write("[OWNFOIL] console answered for %s after %zums\n", m_page.id.c_str(), m_opened.GetMs());

    evman::push(evman::CallbackEventData{[this, version = std::move(version), installed_dlc = std::move(installed_dlc), dlc_installed]() mutable {
        m_installed = std::move(version);
        m_installed_dlc = std::move(installed_dlc);
        m_dlc_installed = dlc_installed;
        m_console_loaded = true;
        MarkInstalled();

        // until the shop answers there are no facts for it to join.
        if (m_loaded) {
            BuildFacts();
        }
    }, token}, false);
}

void TitleMenu::MarkInstalled() {
    // the rows are built one for one from the title's dlc.
    for (size_t i = 0; i < m_rows.size() && i < m_title.dlc.size(); i++) {
        const auto& id = m_title.dlc[i].app_id;
        m_rows[i].installed = std::find(m_installed_dlc.begin(), m_installed_dlc.end(), id) != m_installed_dlc.end();
    }
}

} // namespace sphaira::ui::menu::ownfoil
