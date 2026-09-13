#include "ui/menus/wifi_menu.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "wifi_manager.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace sphaira::ui::menu::wifi {

Menu::Menu() : grid::Menu{"Wi-Fi"_i18n, MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Connect"_i18n, [this](){
            if (!m_items.empty()) {
                ConfirmConnect(m_items[m_index]);
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_selected_count > 0) {
                ClearSelection();
            } else {
                if (m_connecting) {
                    m_connecting = false;
                    sphaira::wifi::CancelConnect();
                }
                SetPop();
            }
        }}),
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){ ToggleCurrentSelection(); }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){ InvertSelection(); }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
    );
    OnLayoutChange();
}

Menu::~Menu() {
    if (m_connecting) {
        m_connecting = false;
    }
    sphaira::wifi::CancelConnect();
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    Refresh();
}

void Menu::Refresh() {
    const auto list = sphaira::wifi::GetProfiles();
    m_items.clear();
    m_selected_count = 0;
    for (const auto& p : list) {
        Item item;
        static_cast<sphaira::wifi::WifiProfile&>(item) = p;
        m_items.push_back(std::move(item));
    }
    SetIndex(m_index);
}

void Menu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        SetTitleSubHeading("No saved Wi-Fi networks"_i18n, true);
        SetSubHeading("");
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index && m_list) {
        m_list->SetYoff(0);
    }
    const auto& item = m_items[m_index];
    std::string display = item.name.empty() ? item.ssid : item.name;
    if (item.is_connected) {
        display += "  ·  " + "Connected"_i18n;
    } else {
        display += "  ·  " + item.GetAuthString();
    }
    SetTitleSubHeading(display, true);
    SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_items.size()));
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, grid::LayoutType_List);
    SetIndex(0);
}

void Menu::ToggleCurrentSelection() {
    if (m_items.empty()) {
        return;
    }
    auto& item = m_items[m_index];
    item.selected ^= 1;
    m_selected_count += item.selected ? 1 : -1;
    if (m_index + 1 < static_cast<s64>(m_items.size())) {
        SetIndex(m_index + 1);
        if (m_list) {
            m_list->EnsureVisible(m_index, m_items.size());
        }
    }
}

void Menu::InvertSelection() {
    m_selected_count = 0;
    for (auto& item : m_items) {
        item.selected ^= 1;
        if (item.selected) {
            m_selected_count++;
        }
    }
}

void Menu::ClearSelection() {
    for (auto& item : m_items) {
        item.selected = false;
    }
    m_selected_count = 0;
}

void Menu::SelectAll() {
    m_selected_count = 0;
    for (auto& item : m_items) {
        item.selected = true;
        m_selected_count++;
    }
}

auto Menu::SelectedProfiles() const -> std::vector<sphaira::wifi::WifiProfile> {
    std::vector<sphaira::wifi::WifiProfile> out;
    for (const auto& item : m_items) {
        if (item.selected) {
            out.push_back(item);
        }
    }
    if (out.empty() && !m_items.empty()) {
        const auto i = (m_index >= 0 && static_cast<size_t>(m_index) < m_items.size()) ? m_index : 0;
        out.push_back(m_items[i]);
    }
    return out;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_connecting) {
        if (m_connect_ts.GetMs() >= 8000) {
            m_connecting = false;
            sphaira::wifi::CancelConnect();
            Refresh();
            SetTitleSubHeading("Connection timed out"_i18n, true);
        } else {
            const auto status = sphaira::wifi::PollConnect();
            if (status.state == sphaira::wifi::ConnectState::Succeeded) {
                m_connecting = false;
                Refresh();
                SetTitleSubHeading("Connected to "_i18n + m_connecting_name, true);
            } else if (status.state == sphaira::wifi::ConnectState::Failed) {
                m_connecting = false;
                Refresh();
                App::PushErrorBox(status.result, "Failed to connect to Wi-Fi network."_i18n);
            }
        }
    }

    if (m_items.empty() || !m_list) {
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
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

    if (m_items.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No saved Wi-Fi networks"_i18n.c_str());
        return;
    }

    if (!m_list) {
        return;
    }

    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        auto& item = m_items[i];
        const auto selected = (m_index == i);

        if (!selected) {
            DrawElement(v, ThemeEntryID_GRID);
        } else {
            gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
        }

        if (item.selected) {
            auto tint = theme->GetColour(ThemeEntryID_FOCUS);
            tint.a *= 0.35f;
            gfx::drawRect(vg, v, tint, 5.f);
        }

        const float icon_size = 46.f;
        const float icon_x = v.x + 12.f;
        const float icon_y = v.y + (v.h - icon_size) / 2.f;
        DrawWifiIcon(vg, theme, Vec4{icon_x, icon_y, icon_size, icon_size}, item.is_connected, selected);

        const float text_x = icon_x + icon_size + 16.f;

        std::string status_tag;
        NVGcolor status_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        if (item.is_connected) {
            status_tag = "Connected"_i18n;
            status_col = nvgRGBA(80, 200, 120, 255);
        }

        float status_w = 0.f;
        if (!status_tag.empty()) {
            float bounds[4]{};
            gfx::textBounds(vg, 0, 0, bounds, status_tag.c_str());
            status_w = bounds[2] - bounds[0] + 20.f;
            gfx::drawText(vg, v.x + v.w - 20.f, v.y + v.h / 2.f, 16.f,
                status_col, status_tag.c_str(),
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }

        nvgSave(vg);
        nvgIntersectScissor(vg, text_x, v.y, v.w - (text_x - v.x) - 20.f - status_w, v.h);

        const std::string display_name = item.name.empty() ? item.ssid : item.name;
        gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
            "%s", display_name.c_str());

        std::string sub_text = item.GetAuthString();
        if (display_name != item.ssid && !item.ssid.empty()) {
            sub_text = item.ssid + "  ·  " + sub_text;
        }

        gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", sub_text.c_str());
        nvgRestore(vg);

        DrawSelectionMark(vg, theme, grid::LayoutType_List, v, Vec4{icon_x, icon_y, icon_size, icon_size}, item.selected, m_selected_count > 0);
    });
}

void Menu::DrawWifiIcon(NVGcontext* vg, Theme* theme, const Vec4& v, bool is_connected, bool selected) {
    const float cx = v.x + v.w / 2.f;
    const float cy = v.y + v.h * 0.72f;

    const NVGcolor col = is_connected ? nvgRGBA(80, 200, 120, 255)
                                      : theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);

    // Center dot
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, 3.2f);
    nvgFillColor(vg, col);
    nvgFill(vg);

    // Concentric arcs
    nvgStrokeColor(vg, col);
    nvgStrokeWidth(vg, 2.8f);
    nvgLineCap(vg, NVG_ROUND);

    // Inner arc (radius 9)
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, 9.f, -2.35619f, -0.785398f, NVG_CW);
    nvgStroke(vg);

    // Middle arc (radius 16)
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, 16.f, -2.35619f, -0.785398f, NVG_CW);
    nvgStroke(vg);
}

void Menu::ShowContextMenu() {
    if (m_selected_count > 1) {
        const std::string title = std::to_string(m_selected_count) + " " + "Selected"_i18n;
        auto options = std::make_unique<Sidebar>(title, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryHeader>("BATCH ACTIONS"_i18n);
        options->Add<SidebarEntryCallback>("Delete selected"_i18n, [this](){
            ConfirmDeleteBatch();
        }, true, "Remove all selected Wi-Fi networks from the console."_i18n);

        options->Add<SidebarEntryHeader>("SELECTION"_i18n);
        options->Add<SidebarEntryCallback>("Select all"_i18n, [this](){
            SelectAll();
        }, true, "Select all saved Wi-Fi networks."_i18n);
        options->Add<SidebarEntryCallback>("Clear selection"_i18n, [this](){
            ClearSelection();
        }, true, "Deselect all networks."_i18n);

        options->Add<SidebarEntryHeader>("WIRELESS"_i18n);
        const bool wifi_on = sphaira::wifi::IsWirelessEnabled();
        options->Add<SidebarEntryCallback>(wifi_on ? "Turn Wi-Fi Off"_i18n : "Turn Wi-Fi On"_i18n, [this, wifi_on](){
            const Result rc = sphaira::wifi::SetWirelessEnabled(!wifi_on);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, wifi_on ? "Failed to disable Wi-Fi."_i18n : "Failed to enable Wi-Fi."_i18n);
            } else {
                Refresh();
            }
        }, true, "Toggle wireless communication."_i18n);

        options->Add<SidebarEntryCallback>("Refresh"_i18n, [this](){
            Refresh();
        }, true, "Reload saved Wi-Fi network profiles."_i18n);
        return;
    }

    if (m_items.empty()) {
        auto options = std::make_unique<Sidebar>("Wi-Fi"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryHeader>("WIRELESS"_i18n);
        const bool wifi_on = sphaira::wifi::IsWirelessEnabled();
        options->Add<SidebarEntryCallback>(wifi_on ? "Turn Wi-Fi Off"_i18n : "Turn Wi-Fi On"_i18n, [this, wifi_on](){
            const Result rc = sphaira::wifi::SetWirelessEnabled(!wifi_on);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, wifi_on ? "Failed to disable Wi-Fi."_i18n : "Failed to enable Wi-Fi."_i18n);
            } else {
                Refresh();
            }
        }, true, "Toggle wireless communication."_i18n);
        options->Add<SidebarEntryCallback>("Refresh"_i18n, [this](){
            Refresh();
        }, true, "Reload saved Wi-Fi network profiles."_i18n);
        return;
    }

    size_t target_idx = static_cast<size_t>(m_index);
    if (m_selected_count == 1) {
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (m_items[i].selected) {
                target_idx = i;
                break;
            }
        }
    }
    const auto target = m_items[target_idx];
    const std::string title = target.name.empty() ? target.ssid : target.name;

    auto options = std::make_unique<Sidebar>(title, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryHeader>("CONNECTION"_i18n);
    options->Add<SidebarEntryCallback>("Connect"_i18n, [this, target](){
        ConfirmConnect(target);
    }, true, "Connect to this Wi-Fi network."_i18n);

    options->Add<SidebarEntryHeader>("SETTINGS"_i18n);
    options->Add<SidebarEntryCallback>("Rename"_i18n, [this, target](){
        ConfirmRename(target);
    }, true, "Change this network's profile display name."_i18n);

    options->Add<SidebarEntryCallback>("Change password"_i18n, [this, target](){
        ConfirmChangePassword(target);
    }, true, "Update the Wi-Fi security password without using Horizon system settings."_i18n);

    options->Add<SidebarEntryCallback>("Edit SSID"_i18n, [this, target](){
        ConfirmChangeSsid(target);
    }, true, "Change the network SSID."_i18n);

    options->Add<SidebarEntryCallback>("View password & details"_i18n, [this, target](){
        ShowDetails(target);
    }, true, "View the saved password, security protocol, and details."_i18n);

    options->Add<SidebarEntryCallback>("Delete network"_i18n, [this, target](){
        ConfirmDeleteSingle(target);
    }, true, "Remove this Wi-Fi network from saved profiles."_i18n);

    options->Add<SidebarEntryHeader>("WIRELESS"_i18n);
    const bool wifi_on = sphaira::wifi::IsWirelessEnabled();
    options->Add<SidebarEntryCallback>(wifi_on ? "Turn Wi-Fi Off"_i18n : "Turn Wi-Fi On"_i18n, [this, wifi_on](){
        const Result rc = sphaira::wifi::SetWirelessEnabled(!wifi_on);
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, wifi_on ? "Failed to disable Wi-Fi."_i18n : "Failed to enable Wi-Fi."_i18n);
        } else {
            Refresh();
        }
    }, true, "Toggle wireless communication."_i18n);

    options->Add<SidebarEntryCallback>("Refresh"_i18n, [this](){
        Refresh();
    }, true, "Reload saved Wi-Fi network profiles."_i18n);
}

void Menu::ConfirmConnect(const sphaira::wifi::WifiProfile& profile) {
    if (profile.is_connected) {
        App::Push<OptionBox>(
            "Already connected to '" + profile.ssid + "'.",
            "OK"_i18n
        );
        return;
    }

    const std::string name = profile.name.empty() ? profile.ssid : profile.name;
    const std::string msg = "Connect to '" + name + "'?";
    App::Push<OptionBox>(msg, "No"_i18n, "Connect"_i18n, 1, [this, profile, name](auto opt) {
        if (opt && *opt == 1) {
            const Result rc = sphaira::wifi::Connect(profile.uuid);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to initiate connection."_i18n);
            } else {
                m_connecting = true;
                m_connecting_uuid = profile.uuid;
                m_connecting_name = name;
                m_connect_ts.Update();
                SetTitleSubHeading("Connecting to '" + name + "'...", true);
            }
        }
    });
}

void Menu::ConfirmDeleteSingle(const sphaira::wifi::WifiProfile& profile) {
    const std::string name = profile.name.empty() ? profile.ssid : profile.name;
    const std::string msg = "Delete Wi-Fi network '" + name + "'?";
    App::Push<OptionBox>(msg, "Cancel"_i18n, "Delete"_i18n, 0, [this, profile](auto opt) {
        if (opt && *opt == 1) {
            const Result rc = sphaira::wifi::RemoveProfile(profile.uuid);
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to delete network profile."_i18n);
            } else {
                ClearSelection();
                Refresh();
            }
        }
    });
}

void Menu::ConfirmDeleteBatch() {
    auto selected = SelectedProfiles();
    if (selected.empty()) {
        return;
    }

    const std::string msg = "Delete " + std::to_string(selected.size()) + " selected Wi-Fi network(s)?";
    App::Push<OptionBox>(msg, "Cancel"_i18n, "Delete"_i18n, 0, [this, selected](auto opt) {
        if (opt && *opt == 1) {
            Result first_rc = 0;
            size_t failed_count = 0;
            for (const auto& p : selected) {
                const Result rc = sphaira::wifi::RemoveProfile(p.uuid);
                if (R_FAILED(rc)) {
                    if (R_SUCCEEDED(first_rc)) {
                        first_rc = rc;
                    }
                    failed_count++;
                }
            }
            ClearSelection();
            Refresh();
            if (failed_count > 0) {
                const size_t total_count = selected.size();
                const size_t succeeded_count = total_count - failed_count;
                std::string err_msg;
                if (succeeded_count > 0) {
                    err_msg = "Deleted "_i18n + std::to_string(succeeded_count) +
                              " network profiles; failed to delete "_i18n + std::to_string(failed_count) + ".";
                } else {
                    err_msg = "Failed to delete "_i18n + std::to_string(failed_count) +
                              " network profiles."_i18n;
                }
                App::PushErrorBox(first_rc, err_msg);
            }
        }
    });
}

void Menu::ConfirmRename(const sphaira::wifi::WifiProfile& profile) {
    std::string out;
    const std::string current = profile.name.empty() ? profile.ssid : profile.name;
    if (R_SUCCEEDED(swkbd::ShowText(out, "Rename Wi-Fi Network"_i18n.c_str(), current.c_str(), 1, 63)) && !out.empty() && out != current) {
        const Result rc = sphaira::wifi::RenameProfile(profile.uuid, out);
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to rename network profile."_i18n);
        } else {
            Refresh();
        }
    }
}

void Menu::ConfirmChangePassword(const sphaira::wifi::WifiProfile& profile) {
    std::string out;
    if (R_SUCCEEDED(swkbd::ShowText(out, "Enter New Wi-Fi Password"_i18n.c_str(), profile.passphrase.c_str(), 0, 64))) {
        const Result rc = sphaira::wifi::ChangePassphrase(profile.uuid, out);
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to update password."_i18n);
        } else {
            App::Push<OptionBox>("Password updated successfully."_i18n, "OK"_i18n);
            Refresh();
        }
    }
}

void Menu::ConfirmChangeSsid(const sphaira::wifi::WifiProfile& profile) {
    std::string out;
    if (R_SUCCEEDED(swkbd::ShowText(out, "Enter New SSID"_i18n.c_str(), profile.ssid.c_str(), 1, 32)) && !out.empty() && out != profile.ssid) {
        const Result rc = sphaira::wifi::ChangeSsid(profile.uuid, out);
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to update SSID."_i18n);
        } else {
            Refresh();
        }
    }
}

void Menu::ShowDetails(const sphaira::wifi::WifiProfile& profile) {
    std::string details = "Network Details\n\n";
    details += "SSID: " + profile.ssid + "\n";
    if (!profile.name.empty() && profile.name != profile.ssid) {
        details += "Name: " + profile.name + "\n";
    }
    details += "Security: " + profile.GetAuthString() + "\n";
    details += "Password: " + (profile.passphrase.empty() ? "(none)" : profile.passphrase) + "\n";
    details += "Status: " + (profile.is_connected ? "Connected"_i18n : "Saved"_i18n);

    App::Push<OptionBox>(details, "OK"_i18n);
}

} // namespace sphaira::ui::menu::wifi
