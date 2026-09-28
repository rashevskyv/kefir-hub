#include "ui/menus/ownfoil_internal.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace sphaira::ui::menu::ownfoil {

std::atomic<u32> g_installs{};

Menu::LazyImage::LazyImage(LazyImage&& rhs) noexcept {
    *this = std::move(rhs);
}

auto Menu::LazyImage::operator=(LazyImage&& rhs) noexcept -> LazyImage& {
    std::swap(image, rhs.image);
    std::swap(tried_cache, rhs.tried_cache);
    std::swap(cached, rhs.cached);
    std::swap(state, rhs.state);
    return *this;
}

Menu::LazyImage::~LazyImage() {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

Menu::Menu(u32 flags) : grid::Menu{"Ownfoil"_i18n, flags} {
    // B leaves the menu from the catalog too: the server list is reached from
    // the catalog's options, not by backing out.
    SetAction(Button::B, Action{"Back"_i18n, [this]{
        // backing out of a connect leaves the list up, not the menu.
        if (m_connecting) {
            m_connect_stop.request_stop();
            m_connecting = false;
            return;
        }
        SetPop();
    }});

    SetAction(Button::A, Action{"Select"_i18n, [this]{
        if (m_mode == Mode::Home) {
            OpenSelected();
            return;
        }

        if (m_mode != Mode::ServerList || m_connecting) {
            return;
        }

        if (m_focus_discover) {
            StartDiscovery();
        } else {
            OnServerSelected(m_index);
        }
    }});

    SetAction(Button::X, Action{"Options"_i18n, [this]{
        if (m_connecting) {
            return;
        }
        ShowOptions();
    }});

    m_list = std::make_unique<List>(1, 4, LIST_POS, LIST_ITEM, LIST_PAD);

    if (m_view.Get() < 0 || m_view.Get() >= ViewType_Count) {
        m_view.Set(ViewType_Icon);
    }

    // a hand-edited ini must not leave the sidebar pointing past the list, nor
    // BuildQuery indexing past SORT_MODES.
    if (m_sort.Get() < 0 || m_sort.Get() >= static_cast<s64>(std::size(SORT_MODES))) {
        m_sort.Set(0);
    }

    SetMode(Mode::ServerList);
}

Menu::~Menu() {
    // ~Async blocks on the worker and runs before the base Object dtor that would
    // cancel it, so the ~1.5s discovery window is cut short here rather than
    // waited out; m_page_stop drops the artwork still queued for the last page.
    m_stop_source.request_stop();
    m_page_stop.request_stop();
    m_connect_stop.request_stop();
}

void Menu::SetMode(Mode mode) {
    const auto was_home = m_mode == Mode::Home;
    m_mode = mode;

    if (mode == Mode::Home) {
        // browsing a shop, the header names it rather than the menu.
        SetTitle(m_config.name);

        // '-' and Y open MainMenu's options and menu picker while this menu is a
        // tab, so remember whatever held them to hand back.
        if (!was_home) {
            m_prev_select = FindAction(Button::SELECT);
            m_prev_y = FindAction(Button::Y);
        }
        SetAction(Button::Y, Action{GetCategoryActionHint(), [this]{
            ShowCategories();
        }});
        SetAction(Button::SELECT, Action{GetViewActionHint(), [this]{
            CycleView();
        }});
        // no hint on ZL: the bar draws it beside ZR, in front of the one hint.
        SetAction(Button::L2, Action{[this]{
            if (m_page > 0) {
                LoadPage(m_page - 1);
            }
        }});
        SetAction(Button::R2, Action{"Page"_i18n, [this]{
            if (m_page + 1 < GetPageMax()) {
                LoadPage(m_page + 1);
            }
        }});
        OnViewChange();
    } else {
        if (was_home) {
            FreeEntries();
            m_pending_page.reset();
            RestoreAction(Button::SELECT, m_prev_select);
            RestoreAction(Button::Y, m_prev_y);
            RemoveAction(Button::L2);
            RemoveAction(Button::R2);
            SetTitle("Ownfoil"_i18n);
            SetTitleSubHeading("");
            SetSubHeading("");
            m_list = std::make_unique<List>(1, 4, LIST_POS, LIST_ITEM, LIST_PAD);
        }
    }
}

auto Menu::FindAction(Button button) const -> std::optional<Action> {
    if (const auto it = m_actions.find(button); it != m_actions.end()) {
        return it->second;
    }
    return {};
}

void Menu::RestoreAction(Button button, std::optional<Action>& prev) {
    if (prev) {
        SetAction(button, *prev);
    } else {
        RemoveAction(button);
    }
    prev.reset();
}

void Menu::FreeEntries() {
    // more than tidiness: a queued download returns at once on a stopped token,
    // which hands the four download threads to the page arriving rather than
    // making it wait out a screenful of artwork nobody will see.
    m_page_stop.request_stop();
    m_page_stop = std::stop_source{};
    m_loading = false;
    m_entries.clear();
    m_entry_index = 0;
}

void Menu::CycleView() {
    m_view.Set((m_view.Get() + 1) % ViewType_Count);
    OnViewChange();
}

void Menu::OnViewChange() {
    m_entry_index = 0;
    m_banner_name.Reset();
    m_banner_publisher.Reset();

    switch (m_view.Get()) {
        case ViewType_Icon:
            grid::Menu::OnLayoutChange(m_list, grid::LayoutType_Grid);
            break;

        case ViewType_Detail:
            grid::Menu::OnLayoutChange(m_list, grid::LayoutType_GridDetail);
            break;

        // grid::Menu knows nothing of this one, so it is built here.
        case ViewType_Banner:
            m_list = std::make_unique<List>(3, 6, m_pos, BANNER_ITEM, BANNER_PAD);
            break;
    }

    // each view hands back a fresh List, which claims L2/R2 to scroll its own
    // rows - here those are the shop's pages, so the press would do both.
    m_list->SetPageJump(false);
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    // an install changes what the console-crossed categories list, so the page
    // on screen is asked for again.
    if (m_mode == Mode::Home && m_installs_shown != g_installs) {
        m_installs_shown = g_installs;
        const auto category = CATEGORIES[m_category].category;
        if (category != sphaira::ownfoil::api::Category::All && category != sphaira::ownfoil::api::Category::Search) {
            LoadPage(m_page);
        }
    }

    // MainMenu copies its own actions onto the current tab after OnFocusGained,
    // so '-' and Y have to be reclaimed every frame the catalog owns them.
    if (m_mode == Mode::Home) {
        const auto it = m_actions.find(Button::SELECT);
        if (it == m_actions.end() || it->second.m_hint != GetViewActionHint()) {
            SetAction(Button::SELECT, Action{GetViewActionHint(), [this]{
                CycleView();
            }});
        }

        const auto y = m_actions.find(Button::Y);
        if (y == m_actions.end() || y->second.m_hint != GetCategoryActionHint()) {
            SetAction(Button::Y, Action{GetCategoryActionHint(), [this]{
                ShowCategories();
            }});
        }
    }

    MenuBase::Update(controller, touch);

    if (m_mode == Mode::Home) {
        if (!m_entries.empty()) {
            m_list->OnUpdate(controller, touch, m_entry_index, m_entries.size(), [this](bool is_touch, s64 i) {
                // a tap on the highlighted card opens it, any other only moves
                // the highlight.
                if (is_touch && m_entry_index == i) {
                    FireAction(Button::A);
                } else {
                    m_entry_index = i;
                }
            });
        }
        return;
    }

    if (m_mode != Mode::ServerList || m_connecting) {
        return;
    }

    // taken before the list, whose touch bounds are the whole menu body.
    if (touch->is_clicked && touch->in_range(DISCOVER_BUTTON)) {
        m_focus_discover = true;
        FireAction(Button::A);
        return;
    }

    // with nothing in the list, the button is the only thing there is to focus.
    if (m_candidates.empty()) {
        return;
    }

    if (m_focus_discover) {
        if (controller->GotDown(Button::UP)) {
            App::PlaySoundEffect(SoundEffect::Focus);
            m_focus_discover = false;
            return;
        }

        // the list is kept out of the d-pad's way while the button has focus but
        // still sees touch, so a tap can pull focus back onto a row.
        if (!touch->is_clicked && !touch->is_scroll && !touch->is_end) {
            return;
        }
    } else if (controller->GotDown(Button::DOWN) && m_index == static_cast<s64>(m_candidates.size()) - 1) {
        App::PlaySoundEffect(SoundEffect::Focus);
        m_focus_discover = true;
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, m_candidates.size(), [this](bool is_touch, s64 i) {
        // a tap on the already-selected row selects it, unless that row didn't
        // have focus to begin with - then the tap only takes it back.
        if (is_touch && !m_focus_discover && m_index == i) {
            FireAction(Button::A);
        } else {
            m_focus_discover = false;
            m_index = i;
        }
    });
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto pdata = GetPolledData();
    if (!pdata.ip) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", "Network access required"_i18n.c_str());
        return;
    }

    if (m_mode == Mode::Home) {
        DrawHome(vg, theme);
        return;
    }

    if (m_connecting) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, LIST_POS.y + LIST_POS.h / 2.f, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s",
            (i18n::Reorder("Connecting to ", m_connecting_name) + "...").c_str());
        return;
    }

    if (m_candidates.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, LIST_POS.y + LIST_POS.h / 2.f, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s",
            "No Ownfoil servers saved.\n\nPress X to add one by hand, or search the network below."_i18n.c_str());
    } else {
        m_list->Draw(vg, theme, m_candidates.size(), [this](NVGcontext* vg, Theme* theme, const Vec4& v, s64 index) {
            const auto& [x, y, w, h] = v;
            const auto& candidate = m_candidates[index];
            const auto& config = candidate.config;
            const auto selected = !m_focus_discover && index == m_index;

            if (selected) {
                gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND));
            }

            const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
            const auto info_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;

            // only what is known: a version comes from a scan, and an entry can
            // have either address or both.
            std::string sub{};
            const auto append = [&sub](const std::string& part) {
                if (!sub.empty()) {
                    sub += SEPARATOR;
                }
                sub += part;
            };
            if (!candidate.version.empty()) {
                append("v" + candidate.version);
            }
            if (!config.local_address.empty()) {
                append("Local"_i18n + " (" + config.local_address + ")");
            }
            if (!config.remote_address.empty()) {
                append("Remote"_i18n + " (" + config.remote_address + ")");
            }

            gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f - 16.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", config.name.c_str());
            gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f + 16.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(info_id), "%s", sub.c_str());

            // anything the list shows that isn't saved yet came out of a scan.
            if (!candidate.saved) {
                const Vec4 tag{x + w - 90.f, y + h / 2.f - 14.f, 70.f, 28.f};
                gfx::drawRect(vg, tag, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 5.f);
                gfx::drawTextArgs(vg, tag.x + tag.w / 2.f, tag.y + tag.h / 2.f, 18.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_BACKGROUND), "%s", "New"_i18n.c_str());
            }
        });
    }

    DrawDiscoverButton(vg, theme);
}

void Menu::DrawHome(NVGcontext* vg, Theme* theme) {
    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 30.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", "Empty..."_i18n.c_str());
        return;
    }

    // capped so a page of fresh artwork doesn't stall the frame on io or gpu.
    int budget = 2;
    const auto view = m_view.Get();

    m_list->Draw(vg, theme, m_entries.size(), [this, view, &budget](NVGcontext* vg, Theme* theme, const Vec4& v, s64 index) {
        const auto& [x, y, w, h] = v;
        auto& e = m_entries[index];
        const auto selected = index == m_entry_index;

        if (view == ViewType_Banner) {
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }

            const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
            const auto info_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;
            const auto xoff = (BANNER_ITEM.w - BANNER_W) / 2.f;
            const auto image = GetImage(e, true, budget);

            gfx::drawImage(vg, x + xoff, y, BANNER_W, BANNER_H, image ? image : App::GetDefaultImage(), 5.f);
            m_banner_name.Draw(vg, selected, x + xoff, y + BANNER_H + 20.f, w - 30.f - xoff, 18.f, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.app.name.c_str());
            m_banner_publisher.Draw(vg, selected, x + xoff, y + BANNER_H + 55.f, w - 30.f - xoff, 18.f, NVG_ALIGN_LEFT, theme->GetColour(info_id), e.app.publisher.c_str());
            return;
        }

        const auto layout = view == ViewType_Detail ? grid::LayoutType_GridDetail : grid::LayoutType_Grid;
        DrawEntry(vg, theme, layout, v, selected, GetImage(e, false, budget), e.app.name.c_str(), e.app.publisher.c_str(), e.app.version.c_str());
    });
}

auto Menu::GetImage(Entry& e, bool banner, int& budget) -> int {
    auto& image = banner ? e.banner : e.icon;
    const auto& src = banner ? e.app.banner : e.app.icon;
    const auto& url = src.url;

    if (image.image) {
        return image.image;
    }

    // a title the catalog has no artwork for is never going to gain any.
    if (url.empty()) {
        image.state = ImageState::Failed;
        return App::GetDefaultImage();
    }

    if (image.state == ImageState::Failed) {
        return App::GetDefaultImage();
    }

    // this runs per visible entry per frame, so bail before hashing a cache path
    // that won't be used.
    if (image.state == ImageState::Progress) {
        return 0;
    }

    if (budget <= 0) {
        return 0;
    }

    const auto path = BuildImageCache(e.app.app_id, url, banner ? "banner" : "icon");

    const auto load = [&]() -> bool {
        const auto data = ImageLoadFromFile(path, ImageFlag_JPEG);
        if (data.data.empty()) {
            return false;
        }
        image.image = nvgCreateImageRGBA(App::GetVg(), data.w, data.h, 0, data.data.data());
        return image.image != 0;
    };

    // whatever is already on the sd card is worth a look before the network.
    if (!image.tried_cache) {
        image.tried_cache = true;
        image.cached = load();
        if (image.cached) {
            // only a real decode is worth charging against the frame's budget.
            budget--;
            return image.image;
        }
    }

    switch (image.state) {
        case ImageState::None: {
            image.state = ImageState::Progress;
            const auto index = &e - m_entries.data();

            // the shop gates its own artwork like its catalogue, so its copies
            // need credentials; a hotlink points at a cdn that must not get them.
            const auto user = src.local ? m_config.user : "";
            const auto pass = src.local ? m_config.pass : "";

            const auto queued = curl::Api().ToFileAsync(
                curl::Url{url},
                curl::Path{path},
                curl::UserPass{user, pass},
                curl::PreemptiveAuth{src.local},
                curl::Flags{curl::Flag_Cache},
                curl::StopToken{m_page_stop.get_token()},
                curl::OnComplete{[this, index, banner](auto& result) {
                    // entries never move while a page is up, and leaving one stops
                    // the token this carries, so the index names the same title.
                    auto& entry = m_entries[index];
                    auto& done = banner ? entry.banner : entry.icon;
                    done.state = result.success ? ImageState::Done : ImageState::Failed;
                }}
            );

            if (!queued) {
                image.state = ImageState::Failed;
            }
        }   break;

        case ImageState::Done: {
            if (load()) {
                budget--;
            } else {
                image.state = ImageState::Failed;
            }
        }   break;

        case ImageState::Progress:
        case ImageState::Failed:
            break;
    }

    return image.image;
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    // this runs back from every popup the menu pushes, so the list is not
    // reloaded here: every path that writes an entry calls ReloadSaved itself.
    if (m_did_initial_setup) {
        return;
    }
    m_did_initial_setup = true;
    ReloadSaved();

    if (!GetPolledData().ip) {
        return;
    }

    // a single saved server needs no picking; the list stays one X press away.
    if (m_saved.size() == 1) {
        ConnectTo(m_saved[0]);
    }
}

} // namespace sphaira::ui::menu::ownfoil
