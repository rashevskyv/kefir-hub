#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_sources.hpp"

#include "ui/menus/filebrowser.hpp"

#include "app.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "log.hpp"
#include "swkbd.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/remote_input.hpp"
#include "utils/devoptab_smb2.hpp"
#include "utils/nfs_url.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {

// Two blocks: the sources themselves at the top (add one, then the ones you
// have), and the knobs that govern them below the fold. Opening this category
// should put "add a source" under the cursor, not a checkbox.
auto BuildSourcesCategoryItems(Menu* menu) -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "+ Add network location"_i18n,
        "Configure a new network location (supported protocols: SMB, NFS, WebDAV, FTP, HTTP)."_i18n,
        [](){ return std::string{}; },
        [menu](){
            sphaira::ui::menu::filebrowser::AddNetworkLocationInteractive([menu](){
                menu->OnFocusGained();
            });
        }
    });

    const auto network_locations = location::Load();
    for (const auto& loc : network_locations) {
        items.emplace_back(SettingsItem{
            loc.name,
            loc.url,
            [](){ return std::string{}; },
            [loc](){
                if (loc.IsConfigured()) {
                    App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None, &loc);
                } else {
                    App::Push<SourceEditMenu>(loc.name);
                }
            },
            SettingsItemKind::Folder,
            // tags the row as an editable network location. The category also
            // holds usb storage toggles and WebDAV, which the per-location
            // options menu must not offer itself on.
            NETWORK_LOCATION_ID
        });
    }

    items.emplace_back(MakeHeader("Source settings"_i18n));

    // usb mass storage is a file source like any other, so it is configured
    // here next to the network locations rather than under "Network".
    items.emplace_back(MakeBoolItem(
        "USB storage"_i18n,
        "Mount connected USB drives next to the microSD card. Shares the USB port with MTP, so turning this on turns MTP off."_i18n,
        App::GetHddEnable, App::SetHddEnable));

    items.emplace_back(MakeBoolItem(
        "USB storage read-only"_i18n,
        "Protect connected USB drives from changes. Turn this off to allow writing, renaming, deleting and installing to them."_i18n,
        App::GetWriteProtect, App::SetWriteProtect));

    // the drives plugged in right now, so the two toggles above can be seen to
    // have taken effect without leaving the screen.
    for (const auto& e : location::GetStdio(false)) {
        items.emplace_back(SettingsItem{
            e.name,
            e.write_protect() ? "Connected, mounted read-only."_i18n : "Connected, writable."_i18n,
            [](){ return std::string{}; },
            [](){},
        });
    }

    return items;
}


auto TestLocationConnection(const location::Entry& loc) -> Result {
    if (loc.IsSmb()) {
#ifdef BUILD_SMB2
        CSMB2FS test_smb(loc.url, "test_smb", "test_smb");
        return test_smb.CheckConnection() ? 0 : -1;
#else
        return -1;
#endif
    }
    if (loc.IsNfs()) {
        return devoptab::nfs::TestConnection(loc.url);
    }

    curl::Api api(CURL_LOCATION_TO_API(loc));
    curl::ProbeType type = curl::ProbeType::Webdav;
    if (loc.url.starts_with("ftp://") || loc.url.starts_with("ftps://") || loc.protocol == "ftp") {
        type = curl::ProbeType::Ftp;
    } else if (loc.protocol == "http") {
        type = curl::ProbeType::Http;
    }

    const auto result = curl::Probe(api, type);
    log_write("[SOURCE] connection probe for %s: success=%d code=%ld\n", loc.name.c_str(), result.success, result.code);
    return result.success ? 0 : -1;
}


namespace {

auto GetLocationProtocol(const location::Entry& loc) -> std::string {
    if (!loc.protocol.empty()) {
        return loc.protocol;
    }
    if (loc.url.starts_with("smb://")) {
        return "smb";
    }
    if (loc.url.starts_with("nfs://")) {
        return "nfs";
    }
    if (loc.url.starts_with("ftp://") || loc.url.starts_with("ftps://")) {
        return "ftp";
    }
    if (loc.url.starts_with("http://") || loc.url.starts_with("https://")) {
        return "webdav";
    }
    return "webdav";
}

auto GetLocationProtocolLabel(const std::string& protocol) -> std::string {
    if (protocol == "smb") {
        return "Samba (SMB)";
    }
    if (protocol == "nfs") {
        return "NFS";
    }
    if (protocol == "ftp") {
        return "FTP";
    }
    if (protocol == "http") {
        return "HTTP";
    }
    return "WebDAV";
}

void ChangeLocationProtocol(location::Entry& loc, const std::string& protocol) {
    std::string address;
    if (const auto scheme = loc.url.find("://"); scheme != std::string::npos) {
        address = loc.url.substr(scheme + 3);
    }

    loc.protocol = protocol;
    loc.bearer.clear();
    loc.pub_key.clear();
    loc.priv_key.clear();

    if (protocol == "smb") {
        loc.url = "smb://" + address;
        loc.port = 0;
    } else if (protocol == "nfs") {
        loc.url = "nfs://" + address;
        loc.port = 0;
        loc.user.clear();
        loc.pass.clear();
    } else if (protocol == "webdav") {
        loc.url = "webdav://" + address;
        loc.port = 0;
    } else if (protocol == "ftp") {
        loc.url = "ftp://" + address;
        loc.port = 21;
    } else {
        loc.url = "http://" + address;
        loc.port = 0;
        loc.user.clear();
        loc.pass.clear();
    }
}

// every field offers the console keyboard or a phone / PC browser: a server URL or a password
// is far easier to type there. The answer arrives later, so `done` saves by itself.
void AskText(const std::string& guide, const std::string& current, std::function<void(const std::string&)> done) {
    remote_input::Options opts{};
    opts.title = guide;
    opts.guide = guide;
    opts.default_text = current;
    remote_input::PromptTextInput(opts, [done](const std::string& text) {
        done(text);
    });
}


} // namespace

SourceEditMenu::SourceEditMenu(std::string name) : MenuBase{name, MenuFlag_None}, m_loc_name{name} {
    m_items = BuildEditItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

SourceEditMenu::~SourceEditMenu() = default;

void SourceEditMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    std::string item_label;
    if (!m_items.empty() && m_index >= 0 && m_index < m_items.size()) {
        item_label = m_items[m_index].label;
    }

    float saved_yoff = m_list->GetYoff();
    s64 saved_index = m_index;

    m_items = BuildEditItems();

    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });

    s64 new_index = (it == m_items.cend()) ? saved_index : std::distance(m_items.cbegin(), it);
    if (m_items.empty()) {
        m_index = 0;
        m_list->SetYoff(0.f);
        return;
    }
    new_index = std::clamp<s64>(new_index, 0, static_cast<s64>(m_items.size() - 1));
    SetIndex(new_index);
    m_list->SetYoff(saved_yoff);
}

void SourceEditMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void SourceEditMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void SourceEditMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

void SourceEditMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

std::vector<SettingsItem> SourceEditMenu::BuildEditItems() {
    std::vector<SettingsItem> items;
    auto network_locations = location::Load();
    auto it = std::find_if(network_locations.begin(), network_locations.end(), [&](const auto& e) {
        return e.name == m_loc_name;
    });
    if (it == network_locations.end()) {
        return items;
    }
    location::Entry loc = *it;

    const auto proto = GetLocationProtocol(loc);

    items.emplace_back(SettingsItem{
        "Name"_i18n,
        "The name of this source in the list of sources."_i18n,
        [loc](){ return loc.name; },
        [this, loc]() {
            AskText("Enter location name (e.g. My NAS)"_i18n, loc.name, [this, loc](const std::string& out) {
                if (out.empty() || out == loc.name) {
                    return;
                }
                if (std::ranges::any_of(location::Load(), [&](const auto& e){ return e.name == out; })) {
                    App::Push<OptionBox>("A source with this name already exists."_i18n, "OK"_i18n);
                    return;
                }
                location::Remove(loc.name);
                auto renamed = loc;
                renamed.name = out;
                location::Add(renamed);
                m_loc_name = out;
                SetTitle(out);
                m_items = BuildEditItems();
            });
        }
    });

    items.emplace_back(SettingsItem{
        "Protocol"_i18n,
        "Change the network protocol and edit the fields required by the new source type."_i18n,
        [proto](){ return GetLocationProtocolLabel(proto); },
        [this, proto]() {
            PopupList::Items protocols = {"Samba (SMB)", "NFS", "WebDAV", "FTP", "HTTP"};
            App::Push<PopupList>("Select Protocol"_i18n, protocols, [this, proto](auto op_index) {
                if (!op_index) {
                    return;
                }

                constexpr std::array protocol_values{"smb", "nfs", "webdav", "ftp", "http"};
                const auto selected = protocol_values[*op_index];
                if (proto == selected) {
                    return;
                }

                auto locations = location::Load();
                const auto entry = std::ranges::find_if(locations, [this](const auto& candidate) {
                    return candidate.name == m_loc_name;
                });
                if (entry == locations.end()) {
                    return;
                }

                auto new_loc = *entry;
                ChangeLocationProtocol(new_loc, selected);
                location::Remove(new_loc.name);
                location::Add(new_loc);
                if (new_loc.name == App::GetWebdavUrlName() && new_loc.protocol != "webdav") {
                    App::SetWebdavUrl("");
                }
                m_items = BuildEditItems();
                SetIndex(0);
                App::Notify("Source protocol changed."_i18n);
            });
        }
    });

    if (proto == "smb") {
        std::string server, share;
        if (loc.url.rfind("smb://", 0) == 0) {
            size_t host_start = 6;
            size_t slash_pos = loc.url.find('/', host_start);
            if (slash_pos == std::string::npos) {
                server = loc.url.substr(host_start);
                share = "";
            } else {
                server = loc.url.substr(host_start, slash_pos - host_start);
                share = loc.url.substr(slash_pos + 1);
            }
        }

        items.emplace_back(SettingsItem{
            "Server IP / Hostname"_i18n,
            "Samba server IP address or hostname."_i18n,
            [server](){ return server; },
            [this, loc, server, share]() {
                AskText("Enter server IP or hostname"_i18n, server, [this, loc, share](const std::string& out) {
                    location::Entry new_loc = loc;
                    new_loc.url = "smb://" + out + "/" + share;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                });
            }
        });

        items.emplace_back(SettingsItem{
            "Share Name"_i18n,
            "Samba shared folder name."_i18n,
            [share](){ return share; },
            [this, loc, server, share]() {
                AskText("Enter share name"_i18n, share, [this, loc, server](const std::string& out) {
                    location::Entry new_loc = loc;
                    new_loc.url = "smb://" + server + "/" + out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                });
            }
        });
    }
    else if (proto == "ftp") {
        std::string server;
        if (loc.url.rfind("ftp://", 0) == 0) {
            server = loc.url.substr(6);
            if (!server.empty() && server.back() == '/') {
                server.pop_back();
            }
        }

        items.emplace_back(SettingsItem{
            "Server IP / Hostname"_i18n,
            "FTP server IP address or hostname."_i18n,
            [server](){ return server; },
            [this, loc, server]() {
                AskText("Enter server IP or hostname"_i18n, server, [this, loc](const std::string& out) {
                    location::Entry new_loc = loc;
                    new_loc.url = "ftp://" + out + "/";
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                });
            }
        });

        items.emplace_back(SettingsItem{
            "Port"_i18n,
            "FTP port."_i18n,
            [loc](){ return std::to_string(loc.port); },
            [this, loc]() {
                AskText("Enter port"_i18n, std::to_string(loc.port), [this, loc](const std::string& out) {
                    location::Entry new_loc = loc;
                    const auto parsed = std::strtoul(out.c_str(), nullptr, 10);
                    if (parsed >= 1 && parsed <= 65535) {
                        new_loc.port = static_cast<u16>(parsed);
                        location::Add(new_loc);
                        m_items = BuildEditItems();
                    }
                });
            }
        });
    }
    else { // webdav / http
        items.emplace_back(SettingsItem{
            "Server URL"_i18n,
            "Server connection URL address."_i18n,
            [loc](){ return loc.url; },
            [this, loc]() {
                AskText("Enter Server URL"_i18n, loc.url, [this, loc](const std::string& out) {
                    location::Entry new_loc = loc;
                    new_loc.url = out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                });
            }
        });
    }

    if (proto != "http" && proto != "nfs") {
        items.emplace_back(SettingsItem{
            "Username"_i18n,
            "Username for network connection (optional)."_i18n,
            [loc](){ return loc.user; },
            [this, loc]() {
                AskText("Enter username"_i18n, loc.user, [this, loc](const std::string& out) {
                    location::Entry new_loc = loc;
                    new_loc.user = out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                });
            }
        });

        items.emplace_back(SettingsItem{
            "Password"_i18n,
            "Password for network connection (optional)."_i18n,
            [loc](){ return loc.pass.empty() ? "" : "********"; },
            [this, loc]() {
                AskText("Enter password"_i18n, loc.pass, [this, loc](const std::string& out) {
                    location::Entry new_loc = loc;
                    new_loc.pass = out;
                    location::Add(new_loc);
                    m_items = BuildEditItems();
                });
            }
        });
    }

    items.emplace_back(SettingsItem{
        "Test Connection"_i18n,
        "Test connection with current settings."_i18n,
        [](){ return std::string{}; },
        [loc]() mutable {
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
        }
    });

    return items;
}


} // namespace sphaira::ui::menu::settings
