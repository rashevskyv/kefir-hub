#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include "ui/menus/filebrowser.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "swkbd.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::settings {

Menu::Menu() : MenuBase{"Settings"_i18n, MenuFlag_None} {
    BuildCategories();

    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            OnBack();
        }})
    );

    m_category_list = std::make_unique<List>(1, 8, Vec4{76.f, 138.f, 300.f, 448.f}, Vec4{76.f, 138.f, 300.f, 56.f});
    m_category_list->SetLayout(List::Layout::GRID);
    // up/down wrap around the ends; L/R (page) and ZL/ZR (ends) are driven
    // manually in Update() instead of via the List flags, because in a
    // single-column GRID those flags also capture dpad LEFT/RIGHT, which this
    // menu uses to move between the category and item panes.
    m_category_list->SetPageJump(false);
    m_category_list->SetFastScroll(false);

    m_item_list = std::make_unique<List>(1, 7, Vec4{420.f, 132.f, 780.f, 462.f}, Vec4{420.f, 132.f, 780.f, 66.f});
    m_item_list->SetLayout(List::Layout::GRID);
    m_item_list->SetPageJump(false);
    m_item_list->SetFastScroll(false);

    SetCategoryIndex(0);
}

Menu::~Menu() = default;

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    // an open folder owns the right pane: rebuild its rows in place (a popup
    // may have changed what they read) and leave the category pane alone.
    if (m_folder_open) {
        BuildCategories();
        m_category_index = std::clamp<s64>(m_category_index, 0, static_cast<s64>(m_categories.size()) - 1);

        if (m_folder_builder) {
            const auto yoff = m_item_list->GetYoff();
            m_folder_items = m_folder_builder();
            m_item_list->SetYoff(yoff);
        }

        SetFolderIndex(m_folder_index);
        return;
    }

    std::string category_label;
    std::string item_label;
    if (!m_categories.empty()) {
        category_label = m_categories[m_category_index].label;
        const auto& items = m_categories[m_category_index].items;
        if (!items.empty() && m_item_index >= 0 && m_item_index < items.size()) {
            item_label = items[m_item_index].label;
        }
    }

    BuildCategories();
    auto it = std::find_if(m_categories.cbegin(), m_categories.cend(), [&](const auto& category) {
        return category.label == category_label;
    });

    s64 new_cat_index = (it == m_categories.cend()) ? m_category_index : std::distance(m_categories.cbegin(), it);
    float saved_yoff = m_item_list->GetYoff();
    s64 saved_item_index = m_item_index;

    SetCategoryIndex(new_cat_index);

    if (new_cat_index < m_categories.size()) {
        const auto& new_items = m_categories[new_cat_index].items;
        if (new_items.empty()) {
            m_item_index = 0;
            m_item_list->SetYoff(0.f);
            return;
        }
        auto item_it = std::find_if(new_items.cbegin(), new_items.cend(), [&](const auto& item) {
            return item.label == item_label;
        });
        if (item_it != new_items.cend()) {
            SetItemIndex(std::distance(new_items.cbegin(), item_it));
        } else {
            SetItemIndex(std::clamp<s64>(saved_item_index, 0, static_cast<s64>(new_items.size() - 1)));
        }

        if (new_cat_index == m_category_index) {
            m_item_list->SetYoff(saved_yoff);
        }
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_categories.empty()) return;
    bool focus_changed = false;
    if (touch->is_clicked) {
        if (touch->in_range(m_category_list->GetPos())) {
            if (m_focus_pane != FocusPane::Categories) {
                SetFocusPane(FocusPane::Categories);
                focus_changed = true;
            }
        } else if (touch->in_range(m_item_list->GetPos())) {
            if (m_focus_pane != FocusPane::Items) {
                SetFocusPane(FocusPane::Items);
                focus_changed = true;
            }
        }
    }

    if (m_focus_pane == FocusPane::Categories) {
        if (controller->GotDown(Button::RIGHT)) {
            SetFocusPane(FocusPane::Items);
            App::PlaySoundEffect(SoundEffect_Focus);
        }
    } else {
        if (controller->GotDown(Button::LEFT)) {
            SetFocusPane(FocusPane::Categories);
            App::PlaySoundEffect(SoundEffect_Focus);
        }
    }

    MenuBase::Update(controller, touch);

    // fast navigation of the focused pane: L/R jump a page, ZL/ZR jump to the
    // first/last entry. done here (not through List's page-jump/fast-scroll
    // flags) so it doesn't steal dpad LEFT/RIGHT from the pane switch above.
    {
        const bool cats = m_focus_pane == FocusPane::Categories;
        List* list = cats ? m_category_list.get() : m_item_list.get();
        s64 idx = cats ? CategoryRow() : CurrentItemIndex();
        const s64 cnt = cats ? CategoryRowCount() : static_cast<s64>(CurrentItems().size());

        bool moved = false;
        if (controller->GotDown(Button::R2)) {
            moved = list->ScrollToEnd(idx, cnt);
        } else if (controller->GotDown(Button::L2)) {
            moved = list->ScrollToStart(idx, cnt);
        } else if (controller->GotDown(Button::R)) {
            moved = list->ScrollPageDown(idx, cnt);
        } else if (controller->GotDown(Button::L)) {
            moved = list->ScrollPageUp(idx, cnt);
        }

        if (moved) {
            App::PlaySoundEffect(SoundEffect_Focus);
            if (cats) {
                SetCategoryRow(idx);
            } else {
                SetCurrentItemIndex(idx);
            }
        }
    }

    if (m_focus_pane == FocusPane::Categories) {
        m_category_list->OnUpdate(controller, touch, CategoryRow(), CategoryRowCount(), [this, focus_changed](bool touch, auto i) {
            if (touch && CategoryRow() == i) {
                if (!focus_changed) {
                    FireAction(Button::A);
                }
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetCategoryRow(i);
            }
        }, this);
    } else {
        m_item_list->OnUpdate(controller, touch, CurrentItemIndex(), CurrentItems().size(), [this, focus_changed](bool touch, auto i) {
            if (touch && CurrentItemIndex() == i) {
                if (!focus_changed) {
                    FireAction(Button::A);
                }
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetCurrentItemIndex(i);
            }
        }, this);
    }

    // a finger scrolls the pane it is over, not the pane that happens to hold
    // the cursor: reaching for the right hand list should not need a focus
    // change first. The cursor stays where it is either way.
    if (m_focus_pane == FocusPane::Categories) {
        m_item_list->OnUpdateTouchOnly(touch, CurrentItems().size());
    } else {
        m_category_list->OnUpdateTouchOnly(touch, CategoryRowCount());
    }

    // the pane may have changed category above, so read the live one.
    const auto& current_items = CurrentItems();
    const bool on_network_location = !m_folder_open
        && m_item_index >= 0
        && m_item_index < static_cast<s64>(current_items.size())
        && current_items[m_item_index].id == NETWORK_LOCATION_ID;

    if (m_focus_pane == FocusPane::Items && on_network_location) {
        const auto location_name = current_items[m_item_index].label;
        SetAction(Button::START, Action{"Options"_i18n, [this, location_name](){
            auto network_locations = location::Load();
            auto it = std::find_if(network_locations.begin(), network_locations.end(), [&](const auto& e) {
                return e.name == location_name;
            });
            if (it != network_locations.end()) {
                location::Entry loc = *it;
                auto options = std::make_unique<Sidebar>(loc.name, Sidebar::Side::RIGHT);

                options->Add<SidebarEntryCallback>("Enter/Connect"_i18n, [this, loc](){
                    if (loc.IsConfigured()) {
                        if (loc.IsSmb() || loc.IsNfs()) {
                            App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None, &loc);
                        } else {
                            App::Push<OptionBox>("Browsing is not supported for this protocol yet."_i18n, "OK"_i18n);
                        }
                    } else {
                        App::Push<SourceEditMenu>(loc.name);
                    }
                }, true, "Connect to this network location."_i18n);

                options->Add<SidebarEntryCallback>("Edit"_i18n, [loc](){
                    App::Push<SourceEditMenu>(loc.name);
                }, true, "Configure connection settings."_i18n);

                options->Add<SidebarEntryCallback>("Test Connection"_i18n, [loc](){
                    App::Push<ProgressBox>(0, "Testing Connection..."_i18n, loc.name, [loc](auto pbox) -> Result {
                        return TestLocationConnection(loc);
                    }, [loc](Result rc) {
                        filebrowser::SetSourceConnectionStatus(loc.url, R_SUCCEEDED(rc));
                        if (R_SUCCEEDED(rc)) {
                            App::Notify("Connection test successful!"_i18n);
                        } else {
                            App::Push<OptionBox>("Connection test failed!"_i18n, "OK"_i18n);
                        }
                    });
                }, true, "Test connection with current settings."_i18n);

                options->Add<SidebarEntryCallback>("Rename"_i18n, [this, loc](){
                    std::string out;
                    if (R_SUCCEEDED(swkbd::ShowText(out, "Rename Network Location"_i18n.c_str(), loc.name.c_str()))) {
                        if (!out.empty() && out != loc.name) {
                            location::Remove(loc.name);
                            location::Entry new_loc = loc;
                            new_loc.name = out;
                            location::Add(new_loc);
                            App::Notify("Location renamed successfully!"_i18n);
                            OnFocusGained();
                        }
                    }
                }, true, "Rename this network location."_i18n);

                options->Add<SidebarEntryCallback>("Properties"_i18n, [loc](){
                    std::string props = "Name: "_i18n + loc.name + "\n";
                    std::string proto = loc.protocol;
                    if (proto.empty()) {
                        if (loc.IsSmb()) proto = "smb";
                        else if (loc.IsNfs()) proto = "nfs";
                        else if (loc.url.starts_with("ftp://")) proto = "ftp";
                        else if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) proto = "webdav"; // fallback
                        else if (loc.url.starts_with("webdav://") || loc.url.starts_with("webdavs://")) proto = "webdav";
                    }
                    props += "Protocol: "_i18n + proto + "\n";
                    props += "URL: "_i18n + loc.url + "\n";
                    if (!loc.user.empty()) {
                        props += "Username: "_i18n + loc.user + "\n";
                    }
                    if (loc.port) {
                        props += "Port: "_i18n + std::to_string(loc.port) + "\n";
                    }
                    App::Push<OptionBox>(props, "OK"_i18n);
                }, true, "View network location properties."_i18n);

                options->Add<SidebarEntryCallback>("Delete"_i18n, [this, loc](){
                    App::Push<OptionBox>(
                        "Delete this network location?"_i18n,
                        "No"_i18n, "Yes"_i18n, 0, [this, loc](auto op_delete_idx) {
                            if (op_delete_idx && *op_delete_idx) {
                                if (loc.name == App::GetWebdavUrlName()) {
                                    App::SetWebdavUrl("");
                                }
                                location::Remove(loc.name);
                                App::Notify("Location deleted successfully!"_i18n);
                                OnFocusGained();
                            }
                        }
                    );
                }, true, "Delete this network location."_i18n);

                App::Push(std::move(options));
            }
        }});
    } else {
        RemoveAction(Button::START);
    }
}





void Menu::SetFocusPane(FocusPane pane) {
    m_focus_pane = pane;
}

void Menu::SetCategoryIndex(s64 index) {
    if (m_categories.empty()) {
        m_category_index = 0;
        m_item_index = 0;
        return;
    }

    m_category_index = std::clamp<s64>(index, 0, static_cast<s64>(m_categories.size() - 1));
    m_item_index = 0;
    m_item_list->SetYoff(0);
    if (!m_category_index) {
        m_category_list->SetYoff(0);
    }

    // a category may open on a section header; step past it.
    SetItemIndex(0);

    SetTitleSubHeading(m_categories[m_category_index].description, true);
    SetSubHeading("");
}

void Menu::SetItemIndex(s64 index) {
    const auto& items = m_categories[m_category_index].items;
    if (items.empty()) {
        m_item_index = 0;
        return;
    }

    m_item_index = ResolveItemIndex(items, index, m_item_index);
    // the cursor may have stepped over a caption or wrapped around an end,
    // neither of which the list scrolled for.
    m_item_list->EnsureVisible(m_item_index, static_cast<s64>(items.size()));
}

auto Menu::CategoryRowCount() const -> s64 {
    return static_cast<s64>(m_categories.size()) + (m_folder_open ? 1 : 0);
}

auto Menu::CategoryRow() const -> s64 {
    return m_folder_open ? m_category_index + 1 : m_category_index;
}

void Menu::SetCategoryRow(s64 row) {
    const auto count = CategoryRowCount();
    if (count <= 0) {
        return;
    }

    row = std::clamp<s64>(row, 0, count - 1);

    if (m_folder_open) {
        // still on the open folder's own row: nothing changes.
        if (row == m_category_index + 1) {
            m_category_list->EnsureVisible(row, count);
            return;
        }

        // leaving the folder's row closes it, and the rows below it shift back
        // up by one now that it is gone.
        const auto category = row <= m_category_index ? row : row - 1;
        CloseFolder();
        SetCategoryIndex(category);
    } else {
        SetCategoryIndex(row);
    }

    m_category_list->EnsureVisible(CategoryRow(), CategoryRowCount());
}

auto Menu::CurrentItems() const -> const std::vector<SettingsItem>& {
    return m_folder_open ? m_folder_items : m_categories[m_category_index].items;
}

auto Menu::CurrentItemIndex() const -> s64 {
    return m_folder_open ? m_folder_index : m_item_index;
}

void Menu::SetCurrentItemIndex(s64 index) {
    if (m_folder_open) {
        SetFolderIndex(index);
    } else {
        SetItemIndex(index);
    }
}

void Menu::SetFolderIndex(s64 index) {
    if (m_folder_items.empty()) {
        m_folder_index = 0;
        return;
    }

    m_folder_index = ResolveItemIndex(m_folder_items, index, m_folder_index);
    m_item_list->EnsureVisible(m_folder_index, static_cast<s64>(m_folder_items.size()));
}

void Menu::OpenFolder(const SettingsItem& item) {
    m_saved_item_index = m_item_index;
    m_saved_item_yoff = m_item_list->GetYoff();

    m_folder_builder = item.folder_items;
    m_folder_items = m_folder_builder();
    m_folder_label = item.label;
    m_folder_open = true;
    m_folder_index = 0;
    m_item_list->SetYoff(0);

    SetFolderIndex(0);
    SetTitleSubHeading(item.description, true);
    SetSubHeading("");
    SetFocusPane(FocusPane::Items);
    m_category_list->EnsureVisible(CategoryRow(), CategoryRowCount());
}

void Menu::CloseFolder() {
    if (!m_folder_open) {
        return;
    }

    m_folder_open = false;
    m_folder_items.clear();
    m_folder_builder = {};
    m_folder_label.clear();
    m_folder_index = 0;

    m_item_index = m_saved_item_index;
    m_item_list->SetYoff(m_saved_item_yoff);
    m_item_list->EnsureVisible(m_item_index, static_cast<s64>(m_categories[m_category_index].items.size()));
    SetTitleSubHeading(m_categories[m_category_index].description, true);
    SetSubHeading("");
}

void Menu::OnSelect() {
    if (m_categories.empty()) {
        return;
    }

    if (m_focus_pane == FocusPane::Categories) {
        SetFocusPane(FocusPane::Items);
        return;
    }

    const auto& items = CurrentItems();
    const auto index = CurrentItemIndex();
    if (index < 0 || index >= static_cast<s64>(items.size())) {
        return;
    }

    // by value: opening a folder replaces the vector this row lives in.
    const auto item = items[index];
    if (item.kind == SettingsItemKind::Folder && item.folder_items) {
        OpenFolder(item);
        return;
    }

    if (item.action) {
        item.action();
    }
}

void Menu::OnBack() {
    // a folder page hands the right pane back to its category, one step at a
    // time, instead of dropping straight out of the settings menu.
    if (m_folder_open) {
        const auto pane = m_focus_pane;
        CloseFolder();
        SetFocusPane(pane);
        m_category_list->EnsureVisible(CategoryRow(), CategoryRowCount());
        return;
    }

    if (m_focus_pane == FocusPane::Items) {
        SetFocusPane(FocusPane::Categories);
        return;
    }

    SetPop();
}


} // namespace sphaira::ui::menu::settings
