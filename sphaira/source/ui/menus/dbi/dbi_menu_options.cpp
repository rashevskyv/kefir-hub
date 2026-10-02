#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/dbi_internal.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "ui/sidebar.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace sphaira::ui::menu::dbi {

void Menu::UpdateActions() {
    const auto state = m_state.load();
    if (state == State::ReviewQueue) {
        RemoveActions();
        SetActions(
            std::make_pair(Button::X, Action{"Select"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                if (m_install_requested) return;
                if (m_index >= 0 && m_index < static_cast<s64>(m_queue.size())) {
                    if (R_SUCCEEDED(m_queue[m_index].analysis_result)) {
                        m_queue[m_index].selected = !m_queue[m_index].selected;
                    }
                    if (m_index + 1 < static_cast<s64>(m_queue.size())) {
                        SetIndex(m_index + 1);
                    }
                }
            }}),
            std::make_pair(Button::Y, Action{"Invert"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                if (m_install_requested) return;
                for (auto& entry : m_queue) {
                    if (R_SUCCEEDED(entry.analysis_result)) entry.selected = !entry.selected;
                }
            }}),
            std::make_pair(Button::A, Action{"Install selected"_i18n, [this]() { StartInstall(); }}),
            std::make_pair(Button::L3, Action{"Package target"_i18n, [this]() { CycleSelectedTarget(); }}),
            std::make_pair(Button::R3, Action{m_minimized ? "Expand"_i18n : "Minimize"_i18n, [this]() { ToggleMinimized(); }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this]() { DisplayQueueOptions(); }}),
            std::make_pair(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }})
        );
        m_actions_dirty = false;
        return;
    }

    InstallSession::UpdateActions();

    if (state == State::Installing) {
        SetAction(Button::START, Action{"Options"_i18n, [this]() { DisplayQueueOptions(); }});
    } else if (state == State::Summary && !m_session_failed) {
        SetAction(Button::B, Action{"Back"_i18n, [this]() {
            {
                SCOPED_MUTEX(&m_mutex);
                RecomputePlan(true);
            }
            m_state = State::ReviewQueue;
            m_actions_dirty = true;
        }});
    }
}


void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_state.load() == State::ReviewQueue) {
        if (m_actions_dirty) UpdateActions();
        MenuBase::Update(controller, touch);
        bool activate = false;
        if (!m_queue.empty()) {
            SCOPED_MUTEX(&m_mutex);
            m_list->OnUpdate(controller, touch, m_index, m_queue.size(), [this, &activate](bool pressed, s64 index) {
                if (pressed && m_index == index) activate = true;
                else SetIndex(index);
            }, this);
        }
        if (activate) FireAction(Button::A);
        return;
    }

    InstallSession::Update(controller, touch);
}


void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff();
    } else if (!m_queue.empty()) {
        // keep one row of context past the cursor visible, so scrolling starts
        // at the second-to-last row rather than when the cursor falls off the
        // edge. also covers the callers that move the index themselves (X
        // toggling selection), which otherwise never touched the scroll offset.
        const s64 count = m_queue.size();
        m_list->EnsureVisible(m_index + 1, count);
        m_list->EnsureVisible(m_index - 1, count);
        m_list->EnsureVisible(m_index, count);
    }
}

void Menu::CycleSelectedTarget() {
    SCOPED_MUTEX(&m_mutex);
    if (m_install_requested) return;
    if (m_index < 0 || m_index >= static_cast<s64>(m_queue.size()) || R_FAILED(m_queue[m_index].analysis_result)) return;
    auto& target = m_queue[m_index].target;
    target = target == InstallTarget::Auto ? InstallTarget::Sd
        : target == InstallTarget::Sd ? InstallTarget::Nand : InstallTarget::Auto;
}

void Menu::SortQueue() {
    SCOPED_MUTEX(&m_mutex);
    if (m_state.load() != State::ReviewQueue || m_install_requested) return;
    if (m_queue.empty()) return;

    size_t focused_source_index = 0;
    bool has_focus = false;
    if (m_index >= 0 && m_index < static_cast<s64>(m_queue.size())) {
        focused_source_index = m_queue[m_index].source_index;
        has_focus = true;
    }

    const bool is_asc = (m_session_sort_order == 0);

    auto comp = [&](const QueueEntry& a, const QueueEntry& b) -> bool {
        switch (m_session_sort_type) {
            case 1: { // Name
                int cmp = strcasecmp(a.file_name.c_str(), b.file_name.c_str());
                if (cmp != 0) {
                    return is_asc ? (cmp < 0) : (cmp > 0);
                }
                return a.source_index < b.source_index;
            }
            case 2: { // Package size
                s64 sa = QueuePackageSize(a);
                s64 sb = QueuePackageSize(b);
                if (sa != sb) {
                    return is_asc ? (sa < sb) : (sa > sb);
                }
                return a.source_index < b.source_index;
            }
            case 3: { // Install size
                s64 ia = PlanSize(a);
                s64 ib = PlanSize(b);
                if (ia != ib) {
                    return is_asc ? (ia < ib) : (ia > ib);
                }
                return a.source_index < b.source_index;
            }
            case 0: // Queue order
            default: {
                return is_asc ? (a.source_index < b.source_index) : (a.source_index > b.source_index);
            }
        }
    };

    std::stable_sort(m_queue.begin(), m_queue.end(), comp);

    if (has_focus) {
        for (size_t i = 0; i < m_queue.size(); i++) {
            if (m_queue[i].source_index == focused_source_index) {
                SetIndex(static_cast<s64>(i));
                break;
            }
        }
    }
    RecomputePlan();
    m_actions_dirty = true;
}

void Menu::DisplayQueueOptions(bool left_side) {
    auto options = std::make_unique<Sidebar>("Options"_i18n, left_side ? Sidebar::Side::LEFT : Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const bool global = App::GetSaveSettingsGlobally();

    options->Add<SidebarEntryHeader>("Install Options"_i18n);

    SidebarEntryArray::Items skip_installed_items;
    skip_installed_items.push_back("Reinstall"_i18n);
    skip_installed_items.push_back("Skip"_i18n);
    skip_installed_items.push_back("Prompt"_i18n);

    s64 current_skip = global ? App::GetApp()->m_skip_if_already_installed.Get() : m_session_skip_if_already_installed;
    options->Add<SidebarEntryArray>("Skip if already installed"_i18n, skip_installed_items, [this, global](s64& index_out){
        if (global) {
            App::GetApp()->m_skip_if_already_installed.Set(index_out);
        } else {
            m_session_skip_if_already_installed = index_out;
        }
    }, current_skip, "For titles / ncas already installed: reinstall, skip, or prompt each time."_i18n);

    SidebarEntryArray::Items install_items;
    install_items.push_back("microSD card only"_i18n);
    install_items.push_back("System memory only"_i18n);
    install_items.push_back("System first, then SD"_i18n);
    install_items.push_back("SD first, then system"_i18n);
    install_items.push_back("Automatic"_i18n);

    s64 current_loc = global ? App::GetInstallLocation() : m_session_install_location;
    options->Add<SidebarEntryArray>("Install location"_i18n, install_items, [this, global](s64& index_out){
        if (global) {
            App::SetInstallLocation(index_out);
        } else {
            m_session_install_location = index_out;
        }
        if (m_state.load() == State::ReviewQueue) {
            SCOPED_MUTEX(&m_mutex);
            RecomputePlan(true);
            m_actions_dirty = true;
        }
    }, current_loc);

    // one entry per target: the two media fill at very different rates and want
    // very different headroom.
    auto add_reserve = [&](const std::string& title, const std::string& prompt, const std::string& help,
                           long (*get)(), void (*set)(long), long* session) {
        auto entry_ptr = std::make_unique<SidebarEntryTextBase>(title,
            std::to_string(global ? get() : *session) + " MB", nullptr, help);
        auto* entry = entry_ptr.get();
        entry->SetCallback([this, global, entry, prompt, get, set, session]() {
            s64 out = global ? get() : *session;
            if (R_SUCCEEDED(swkbd::ShowNumPad(out, prompt.c_str(), std::to_string(out).c_str(), 1, 5))) {
                if (out >= 0 && out <= 32768) {
                    if (global) {
                        set(out);
                    } else {
                        *session = out;
                    }
                    entry->SetValue(std::to_string(out) + " MB");
                    if (m_state.load() == State::ReviewQueue) {
                        SCOPED_MUTEX(&m_mutex);
                        RecomputePlan(true);
                        m_actions_dirty = true;
                    }
                }
            }
        });
        options->Add(std::move(entry_ptr));
    };

    add_reserve("Reserve free space (system)"_i18n, "Enter System Reserve Free Space (MB)"_i18n,
        "Free space to keep on system memory when planning installs (MB)."_i18n,
        App::GetInstallReserveMb, App::SetInstallReserveMb, &m_session_reserve_mb);
    add_reserve("Reserve free space (microSD)"_i18n, "Enter microSD Reserve Free Space (MB)"_i18n,
        "Free space to keep on the microSD card when planning installs (MB)."_i18n,
        App::GetInstallReserveSdMb, App::SetInstallReserveSdMb, &m_session_reserve_sd_mb);

    auto screen_off_entry = options->Add<SidebarEntryCallback>("Screen off (Minus)"_i18n, [left_side]() {
        auto sub = std::make_unique<Sidebar>("Screen off (Minus)"_i18n, left_side ? Sidebar::Side::LEFT : Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(sub)));

        static constexpr const char* MODE_LABELS[] = {
            "Lower brightness",
            "Turn off backlight",
            "Screensaver",
        };
        static constexpr long BRIGHTNESS_STEPS[] = { 1, 5, 10, 20, 30, 50 };
        static constexpr const char* TIMEOUT_LABELS[] = {
            "Off",
            "30 s",
            "1 min",
            "2 min",
            "5 min",
            "10 min",
        };
        static constexpr long TIMEOUT_STEPS[] = { 0, 30, 60, 120, 300, 600 };

        SidebarEntryArray::Items mode_items;
        for (const auto& label : MODE_LABELS) {
            mode_items.push_back(i18n::get(label));
        }
        s64 current_mode = std::clamp<s64>(App::GetBlankMode(), 0, std::size(MODE_LABELS) - 1);
        sub->Add<SidebarEntryArray>("Minus button"_i18n, mode_items, [](s64& index_out) {
            App::SetBlankMode(index_out);
        }, current_mode, "What pressing Minus does while the install queue is running."_i18n);

        SidebarEntryArray::Items timeout_items;
        s64 current_timeout_idx = 0;
        const long current_timeout = App::GetBlankTimeout();
        for (size_t i = 0; i < std::size(TIMEOUT_STEPS); i++) {
            timeout_items.push_back(i18n::get(TIMEOUT_LABELS[i]));
            if (TIMEOUT_STEPS[i] == current_timeout) {
                current_timeout_idx = static_cast<s64>(i);
            }
        }
        sub->Add<SidebarEntryArray>("Inactivity timeout"_i18n, timeout_items, [](s64& index_out) {
            if (index_out >= 0 && index_out < static_cast<s64>(std::size(TIMEOUT_STEPS))) {
                App::SetBlankTimeout(TIMEOUT_STEPS[index_out]);
            }
        }, current_timeout_idx, "Automatically start the screen off mode after a period of inactivity during installation."_i18n);

        SidebarEntryArray::Items brightness_items;
        s64 current_brightness_idx = 0;
        const long current_brightness = App::GetBlankBrightness();
        for (size_t i = 0; i < std::size(BRIGHTNESS_STEPS); i++) {
            brightness_items.push_back(std::to_string(BRIGHTNESS_STEPS[i]) + "%");
            if (BRIGHTNESS_STEPS[i] == current_brightness) {
                current_brightness_idx = static_cast<s64>(i);
            }
        }
        sub->Add<SidebarEntryArray>("Brightness"_i18n, brightness_items, [](s64& index_out) {
            if (index_out >= 0 && index_out < static_cast<s64>(std::size(BRIGHTNESS_STEPS))) {
                App::SetBlankBrightness(BRIGHTNESS_STEPS[index_out]);
            }
        }, current_brightness_idx, "Panel brightness while the screen is lowered. Ignored when the backlight is turned off."_i18n);

        sub->Add<SidebarEntryBool>("OLED mode"_i18n, App::GetSaverOled(), [](bool& val) {
            App::SetSaverOled(val);
        }, "Light only the pixels that carry information: the empty part of the progress bar is left black."_i18n);

        sub->Add<SidebarEntryHeader>("Show on screensaver"_i18n);

        const auto add_field = [&sub](SaverField bit, const std::string& label, const std::string& description) {
            const bool enabled = (App::GetSaverFields() & bit) != 0;
            sub->Add<SidebarEntryBool>(label, enabled, [bit](bool& val) {
                App::SetSaverField(bit, val);
            }, description);
        };

        add_field(SaverField_Clock, "Clock"_i18n, "Show the current time."_i18n);
        add_field(SaverField_Status, "Status"_i18n, "Show what the queue is doing."_i18n);
        add_field(SaverField_Counter, "Package counter"_i18n, "Show which package of how many is being installed."_i18n);
        add_field(SaverField_File, "Current file"_i18n, "Show the package and file being written."_i18n);
        add_field(SaverField_Bar, "Progress bar"_i18n, "Show the whole-queue progress bar and percentage."_i18n);
        add_field(SaverField_Speed, "Average speed"_i18n, "Show the average write speed."_i18n);
        add_field(SaverField_Eta, "Time remaining"_i18n, "Show the estimated time left for the whole queue."_i18n);
        add_field(SaverField_Elapsed, "Elapsed time"_i18n, "Show how long the queue has been running."_i18n);
        add_field(SaverField_Battery, "Battery"_i18n, "Show the battery level and whether it is charging."_i18n);
        add_field(SaverField_Errors, "Errors"_i18n, "Show the failure count, once anything has failed."_i18n);
        add_field(SaverField_Graph, "Speed graph"_i18n, "Show the live installation read/write speed graph."_i18n);
    }, "Blank or dim the panel while a long queue runs, and choose what the screensaver shows."_i18n);
    screen_off_entry->SetHasSubmenu(true);

    if (m_state.load() == State::ReviewQueue) {
        options->Add<SidebarEntryHeader>("View"_i18n);

        SidebarEntryArray::Items sort_items;
        sort_items.push_back("Queue order"_i18n);
        sort_items.push_back("Name"_i18n);
        sort_items.push_back("Package size"_i18n);
        sort_items.push_back("Install size"_i18n);

        options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
            m_session_sort_type = index_out;
            SortQueue();
        }, m_session_sort_type);

        SidebarEntryArray::Items order_items;
        order_items.push_back("Ascending"_i18n);
        order_items.push_back("Descending"_i18n);

        options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
            m_session_sort_order = index_out;
            SortQueue();
        }, m_session_sort_order);
    }
}

} // namespace sphaira::ui::menu::dbi

#endif
