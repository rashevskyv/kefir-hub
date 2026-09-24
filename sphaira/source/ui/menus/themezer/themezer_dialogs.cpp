#include "ui/menus/themezer.hpp"
#include "ui/menus/themezer/themezer_internal.hpp"
#include "ui/menus/file_viewer.hpp"
#include "ui/menus/ghdl.hpp"
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
#include "nro.hpp"
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::themezer {
void Menu::DisplayScreenshots() {
    if (m_pages.empty() || m_page_index < 0 || m_page_index >= static_cast<s64>(m_pages.size())) {
        return;
    }

    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done || m_index < 0 || m_index >= static_cast<s64>(page.m_packList.size())) {
        return;
    }

    const auto& pack = page.m_packList[m_index];
    std::vector<ScreenshotEntry> screenshots;
    screenshots.reserve(pack.themes.size());

    for (size_t i = 0; i < pack.themes.size(); i++) {
        const auto& theme = pack.themes[i];
        const auto url = GetPreviewUrl(theme.preview);
        if (url.empty()) {
            continue;
        }

        auto cache_id = theme.id;
        if (cache_id.empty()) {
            cache_id = pack.id + "_" + std::to_string(i);
        }

        screenshots.push_back({BuildScreenshotTitle(pack, theme), url, cache_id});
    }

    if (screenshots.empty()) {
        const auto url = GetPreviewUrl(pack.preview);
        if (url.empty()) {
            App::Notify("No screenshots"_i18n);
            return;
        }

        const auto title = pack.details.name.empty() ? "Screenshot"_i18n : pack.details.name;
        const auto cache_id = pack.id.empty() ? std::to_string(HashString(url)) : pack.id + "_collage";
        screenshots.push_back({title, url, cache_id});
    }

    auto paths = std::make_shared<std::vector<fs::FsPath>>();
    auto titles = std::make_shared<std::vector<std::string>>();
    paths->reserve(screenshots.size());
    titles->reserve(screenshots.size());

    bool needs_download = false;
    for (auto& screenshot : screenshots) {
        if (screenshot.cache_id.empty()) {
            screenshot.cache_id = std::to_string(HashString(screenshot.url));
        }

        const auto path = apiBuildScreenshotCache(screenshot.cache_id);
        needs_download |= !fs::FileExists(path);
        paths->emplace_back(path);
        titles->emplace_back(screenshot.title);
    }

    const auto open_gallery = [paths, titles](){
        if (!paths->empty()) {
            App::Push<fileview::Menu>((*paths)[0], *paths, 0, *titles);
        }
    };

    if (!needs_download) {
        open_gallery();
        return;
    }

    App::Push<ProgressBox>(
        0, "Downloading "_i18n, pack.details.name, [screenshots, paths](auto pbox) -> Result {
            for (size_t i = 0; i < screenshots.size(); i++) {
                if (pbox->ShouldExit()) {
                    return pbox->ShouldExitResult();
                }

                const auto& path = (*paths)[i];
                if (fs::FileExists(path)) {
                    continue;
                }

                pbox->NewTransfer(screenshots[i].title);
                const auto result = curl::Api().ToFile(
                    curl::Url{screenshots[i].url},
                    curl::Path{path},
                    curl::Flags{curl::Flag_Cache},
                    curl::OnProgress{pbox->OnDownloadProgressCallback()}
                );

                R_UNLESS(result.success, Result_ThemezerFailedToDownloadThemeMeta);
            }

            R_SUCCEED();
        }, [paths, titles](Result rc){
            App::PushErrorBox(rc, "Failed to download screenshot"_i18n);
            if (R_SUCCEEDED(rc) && !paths->empty()) {
                App::Push<fileview::Menu>((*paths)[0], *paths, 0, *titles);
            }
        }
    );
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Themezer Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items sort_items;
    sort_items.push_back("Rising"_i18n);
    sort_items.push_back("Trending"_i18n);
    sort_items.push_back("Created"_i18n);
    sort_items.push_back("Updated"_i18n);
    sort_items.push_back("Downloads"_i18n);
    sort_items.push_back("Saves"_i18n);

    SidebarEntryArray::Items order_items;
    order_items.push_back("Descending"_i18n);
    order_items.push_back("Ascending"_i18n);

    SidebarEntryArray::Items target_items;
    target_items.push_back("All"_i18n);
    target_items.push_back("Home Menu"_i18n);
    target_items.push_back("Lock Screen"_i18n);
    target_items.push_back("All Apps"_i18n);
    target_items.push_back("Settings"_i18n);
    target_items.push_back("Player Select"_i18n);
    target_items.push_back("User Page"_i18n);
    target_items.push_back("News"_i18n);

    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
        if (m_sort.Get() != index_out) {
            m_sort.Set(index_out);
            InvalidateAllPages();
        }
    }, m_sort.Get(), "Select how themes are sorted in the list."_i18n);

    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
        if (m_order.Get() != index_out) {
            m_order.Set(index_out);
            InvalidateAllPages();
        }
    }, m_order.Get(), "Sort themes in ascending or descending order."_i18n);

    options->Add<SidebarEntryArray>("Target"_i18n, target_items, [this](s64& index_out){
        if (m_target.Get() != index_out) {
            m_target.Set(index_out);
            InvalidateAllPages();
        }
    }, m_target.Get(), "Filter themes by target layout."_i18n);

    options->Add<SidebarEntryCallback>("Tags"_i18n, [this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out, "Enter tags (separated by spaces or commas)"_i18n.c_str(), m_tags.Get().c_str()))) {
            m_tags.Set(out);
            InvalidateAllPages();
        }
    }, "Filter themes by tags (e.g. anime, dark). Separate with spaces or commas."_i18n);

    options->Add<SidebarEntryCallback>("Page"_i18n, [this](){
        s64 out;
        if (R_SUCCEEDED(swkbd::ShowNumPad(out, "Enter Page Number"_i18n.c_str(), nullptr, -1, 3))) {
            if (out > 0 && out <= m_page_index_max) {
                m_page_index = out - 1;
                PackListDownload();
            } else {
                log_write("invalid page number\n");
                App::Notify("Bad Page"_i18n);
            }
        }
    }, "Jump to a specific page number in the theme list."_i18n);

    options->Add<SidebarEntryCallback>("Search"_i18n, [this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out)) && !out.empty()) {
            m_search = out;
            // PackListDownload();
            InvalidateAllPages();
        }
    }, "Search for themes by name or keyword."_i18n);

    if (HasNro()) {
        options->Add<SidebarEntryCallback>("Launch NXthemes_Installer.nro"_i18n, [](){
            const auto rc = nro_launch(GetNroPath());
            App::PushErrorBox(rc, "Failed to launch NXthemes_Installer.nro"_i18n);
        }, "Open the NXthemes Installer to apply downloaded themes."_i18n);
    }
}


} // namespace sphaira::ui::menu::themezer
