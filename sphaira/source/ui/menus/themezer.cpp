#include "ui/menus/themezer.hpp"
#include "ui/menus/themezer/themezer_internal.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "title_info.hpp"
#include "nro.hpp"
#include "ui/menus/ghdl.hpp"
#include <minIni.h>
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::themezer {
void Menu::ToggleFavorite() {
    if (m_pages.empty() || m_page_index < 0 || m_page_index >= static_cast<s64>(m_pages.size())) {
        return;
    }
    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done || m_index < 0 || m_index >= static_cast<s64>(page.m_packList.size())) {
        return;
    }
    const auto& entry = page.m_packList[m_index];
    const auto& id = entry.id;

    auto it = std::find(m_favorite_ids.begin(), m_favorite_ids.end(), id);
    if (it != m_favorite_ids.end()) {
        m_favorite_ids.erase(it);
        ini_puts("themezer_favorites", id.c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_name").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_creator").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_themes").c_str(), nullptr, App::CONFIG_PATH);
        App::Notify("Removed from Favorites"_i18n);
    } else {
        m_favorite_ids.push_back(id);
        ini_puts("themezer_favorites", id.c_str(), PackListEntryToJson(entry).c_str(), App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_name").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_creator").c_str(), nullptr, App::CONFIG_PATH);
        ini_puts("themezer_favorites", (id + "_themes").c_str(), nullptr, App::CONFIG_PATH);
        App::Notify("Added to Favorites"_i18n);
    }
    UpdateFavoriteAction();
}

void Menu::UpdateFavoriteAction() {
    if (m_pages.empty() || m_page_index < 0 || m_page_index >= static_cast<s64>(m_pages.size())) {
        RemoveAction(Button::L3);
        return;
    }
    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done || m_index < 0 || m_index >= static_cast<s64>(page.m_packList.size())) {
        RemoveAction(Button::L3);
        return;
    }
    const auto& entry = page.m_packList[m_index];
    if (IsFavorite(entry.id)) {
        SetAction(Button::L3, Action{"Unstar"_i18n, [this](){ ToggleFavorite(); }});
    } else {
        SetAction(Button::L3, Action{"Star"_i18n, [this](){ ToggleFavorite(); }});
    }
}

bool Menu::IsFavorite(const std::string& id) const {
    return std::find(m_favorite_ids.begin(), m_favorite_ids.end(), id) != m_favorite_ids.end();
}

Menu::Menu(u32 flags) : MenuBase{"Themezer"_i18n, flags} {
    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);
    m_favorite_ids = GetFavoriteIds();
    UpdateFavoriteAction();

    SetAction(Button::B, Action{"Back"_i18n, [this]{
        // if search is valid, then we are in search mode, return back to normal.
        if (!m_search.empty()) {
            m_search.clear();
            InvalidateAllPages();
        } else {
            SetPop();
        }
    }});

    this->SetActions(
        std::make_pair(Button::A, Action{"Download"_i18n, [this](){
            App::Push<OptionBox>(
                "Download theme?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [this](auto op_index){
                    if (op_index && *op_index) {
                        const auto& page = m_pages[m_page_index];
                        if (page.m_packList.size() && page.m_ready == PageLoadState::Done) {
                            const auto& entry = page.m_packList[m_index];

                            App::Push<ProgressBox>(entry.preview.lazy_image.image, "Downloading "_i18n, entry.details.name, [this, &entry](auto pbox) -> Result {
                                return InstallTheme(pbox, entry);
                            }, [this, &entry](Result rc){
                                App::PushErrorBox(rc, "Failed to download theme"_i18n);

                                if (R_SUCCEEDED(rc)) {
                                    App::Notify("Downloaded "_i18n + entry.details.name);
                                }
                            });
                        }
                    }
                }
            );
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }}),
        std::make_pair(Button::Y, Action{"Screenshot"_i18n, [this](){
            DisplayScreenshots();
        }}),
        std::make_pair(Button::R, Action{"Next Page"_i18n, [this](){
            m_page_index++;
            if (m_page_index >= m_page_index_max) {
                m_page_index = m_page_index_max - 1;
            } else {
                PackListDownload();
            }
        }}),
        std::make_pair(Button::L, Action{"Previous Page"_i18n, [this](){
            if (m_page_index) {
                m_page_index--;
                PackListDownload();
            }
        }}),
        std::make_pair(Button::R2, Action{"", [this](){
            if (m_page_index + 10 >= m_page_index_max) {
                m_page_index = m_page_index_max - 1;
            } else {
                m_page_index += 10;
            }
            PackListDownload();
        }}),
        std::make_pair(Button::L2, Action{"", [this](){
            if (m_page_index >= 10) {
                m_page_index -= 10;
            } else {
                m_page_index = 0;
            }
            PackListDownload();
        }})
    );

    const Vec4 v{75, 110, 350, 250};
    const Vec2 pad{10, 10};
    m_list = std::make_unique<List>(3, 6, m_pos, v, pad);

    m_page_index = 0;
    m_pages.resize(1);
    PackListDownload();
}

Menu::~Menu() {
    *m_alive = false;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_pages.empty()) {
        return;
    }

    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done) {
        return;
    }

    if (m_index == static_cast<s64>(page.m_packList.size() - 1)) {
        if (controller->GotDown(Button::DOWN) || controller->GotDown(Button::RIGHT)) {
            if (m_page_index < m_page_index_max - 1) {
                m_page_index++;
                m_index = 0;
                PackListDownload();
                controller->Reset();
                return;
            }
        }
    }

    m_list->OnUpdate(controller, touch, m_index, page.m_packList.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_pages.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty!"_i18n.c_str());
        return;
    }

    auto& page = m_pages[m_page_index];

    switch (page.m_ready) {
        case PageLoadState::None:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Not Ready..."_i18n.c_str());
            return;
        case PageLoadState::Loading:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading"_i18n.c_str());
            return;
        case PageLoadState::Done:
            break;
        case PageLoadState::Error:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Error loading page!"_i18n.c_str());
            return;
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, page.m_packList.size(), m_index, [this, &page, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto& [x, y, w, h] = v;
        auto& e = page.m_packList[pos];

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = pos == m_index;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            DrawElement(x, y, w, h, ThemeEntryID_GRID);
        }

        const float xoff = (350 - 320) / 2;

        // lazy load image
        Preview* preview = &e.preview;
        std::string_view preview_id{e.id.c_str(), e.id.size()};
        if (preview->thumb.empty() && e.themes.size()) {
            preview = &e.themes[0].preview;
            preview_id = std::string_view{e.themes[0].id.c_str(), e.themes[0].id.size()};
        }

        if (!preview->thumb.empty()) {
            auto& image = preview->lazy_image;
            const auto page_generation_for_image = m_page_generation;
            const auto page_index_for_image = m_page_index;
            const auto entry_index_for_image = pos;
            const bool use_pack_preview = preview == &e.preview;

            // try and load cached image.
            if (image_load_count < image_load_max && !image.image && !image.tried_cache) {
                image.tried_cache = true;
                image.cached = loadPreviewImage(*preview, preview_id);
                if (image.cached) {
                    image_load_count++;
                }
            }

            if (!image.image || image.cached) {
                switch (image.state) {
                    case ImageDownloadState::None: {
                        const auto path = apiBuildIconCache(preview_id);
                        log_write("downloading theme!: %s\n", path.s);

                        const auto url = preview->thumb;
                        log_write("downloading url: %s\n", url.c_str());
                        image.state = ImageDownloadState::Progress;
                        curl::Api().ToFileAsync(
                            curl::Url{url},
                            curl::Path{path},
                            curl::Flags{curl::Flag_Cache},
                            curl::StopToken{this->GetToken()},
                            curl::Priority::Normal,
                            curl::OnComplete{[this, page_generation_for_image, page_index_for_image, entry_index_for_image, use_pack_preview, alive_weak = std::weak_ptr<bool>(m_alive)](auto& result) {
                                auto alive = alive_weak.lock();
                                if (!alive || !*alive) {
                                    return;
                                }
                                if (page_generation_for_image != m_page_generation) {
                                    return;
                                }
                                if (page_index_for_image >= m_pages.size()) {
                                    return;
                                }

                                auto& page = m_pages[page_index_for_image];
                                if (entry_index_for_image >= page.m_packList.size()) {
                                    return;
                                }

                                auto& entry = page.m_packList[entry_index_for_image];
                                auto* preview = &entry.preview;
                                if (!use_pack_preview) {
                                    if (entry.themes.empty()) {
                                        return;
                                    }
                                    preview = &entry.themes[0].preview;
                                }

                                auto& image = preview->lazy_image;
                                if (result.success) {
                                    image.state = ImageDownloadState::Done;
                                    // data hasn't changed
                                    if (result.code == 304) {
                                        image.cached = false;
                                    }
                                } else {
                                    image.state = ImageDownloadState::Failed;
                                    log_write("failed to download image\n");
                                }
                            }
                        });
                    }   break;
                    case ImageDownloadState::Progress: {

                    }   break;
                    case ImageDownloadState::Done: {
                        image.cached = false;
                        if (!loadPreviewImage(*preview, preview_id)) {
                            image.state = ImageDownloadState::Failed;
                        } else {
                            image_load_count++;
                        }
                    }   break;
                    case ImageDownloadState::Failed: {
                    }   break;
                }
            }

            gfx::drawImage(vg, x + xoff, y, 320, 180, image.image ? image.image : App::GetDefaultImage(), 5);
        }

        const auto text_x = x + xoff;
        const auto text_clip_w = w - 30.f - xoff;
        const float font_size = 18;
        m_scroll_name.Draw(vg, selected, text_x, y + 180 + 20, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.details.name.c_str());
        m_scroll_author.Draw(vg, selected, text_x, y + 180 + 55, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.creator.display_name.c_str());
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_checked_for_nro) {
        m_checked_for_nro = true;

        // check if we have the nro, if not, then prompt the user to download from the appstore.
        if (!HasNro()) {
            App::Push<OptionBox>(
                "NXthemes_Installer.nro not found, download now?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [this](auto op_index){
                    if (op_index && *op_index) {
                        const gh::AssetEntry asset{
                            .name = "NXThemesInstaller.nro",
                            // same path as appstore
                            .path = "/switch/Switch_themes_Installer/NXThemesInstaller.nro",
                        };

                        gh::Download(NRO_URL, asset);
                    }
                }
            );
        }
    }
}

void Menu::InvalidateAllPages() {
    m_page_generation++;
    m_pages.clear();
    m_pages.resize(1);
    m_page_index = 0;
    PackListDownload();
}

void Menu::PackListDownload() {
    const auto page_index = m_page_index + 1;
    char subheading[128];
    std::snprintf(subheading, sizeof(subheading), "Page %zu / %zu"_i18n.c_str(), m_page_index+1, m_page_index_max);
    SetTitleSubHeading(subheading);
    SetSubHeading("");

    m_index = 0;
    m_list->SetYoff(0);

    // already downloaded
    if (m_pages[m_page_index].m_ready != PageLoadState::None) {
        return;
    }
    m_pages[m_page_index].m_ready = PageLoadState::Loading;

    Config config;
    config.page = page_index;
    config.SetQuery(m_search);
    config.sort_index = m_sort.Get();
    config.order_index = m_order.Get();

    const auto target_val = m_target.Get();
    if (target_val > 0 && target_val <= std::size(REQUEST_TARGET)) {
        config.target = REQUEST_TARGET[target_val - 1];
    }

    const auto tags_val = m_tags.Get();
    if (!tags_val.empty()) {
        std::string current;
        for (char ch : tags_val) {
            if (ch == ',' || ch == ' ' || ch == ';') {
                if (!current.empty()) {
                    config.tags.push_back(current);
                    current.clear();
                }
            } else {
                current += ch;
            }
        }
        if (!current.empty()) {
            config.tags.push_back(current);
        }
    }

    const auto packList_body = apiBuildListPacksBody(config);
    const auto packlist_path = apiBuildListPacksCache(config);
    const auto page_generation = m_page_generation;

    log_write("\npackList_body: %s\n\n", packList_body.c_str());

    curl::Api().ToFileAsync(
        curl::Url{GRAPHQL_URL},
        curl::Path{packlist_path},
        curl::Fields{packList_body},
        curl::Header{
            { "Accept", "application/json" },
            { "Content-Type", "application/json" },
        },
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, page_index, page_generation, alive_weak = std::weak_ptr<bool>(m_alive)](auto& result){
            auto alive = alive_weak.lock();
            if (!alive || !*alive) {
                return;
            }
            App::SetBoostMode(true);
            ON_SCOPE_EXIT(App::SetBoostMode(false));

            if (page_generation != m_page_generation) {
                log_write("ignoring stale themezer generation\n");
                return;
            }

            if (page_index == 0 || page_index > m_pages.size()) {
                log_write("ignoring stale themezer page response: %zu\n", static_cast<size_t>(page_index));
                return;
            }

            log_write("got themezer data\n");
            if (!result.success) {
                auto& page = m_pages[page_index-1];
                page.m_ready = PageLoadState::Error;
                log_write("failed to get themezer data...\n");
                return;
            }

            PackList a;
            from_json(result.path, a);

            if (!a.pagination.page_count || page_index > a.pagination.page_count) {
                auto& page = m_pages[page_index-1];
                page.m_ready = PageLoadState::Error;
                log_write("failed to parse themezer data...\n");
                return;
            }

            m_pages.resize(a.pagination.page_count);
            auto& page = m_pages[page_index-1];

            page.m_packList = a.packList;
            page.m_pagination = a.pagination;
            page.m_ready = PageLoadState::Done;
            m_page_index_max = a.pagination.page_count;
            UpdateFavoriteAction();

            char subheading[128];
            std::snprintf(subheading, sizeof(subheading), "Page %zu / %zu"_i18n.c_str(), m_page_index+1, m_page_index_max);
            SetTitleSubHeading(subheading);
            SetSubHeading("");

            log_write("a.pagination.page: %zu\n", a.pagination.page);
            log_write("a.pagination.page_count: %zu\n", a.pagination.page_count);
        }
    });
}


sphaira::ui::menu::themezer::LazyImage::~LazyImage() {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

} // namespace sphaira::ui::menu::themezer
