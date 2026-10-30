#include "ui/menus/tools_menu.hpp"
#include "battery_page.hpp"

#include "ui/menus/uninstaller_menu.hpp"
#include "ui/menus/users_menu.hpp"
#include "ui/menus/wifi_menu.hpp"
#include "ui/menus/settings/settings_fancurve.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/system_info_menu.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "game_updates.hpp"
#include "zero_fill.hpp"
#include "system_cleanup.hpp"

#include "app.hpp"
#include "option.hpp"
#include "swkbd.hpp"
#include "web.hpp"
#include "web_favorites.hpp"
#include "fs.hpp"
#include "ui/remote_input.hpp"
#include <functional>
#include "ui/menus/storage_menu.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>

namespace sphaira::ui::menu::tools {
namespace {

void ComingSoon() {
    App::Push<ui::OptionBox>("Coming soon"_i18n, "OK"_i18n);
}

void OpenCleanup() {
    // a list from the bottom with a tick box per kind of junk, then one row that starts the cleanup.
    auto opts = std::make_shared<cleanup::Options>();
    bool* const fields[] = {
        &opts->old_updates, &opts->orphans_sd, &opts->orphans_nand, &opts->placeholders_sd, &opts->placeholders_nand,
        &opts->unused_tickets, &opts->erpt_reports, &opts->contents_folders, &opts->deleted_user_saves,
    };
    PopupList::Items items{
        "Old game updates"_i18n, "Lost content on the SD card"_i18n, "Lost content in system memory"_i18n,
        "Unfinished installs on the SD card"_i18n, "Unfinished installs in system memory"_i18n, "Unused tickets"_i18n,
        "Error reports"_i18n, "Folders of removed games"_i18n, "Saves of removed users"_i18n,
        "Start cleanup"_i18n,
    };
    std::vector<std::string> descriptions{
        "Updates older than the newest one installed for the same game."_i18n,
        "Game files no installed game uses any more."_i18n,
        "Game files no installed game uses any more."_i18n,
        "Pieces left by installs that stopped half way."_i18n,
        "Pieces left by installs that stopped half way."_i18n,
        "Tickets of games that are no longer installed."_i18n,
        "Crash reports in /atmosphere/erpt_reports."_i18n,
        "Mods and cheats folders in /atmosphere/contents for games that are no longer on the console. Sysmodules are kept."_i18n,
        "Saves of users that are no longer on the console. They cannot be restored afterwards."_i18n,
        "Removes everything ticked above."_i18n,
    };
    auto checks = std::make_shared<std::vector<bool>>();
    for (auto* f : fields) {
        checks->push_back(*f);
    }
    auto popup = std::make_unique<PopupList>("Clean system junk"_i18n, std::move(items), [opts, checks, fields](auto op_index){
        if (!op_index) {
            return;
        }
        for (size_t i = 0; i < checks->size(); i++) {
            *fields[i] = (*checks)[i];
        }
        // an install that is minimized is still running; its unfinished files would be removed
        if (App::GetProgressActive()) {
            App::Notify("Finish or stop the running transfer first."_i18n);
            return;
        }
        auto report = std::make_shared<cleanup::Report>();
        App::Push<ui::ProgressBox>(0, "Clean system junk"_i18n, "", [opts, report](auto pbox) -> Result {
            return cleanup::Run(pbox, *opts, *report);
        }, [report](Result rc){
            if (R_SUCCEEDED(rc) || rc == Result_TransferCancelled) {
                // a stopped run reports what it did before it stopped
                std::string text = (R_SUCCEEDED(rc) ? "Cleaned: "_i18n : "Cleanup stopped. Cleaned: "_i18n) + std::to_string(report->removed) +
                    " · SD +" + grid::FormatBytes(static_cast<u64>(report->freed_sd)) +
                    " · NAND +" + grid::FormatBytes(static_cast<u64>(report->freed_nand));
                if (report->skipped) {
                    text += " · " + "Skipped: "_i18n + std::to_string(report->skipped);
                }
                App::Notify(text);
            } else {
                App::PushErrorBox(rc, "Cleanup failed!"_i18n);
            }
        });
    });
    popup->SetChecks(checks).SetDescriptions(std::move(descriptions));
    App::Push(std::move(popup));
}

// microSD only: the NAND variant was removed in v0.14.018.
void FillSdWithZeros() {
    App::Push<ui::OptionBox>(
        "Overwrite all free space on the microSD card with zeros? Files are not touched. It can take a long time."_i18n,
        "Back"_i18n, "Fill"_i18n, 1, [](auto op_index){
        if (!op_index || !*op_index) {
            return;
        }
        App::Push<ui::ProgressBox>(0, "Filling with zeros"_i18n, "microSD card"_i18n, [](auto pbox) -> Result {
            return zero_fill::FillSdFreeSpace(pbox);
        }, [](Result rc){
            if (R_SUCCEEDED(rc)) {
                App::Notify("Free space filled with zeros"_i18n);
            } else if (rc != Result_TransferCancelled) {
                App::PushErrorBox(rc, "Fill failed!"_i18n);
            }
        });
    });
}

// a plain row of the system tools list: label, description, action.
auto Tool(std::string label, std::string description, std::function<void()> action) -> settings::SettingsItem {
    return { std::move(label), std::move(description), {}, std::move(action), settings::SettingsItemKind::Normal };
}

// ---- Web browser: the console's own browser (the NetFront applet, the one BrowseNX starts) behind a page of favourites.
// It is a system applet with its own memory needs: it opens fine when Kefir Hub runs with the full memory (Title Mode).
constexpr const char* FAVOURITES_PATH = "/config/kefir/web_favorites.txt";

auto LoadFavourites() -> std::vector<web::Favourite> {
    std::vector<u8> data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(FAVOURITES_PATH, data))) {
        return {};
    }
    return web::Parse({reinterpret_cast<const char*>(data.data()), data.size()});
}

void SaveFavourites(const std::vector<web::Favourite>& list) {
    const auto text = web::Format(list);
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/config/kefir");
    sd.write_entire_file(FAVOURITES_PATH, std::vector<u8>(text.begin(), text.end()));
    sd.Commit();
}

void OpenUrl(const std::string& url) {
    if (R_FAILED(WebShow(url))) {
        App::Push<ui::OptionBox>("The browser could not start. It needs the full memory: start Kefir Hub from a game (hold R while launching it)."_i18n, "OK"_i18n);
    }
}

// the address from the console's keyboard or sent from a phone or a computer (the QR page); the callback gets it tidied, never empty
void AskAddress(const std::string& initial, std::function<void(const std::string&)> on_url) {
    ui::remote_input::Options opts{};
    opts.title = "Web address"_i18n;
    opts.guide = "Type a web address or send it from a phone or a computer"_i18n;
    opts.default_text = initial;
    opts.placeholder = "https://...";
    opts.max_length = 500;
    ui::remote_input::PromptTextInput(opts, [on_url](const std::string& text) {
        const auto url = web::NormalizeUrl(text);
        if (!url.empty()) {
            on_url(url);
        }
    });
}

void AddFavourite(const std::function<void()>& reopen) {
    AskAddress({}, [reopen](const std::string& url) {
        auto list = LoadFavourites();
        if (web::Add(list, url)) {
            SaveFavourites(list);
        } else {
            App::Notify("Already in the favourites"_i18n);
        }
        reopen();
    });
}

void OpenBrowser() {
    // the system web applet of firmware 20.0.0 to 22.5.0 asks for a system update and opens no page: say so instead
    if (!web::BrowserWorks(HOSVER_MAJOR(hosversionGet()), HOSVER_MINOR(hosversionGet()), HOSVER_MICRO(hosversionGet()))) {
        App::Push<ui::OptionBox>("The browser needs firmware 23.0.1 or newer. Update the firmware in the Updater first."_i18n, "OK"_i18n);
        return;
    }
    static option::OptionString last_url{"web_browser", "last_url", "https://"};
    // the page is rebuilt after every change: the old one leaves and a new one is pushed
    auto self = std::make_shared<SystemToolsMenu*>(nullptr);
    const std::function<void()> reopen = [self]() {
        if (*self) {
            (*self)->SetPop();
        }
        OpenBrowser();
    };

    constexpr size_t FIRST_FAVOURITE_ROW = 4; // Open, Enter an address, Favourites, Add to favourites
    const auto favourites = LoadFavourites();
    std::vector<settings::SettingsItem> items;
    items.push_back(settings::MakeHeader("Open"_i18n));
    items.push_back(Tool("Enter an address"_i18n, "Type a web address, or send it from a phone or a computer, and open it."_i18n, []() {
        AskAddress(last_url.Get(), [](const std::string& url) {
            last_url.Set(url);
            OpenUrl(url);
        });
    }));
    items.push_back(settings::MakeHeader("Favourites"_i18n));
    items.push_back(Tool("Add to favourites"_i18n, "Type a web address, or send it from a phone or a computer, and keep it here."_i18n, [reopen]() {
        AddFavourite(reopen);
    }));
    for (const auto& fav : favourites) {
        auto item = Tool(fav.title, fav.url, [url = fav.url]() { OpenUrl(url); });
        item.selectable = true;
        items.push_back(std::move(item));
    }

    auto menu = std::make_unique<SystemToolsMenu>("Web browser"_i18n, std::move(items));
    *self = menu.get();
    // START: the context menu. It works on the marked favourites (X, Y), or on the one under the cursor.
    menu->SetOptions([reopen, favourites](const std::vector<size_t>& rows) {
        auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));
        options->Add<SidebarEntryCallback>("Add to favourites"_i18n, [reopen]() { AddFavourite(reopen); }, true)->SetIcon(ActionIcon::Star);
        if (rows.empty()) {
            return;
        }
        if (rows.size() == 1) {
            const auto url = favourites[rows[0] - FIRST_FAVOURITE_ROW].url;
            options->Add<SidebarEntryCallback>("Open"_i18n, [url]() { OpenUrl(url); }, true)->SetIcon(ActionIcon::Launch);
        }
        options->Add<SidebarEntryCallback>("Remove"_i18n, [reopen, rows, favourites]() {
            const auto text = rows.size() == 1
                ? "Remove from favourites:"_i18n + " " + favourites[rows[0] - FIRST_FAVOURITE_ROW].title + "?"
                : "Remove from favourites:"_i18n + " " + std::to_string(rows.size()) + "?";
            App::Push<ui::OptionBox>(text, "Back"_i18n, "Remove"_i18n, 0, [reopen, rows](auto op) {
                if (!op || *op != 1) {
                    return;
                }
                auto list = LoadFavourites();
                // from the end, so the earlier numbers stay right
                for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
                    web::Remove(list, *it - FIRST_FAVOURITE_ROW);
                }
                SaveFavourites(list);
                reopen();
            });
        }, true)->SetIcon(ActionIcon::Delete);
    });
    App::Push(std::move(menu));
}

// one list, three captions. The cursor steps over the captions.
auto ToolItems() -> std::vector<settings::SettingsItem> {
    std::vector<settings::SettingsItem> items {
        settings::MakeHeader("Diagnostics"_i18n),
        Tool("System information"_i18n, "Firmware, Atmosphere and console details."_i18n, sysinfo::Open),
        Tool("Check game updates"_i18n, "Find installed games that have a newer update."_i18n, game_updates::Open),
        settings::MakeHeader("Settings"_i18n),
        Tool("Module Manager"_i18n, "Start, stop and configure installed sysmodules."_i18n, [](){
            App::Push<ui::menu::hats::UninstallerMenu>();
        }),
        Tool("Fan curve"_i18n, "Edit Atmosphere tskin fan curves for handheld and docked modes."_i18n, [](){
            ui::menu::settings::OpenFanCurveMenu();
        }),
        Tool("Wi-Fi"_i18n, "Manage wireless connections."_i18n, [](){
            App::Push<ui::menu::wifi::Menu>();
        }),
        Tool("Web browser"_i18n, "Open a web page in the console's own browser. Needs the full memory."_i18n, OpenBrowser),
        Tool("Users"_i18n, "Create, rename, backup and link console user profiles."_i18n, [](){
            App::Push<ui::menu::users::Menu>();
        }),
        settings::MakeHeader("Maintenance"_i18n),
        Tool("Battery calibration"_i18n, "Drain the battery to the cut-off, set the zero and run two full cycles."_i18n, battery::OpenCalibration),
        Tool("Battery"_i18n, "Fuel gauge state, the system's saved copy and a backup."_i18n, battery::Open),
        Tool("Storage manager"_i18n, "What fills the microSD card: games, saves, emuMMC, homebrew and the rest."_i18n, ui::menu::storage::Open),
        Tool("Clean system junk"_i18n, "Delete old updates, lost game files, unused tickets and other leftovers."_i18n, OpenCleanup),
        Tool("Fill free SD space with zeros"_i18n, "Overwrite unused microSD space."_i18n, FillSdWithZeros),
        Tool("Remove parental controls"_i18n, "Clear the console parental-control PIN."_i18n, ComingSoon),
    };
    return items;
}

} // namespace

SystemToolsMenu::SystemToolsMenu() : SystemToolsMenu{"Tools"_i18n, ToolItems()} {
}

SystemToolsMenu::SystemToolsMenu(const std::string& title, std::vector<settings::SettingsItem> items)
    : MenuBase{title, MenuFlag_None}, m_items{std::move(items)} {
    // a page of plain results has nothing to open.
    if (std::any_of(m_items.begin(), m_items.end(), [](const auto& item) { return bool(item.action); })) {
        SetAction(Button::A, Action{"Open"_i18n, [this](){ OnSelect(); }});
    }
    SetAction(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }});
    // selectable rows (the Web browser favourites) are marked like files in the File Browser: X marks one, Y inverts all
    if (std::any_of(m_items.begin(), m_items.end(), [](const auto& item) { return item.selectable; })) {
        SetAction(Button::X, Action{"Select"_i18n, [this](){
            if (m_items[m_index].selectable) {
                m_items[m_index].marked = !m_items[m_index].marked;
            }
        }});
        SetAction(Button::Y, Action{"Invert"_i18n, [this](){
            for (auto& item : m_items) {
                if (item.selectable) {
                    item.marked = !item.marked;
                }
            }
        }});
    }

    // compact rows: the tools list fits on one screen. A longer list (a page of results)
    // shows 11 rows and scrolls. The list area is as tall as its rows plus room for the
    // cursor outline, so the last row and its frame are never cut by the list's own clip.
    constexpr float row_h = 44.f;
    const auto rows = std::min<s64>(static_cast<s64>(m_items.size()), 11);
    m_list = std::make_unique<List>(1, rows,
        Vec4{75.f, 132.f, 1145.f, row_h * static_cast<float>(rows) + 8.f}, Vec4{75.f, 132.f, 1130.f, row_h});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

void SystemToolsMenu::DrawRow(NVGcontext* vg, Theme* theme, const Vec4& v, const settings::SettingsItem& item, bool selected) {
    const float mid_y = v.y + v.h / 2.f;
    if (item.kind == settings::SettingsItemKind::Header) {
        // a caption: small, dimmed, with a rule running off to the right.
        const auto colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawText(vg, v.x + 12.f, mid_y + 4.f, 15.f, colour, item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        float bounds[4];
        nvgFontSize(vg, 15.f);
        gfx::textBounds(vg, 0, 0, bounds, item.label.c_str());
        const float rule_x = v.x + 12.f + (bounds[2] - bounds[0]) + 12.f;
        gfx::drawRect(vg, rule_x, mid_y + 4.f, std::max(0.f, v.x + v.w - 12.f - rule_x), 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        return;
    }

    if (item.done) {
        // a finished step: a green tick where the number was, the name in green, the detail dimmed
        const auto good = nvgRGB(0x4C, 0xAF, 0x50);
        const float split = v.x + v.w * 0.36f;
        if (selected) {
            gfx::drawRect(vg, v.x, v.y + 2.f, v.w, v.h - 4.f, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
            gfx::drawRectOutline(vg, theme, 4.f, v.x, v.y + 2.f, v.w, v.h - 4.f);
        }
        const float tx = v.x + 24.f, ty = mid_y - 10.f, ts = 20.f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, tx + ts * 0.08f, ty + ts * 0.55f);
        nvgLineTo(vg, tx + ts * 0.38f, ty + ts * 0.85f);
        nvgLineTo(vg, tx + ts * 0.92f, ty + ts * 0.18f);
        nvgStrokeColor(vg, good);
        nvgStrokeWidth(vg, ts * 0.13f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, v.x, v.y, split - v.x - 10.f, v.h);
        gfx::drawText(vg, tx + ts + 10.f, mid_y, 19.f, good, item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, split, v.y, v.x + v.w - 20.f - split, v.h);
        gfx::drawText(vg, split, mid_y, 15.f, theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        return;
    }

    if (item.disabled) {
        // same layout as a live row, in the dimmed colour
        const auto dim = nvgTransRGBAf(theme->GetColour(ThemeEntryID_TEXT_INFO), 0.55f);
        const float split = v.x + v.w * 0.36f;
        nvgSave(vg);
        nvgIntersectScissor(vg, v.x, v.y, split - v.x - 10.f, v.h);
        gfx::drawText(vg, v.x + 24.f, mid_y, 19.f, dim, item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, split, v.y, v.x + v.w - 20.f - split, v.h);
        gfx::drawText(vg, split, mid_y, 15.f, dim, item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        return;
    }

    if (item.marked || selected) {
        gfx::drawRect(vg, v.x, v.y + 2.f, v.w, v.h - 4.f, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
    }
    if (selected) {
        gfx::drawRectOutline(vg, theme, 4.f, v.x, v.y + 2.f, v.w, v.h - 4.f);
    }
    // a marked row (X, Y): a bar on the left, drawn last so the cursor does not cover it
    if (item.marked) {
        gfx::drawRect(vg, v.x + 8.f, v.y + 8.f, 6.f, v.h - 16.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 3.f);
    }
    // name on the left, description on the right, each clipped to its half.
    const float split = v.x + v.w * 0.36f;
    nvgSave(vg);
    nvgIntersectScissor(vg, v.x, v.y, split - v.x - 10.f, v.h);
    gfx::drawText(vg, v.x + 24.f, mid_y, 19.f, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    nvgSave(vg);
    nvgIntersectScissor(vg, split, v.y, v.x + v.w - 20.f - split, v.h);
    gfx::drawText(vg, split, mid_y, 15.f, theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
}

void SystemToolsMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    // left/right jump to the first item of the previous / next caption, so
    // three presses walk Diagnostics → Settings → Maintenance and back.
    if (controller && !m_items.empty() && (controller->GotDown(Button::RIGHT) || controller->GotDown(Button::LEFT))) {
        const auto count = static_cast<s64>(m_items.size());
        const auto is_header = [this](s64 i) { return m_items[i].kind == settings::SettingsItemKind::Header; };
        // the caption this item belongs to.
        s64 caption = m_index;
        while (caption > 0 && !is_header(caption)) caption--;
        s64 target = caption;
        if (controller->GotDown(Button::RIGHT)) {
            for (s64 i = caption + 1; i < count; i++) if (is_header(i)) { target = i; break; }
            if (target == caption) target = 0; // wrap to the first caption
        } else {
            for (s64 i = caption - 1; i >= 0; i--) if (is_header(i)) { target = i; break; }
            if (target == caption) { for (s64 i = count - 1; i > caption; i--) if (is_header(i)) { target = i; break; } }
        }
        App::PlaySoundEffect(SoundEffect_Focus);
        SetIndex(target + 1); // first item under the caption (ResolveItemIndex skips captions anyway)
    }
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && i < static_cast<s64>(m_items.size()) && (m_items[i].kind == settings::SettingsItemKind::Header || m_items[i].disabled)) {
            return;
        }
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void SystemToolsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawRow(vg, theme, v, m_items[i], m_index == i);
    });
}

void SystemToolsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void SystemToolsMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = settings::ResolveItemIndex(m_items, index, m_index);
    if (!m_index) {
        m_list->SetYoff(0);
    } else {
        m_list->EnsureVisible(m_index, m_items.size());
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

auto SystemToolsMenu::TargetRows() const -> std::vector<size_t> {
    std::vector<size_t> rows;
    for (size_t i = 0; i < m_items.size(); i++) {
        if (m_items[i].selectable && m_items[i].marked) {
            rows.push_back(i);
        }
    }
    if (rows.empty() && m_index >= 0 && m_index < static_cast<s64>(m_items.size()) && m_items[m_index].selectable) {
        rows.push_back(static_cast<size_t>(m_index));
    }
    return rows;
}

void SystemToolsMenu::SetOptions(std::function<void(const std::vector<size_t>&)> cb) {
    SetAction(Button::START, Action{"Options"_i18n, [this, cb = std::move(cb)](){
        cb(TargetRows());
    }});
}

void SystemToolsMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

} // namespace sphaira::ui::menu::tools
