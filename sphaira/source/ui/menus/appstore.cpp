#include "ui/menus/appstore.hpp"
#include "ui/menus/appstore/appstore_internal.hpp"
#include "ui/menus/appstore_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "image.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::appstore {
Menu::Menu(u32 flags) : grid::Menu{"AppStore"_i18n, flags} {
    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively("/switch/sphaira/cache/appstore/icons");
    fs.CreateDirectoryRecursively("/switch/sphaira/cache/appstore/banners");
    fs.CreateDirectoryRecursively("/switch/sphaira/cache/appstore/screens");

    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_is_author) {
                m_is_author = false;
                if (m_is_search) {
                    SetSearch(m_search_term);
                } else {
                    SetFilter();
                }

                SetIndex(m_entry_author_jump_back);
                if (m_entry_author_jump_back >= 9) {
                    m_list->SetYoff((((m_entry_author_jump_back - 9) + 3) / 3) * m_list->GetMaxY());
                } else {
                    m_list->SetYoff(0);
                }
            } else if (m_is_search) {
                m_is_search = false;
                SetFilter();
                SetIndex(m_entry_search_jump_back);
                if (m_entry_search_jump_back >= 9) {
                    m_list->SetYoff(0);
                    m_list->SetYoff((((m_entry_search_jump_back - 9) + 3) / 3) * m_list->GetMaxY());
                } else {
                    m_list->SetYoff(0);
                }
            } else {
                SetPop();
            }
        }}),
        std::make_pair(Button::A, Action{"Info"_i18n, [this](){
            if (m_entries_current.empty()) {
                // log_write("pushing A when empty: size: %zu count: %zu\n", repo_json.size(), m_entries_current.size());
                return;
            }
            App::Push<EntryMenu>(m_entries[m_entries_current[m_index]], m_default_image, *this);
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("AppStore Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            SidebarEntryArray::Items filter_items;
            filter_items.push_back("All"_i18n);
            filter_items.push_back("Games"_i18n);
            filter_items.push_back("Emulators"_i18n);
            filter_items.push_back("Tools"_i18n);
            filter_items.push_back("Advanced"_i18n);
            filter_items.push_back("Themes"_i18n);
            filter_items.push_back("Legacy"_i18n);
            filter_items.push_back("Misc"_i18n);

            SidebarEntryArray::Items sort_items;
            sort_items.push_back("Updated"_i18n);
            sort_items.push_back("Downloads"_i18n);
            sort_items.push_back("Size"_i18n);
            sort_items.push_back("Alphabetical"_i18n);

            SidebarEntryArray::Items order_items;
            order_items.push_back("Descending"_i18n);
            order_items.push_back("Ascending"_i18n);

            SidebarEntryArray::Items layout_items;
            layout_items.push_back("Icon"_i18n);
            layout_items.push_back("Grid"_i18n);
            layout_items.push_back("HB Menu"_i18n);

            options->Add<SidebarEntryArray>("Filter"_i18n, filter_items, [this](s64& index_out){
                m_filter.Set(index_out);
                SetFilter();
            }, m_filter.Get(), "Show only apps from a specific category."_i18n);

            options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
                m_sort.Set(index_out);
                SortAndFindLastFile();
            }, m_sort.Get(), "Select which field to sort the app list by."_i18n);

            options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
                m_order.Set(index_out);
                SortAndFindLastFile();
            }, m_order.Get(), "Sort apps from newest to oldest or A to Z."_i18n);

            auto current_layout = m_layout.Get();
            if (current_layout == grid::LayoutType_List) {
                current_layout = grid::LayoutType_Grid;
                m_layout.Set(current_layout);
            }
            options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
                m_layout.Set(index_out + 1);
                OnLayoutChange();
            }, current_layout - 1, "Choose how apps are displayed on screen."_i18n);

            options->Add<SidebarEntryCallback>("Search"_i18n, [this](){
                std::string out;
                if (R_SUCCEEDED(swkbd::ShowText(out)) && !out.empty()) {
                    SetSearch(out);
                    log_write("got %s\n", out.c_str());
                }
            }, "Search for apps by name or keyword."_i18n);
        }})
    );

    m_repo_download_state = ImageDownloadState::Progress;
    curl::Api().ToFileAsync(
        curl::Url{URL_JSON},
        curl::Path{REPO_PATH},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result){
            if (result.success) {
                m_repo_download_state = ImageDownloadState::Done;
                if (HasFocus()) {
                    ScanHomebrew();
                }
            } else {
                m_repo_download_state = ImageDownloadState::Failed;
            }
        }
    });

    OnLayoutChange();
}

Menu::~Menu() {

}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries_current.size(), [this](bool touch, auto i) {
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

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading..."_i18n.c_str());
        return;
    }

    if (m_entries_current.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty!"_i18n.c_str());
        return;
    }

    if (m_layout.Get() == grid::LayoutType_HbMenu) {
        const auto index = m_entries_current[m_index];
        auto& e = m_entries[index];
        DrawHbMenuHeader(vg, theme, e.image.image ? e.image.image : m_default_image.image, e.title.c_str(), e.author.c_str(), e.version.c_str(), e.description.c_str());
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, m_entries_current.size(), m_index, [this, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto& [x, y, w, h] = v;
        const auto index = m_entries_current[pos];
        auto& e = m_entries[index];
        auto& image = e.image;

        // try and load cached image.
        if (image_load_count < image_load_max && !image.image && !image.tried_cache) {
            image.tried_cache = true;
            image.cached = EntryLoadImageFile(BuildIconCachePath(e), image);
            if (image.cached) {
                image_load_count++;
            }
        }

        // lazy load image
        if (!image.image || image.cached) {
            switch (image.state) {
                case ImageDownloadState::None: {
                    const auto path = BuildIconCachePath(e);
                    const auto url = BuildIconUrl(e);
                    image.state = ImageDownloadState::Progress;
                    curl::Api().ToFileAsync(
                        curl::Url{url},
                        curl::Path{path},
                        curl::Flags{curl::Flag_Cache},
                        curl::StopToken{this->GetToken()},
                        curl::OnComplete{[this, &image](auto& result) {
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
                    if (image_load_count < image_load_max) {
                        image.cached = false;
                        if (!EntryLoadImageFile(BuildIconCachePath(e), e.image)) {
                            image.state = ImageDownloadState::Failed;
                        } else {
                            image_load_count++;
                        }
                    }
                }   break;
                case ImageDownloadState::Failed: {
                }   break;
            }
        }

        const auto selected = pos == m_index;
        const auto image_vec = DrawEntryNoImage(vg, theme, m_layout.Get(), v, selected, e.title.c_str(), e.author.c_str(), e.version.c_str());

        const auto image_scale = 256.0 / image_vec.w;
        DrawIcon(vg, e.image, m_default_image, image_vec.x, image_vec.y, image_vec.w, image_vec.h, true, image_scale);
        // gfx::drawImage(vg, x + 20, y + 20, image_size, image_size_h, image.image ? image.image : m_default_image);

        // todo: fix position on non-grid layout.
        float i_size = 22;
        switch (e.status) {
            case EntryStatus::Get:
                gfx::drawImage(vg, x + w - 30.f, y + 110, i_size, i_size, m_get.image, 20);
                break;
            case EntryStatus::Installed:
                gfx::drawImage(vg, x + w - 30.f, y + 110, i_size, i_size, m_installed.image, 20);
                break;
            case EntryStatus::Local:
                gfx::drawImage(vg, x + w - 30.f, y + 110, i_size, i_size, m_local.image, 20);
                break;
            case EntryStatus::Update:
                gfx::drawImage(vg, x + w - 30.f, y + 110, i_size, i_size, m_update.image, 20);
                break;
        }
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    // log_write("saying we got focus base: size: %zu count: %zu\n", repo_json.size(), m_entries.size());

    if (!m_default_image.image) {
        EntryLoadImageData(App::GetDefaultImageData(), m_default_image);
        EntryLoadImageData(UPDATE_IMAGE_DATA, m_update);
        EntryLoadImageData(GET_IMAGE_DATA, m_get);
        EntryLoadImageData(LOCAL_IMAGE_DATA, m_local);
        EntryLoadImageData(INSTALLED_IMAGE_DATA, m_installed);
    }

    if (m_entries.empty()) {
        // log_write("got focus with empty size: size: %zu count: %zu\n", repo_json.size(), m_entries.size());

        if (m_repo_download_state == ImageDownloadState::Done) {
            // log_write("is done: size: %zu count: %zu\n", repo_json.size(), m_entries.size());
            ScanHomebrew();
        }
    } else {
        if (m_dirty) {
            m_dirty = false;
            const auto& current_entry = m_entries[m_entries_current[m_index]];
            Sort();

            for (u32 i = 0; i < m_entries_current.size(); i++) {
                if (current_entry.name == m_entries[m_entries_current[i]].name) {
                    const auto index = i;
                    const auto row = m_list->GetRow();
                    const auto page = m_list->GetPage();
                    // guesstimate where the position is
                    if (index >= page) {
                        m_list->SetYoff((((index - page) + row) / row) * m_list->GetMaxY());
                    } else {
                        m_list->SetYoff(0);
                    }
                    SetIndex(i);
                    break;
                }
            }
        }
    }
}

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries_current.size()));
}


LazyImage::~LazyImage() {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

} // namespace sphaira::ui::menu::appstore
