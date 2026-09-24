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
#include "nro.hpp"
#include "nacp_util.hpp"
#include "web.hpp"
#include "ui/menus/homebrew.hpp"
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::appstore {
EntryMenu::EntryMenu(Entry& entry, const LazyImage& default_icon, Menu& menu)
: MenuBase{entry.title, MenuFlag_None}
, m_entry{entry}
, m_default_icon{default_icon}
, m_menu{menu} {
    this->SetActions(
        std::make_pair(Button::DPAD_DOWN | Button::RS_DOWN, Action{[this](){
            if (m_index < (m_options.size() - 1)) {
                SetIndex(m_index + 1);
                App::PlaySoundEffect(SoundEffect_Focus);
            }
        }}),
        std::make_pair(Button::DPAD_UP | Button::RS_UP, Action{[this](){
            if (m_index != 0) {
                SetIndex(m_index - 1);
                App::PlaySoundEffect(SoundEffect_Focus);
            }
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            options->Add<SidebarEntryCallback>("More by Author"_i18n, [this](){
                m_menu.SetAuthor();
                SetPop();
            }, true, "Browse all apps published by this author."_i18n);

            options->Add<SidebarEntryCallback>("Leave Feedback"_i18n, [this](){
                std::string out;
                if (R_SUCCEEDED(swkbd::ShowText(out)) && !out.empty()) {
                    const auto post = "name=" "switch_user" "&package=" + m_entry.name + "&message=" + out;
                    const auto file = BuildFeedbackCachePath(m_entry);

                    curl::Api().ToAsync(
                        curl::Url{URL_POST_FEEDBACK},
                        curl::Path{file},
                        curl::Fields{post},
                        curl::StopToken{this->GetToken()},
                        curl::OnComplete{[](auto& result){
                            if (result.success) {
                                log_write("got feedback!\n");
                            } else {
                                log_write("failed to send feedback :(");
                            }
                        }
                    });
                }
            }, true, "Send feedback or a comment about this application."_i18n);

            if (App::IsApplication() && !m_entry.url.empty()) {
                options->Add<SidebarEntryCallback>("Visit Website"_i18n, [this](){
                    WebShow(m_entry.url);
                }, "Open the author's website for this application."_i18n);
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::L2, Action{"Files"_i18n, [this](){
            m_show_file_list ^= 1;

            if (m_show_file_list && !m_manifest_list && m_file_list_state == ImageDownloadState::None) {
                m_file_list_state = ImageDownloadState::Progress;
                const auto path = BuildManifestCachePath(m_entry);
                std::vector<u8> data;

                if (R_SUCCEEDED(fs::read_entire_file(path, data))) {
                    m_file_list_state = ImageDownloadState::Done;
                    data.push_back('\0');
                    m_manifest_list = std::make_unique<ScrollableText>((const char*)data.data(), 0, 374, 250, 768, 18);
                } else {
                    curl::Api().ToMemoryAsync(
                        curl::Url{BuildManifestUrl(m_entry)},
                        curl::StopToken{this->GetToken()},
                        curl::OnComplete{[this](auto& result){
                            if (result.success) {
                                m_file_list_state = ImageDownloadState::Done;
                                result.data.push_back('\0');
                                m_manifest_list = std::make_unique<ScrollableText>((const char*)result.data.data(), 0, 374, 250, 768, 18);
                            } else {
                                m_file_list_state = ImageDownloadState::Failed;
                            }
                        }}
                    );
                }
            }
        }})
    );

    SetTitleSubHeading("by " + m_entry.author, true);

    m_details = std::make_unique<ScrollableText>(m_entry.details, 0, 374, 250, 768, 18);
    m_changelog = std::make_unique<ScrollableText>(m_entry.changelog, 0, 374, 250, 768, 18);

    m_show_changlog ^= 1;
    ShowChangelogAction();

    const auto path = BuildBannerCachePath(m_entry);
    const auto url = BuildBannerUrl(m_entry);
    m_banner.cached = EntryLoadImageFile(path, m_banner);

    // race condition if we pop the widget before the download completes
    curl::Api().ToFileAsync(
        curl::Url{url},
        curl::Path{path},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, path](auto& result){
            if (result.success) {
                if (result.code == 304) {
                    m_banner.cached = false;
                } else {
                    EntryLoadImageFile(path, m_banner);
                }
            }
        }
    });

    SetTitleSubHeading(m_entry.description, true);
    SetSubHeading("");

    ReadFromInfoJson(m_entry);
    if (m_entry.installed_version.empty() && !m_entry.binary.empty()) {
        fs::FsNativeSd fs;
        if (fs.FileExists(m_entry.binary)) {
            NacpStruct nacp;
            if (R_SUCCEEDED(nro_get_nacp(m_entry.binary, nacp))) {
                m_entry.installed_version = nacp_util::GetDisplayVersion(nacp);
            }
            if (m_entry.installed_version.empty()) {
                m_entry.installed_version = "Installed";
            }
        }
    }

    UpdateOptions();

    // todo: see Draw()
    // const Vec4 v{75, 110, 370, 155};
    // const Vec2 pad{10, 10};
    // m_list = std::make_unique<List>(3, 3, v, pad);
}

EntryMenu::~EntryMenu() {
}

void EntryMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_show_file_list) {
        if (m_manifest_list) {
            m_manifest_list->Update(controller, touch);
        }
    } else {
        m_detail_changelog->Update(controller, touch);
    }
}

void EntryMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    constexpr Vec4 line_vec(30, 86, 1220, 646);
    constexpr Vec4 banner_vec(70, line_vec.y + 20, 848.f, 208.f);
    constexpr Vec4 icon_vec(968, line_vec.y + 30, 256, 150);
    constexpr Vec4 grid_vec(icon_vec.x - 50, line_vec.y + 1, line_vec.w, line_vec.h - line_vec.y - 1);

    // nvgSave(vg);
    // nvgScissor(vg, line_vec.x, line_vec.y, line_vec.w - line_vec.x, line_vec.h - line_vec.y); // clip
    // ON_SCOPE_EXIT(nvgRestore(vg));

    gfx::drawRect(vg, grid_vec, theme->GetColour(ThemeEntryID_GRID));
    DrawIcon(vg, m_banner, m_entry.image.image ? m_entry.image : m_default_icon, banner_vec, false);
    DrawIcon(vg, m_entry.image, m_default_icon, icon_vec);

    constexpr float text_start_x = icon_vec.x;
    float text_start_y = 276.f;
    const float text_inc_y = 26.f;
    const float font_size = 18.f;

    gfx::drawTextArgs(vg, text_start_x, text_start_y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "version: %s"_i18n.c_str(), m_entry.version.c_str());
    text_start_y += text_inc_y;
    if (!m_entry.installed_version.empty()) {
        const auto color = (m_entry.status == EntryStatus::Update) ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : theme->GetColour(ThemeEntryID_TEXT);
        gfx::drawTextArgs(vg, text_start_x, text_start_y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, color, "installed: %s"_i18n.c_str(), m_entry.installed_version.c_str());
        text_start_y += text_inc_y;
    }
    gfx::drawTextArgs(vg, text_start_x, text_start_y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "updated: %s"_i18n.c_str(), m_entry.updated.c_str());
    text_start_y += text_inc_y;
    gfx::drawTextArgs(vg, text_start_x, text_start_y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "category: %s"_i18n.c_str(), m_entry.category.c_str());
    text_start_y += text_inc_y;
    gfx::drawTextArgs(vg, text_start_x, text_start_y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "extracted: %.2f MiB"_i18n.c_str(), (double)m_entry.extracted / 1024.0);
    text_start_y += text_inc_y;
    gfx::drawTextArgs(vg, text_start_x, text_start_y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "app_dls: %s"_i18n.c_str(), AppDlToStr(m_entry.app_dls).c_str());

    // Action buttons anchored cleanly above the footer line (Y=646)
    constexpr float block_w = 256.f;
    constexpr float block_h = 50.f;
    constexpr float block_gap = 14.f;
    const float block_x = icon_vec.x;
    const float bottom_y = 630.f;

    for (size_t i = 0; i < m_options.size(); i++) {
        const float opt_y = bottom_y - ((m_options.size() - i) * block_h + (m_options.size() - 1 - i) * block_gap);
        const auto& option = m_options[i];
        auto text_id = ThemeEntryID_TEXT;
        if (m_index == (s32)i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, Vec4{block_x, opt_y, block_w, block_h});
        }

        gfx::drawTextArgs(vg, block_x + block_w / 2.f, opt_y + block_h / 2.f, 22.f, NVG_ALIGN_MIDDLE | NVG_ALIGN_CENTER, theme->GetColour(text_id), option.display_text.c_str());
    }

    if (m_show_file_list) {
        if (m_manifest_list) {
            m_manifest_list->Draw(vg, theme);
        } else if (m_file_list_state == ImageDownloadState::Progress) {
            gfx::drawText(vg, 110, 374, 18, theme->GetColour(ThemeEntryID_TEXT), "Loading..."_i18n.c_str());
        } else if (m_file_list_state == ImageDownloadState::Failed) {
            gfx::drawText(vg, 110, 374, 18, theme->GetColour(ThemeEntryID_TEXT), "Failed to download manifest"_i18n.c_str());
        }
    } else {
        m_detail_changelog->Draw(vg, theme);
    }
}

void EntryMenu::ShowChangelogAction() {
    std::function<void()> func = std::bind(&EntryMenu::ShowChangelogAction, this);
    m_show_changlog ^= 1;
    m_show_file_list = false;

    if (m_show_changlog) {
        SetAction(Button::L, Action{"Details"_i18n, func});
        m_detail_changelog = m_changelog.get();
    } else {
        SetAction(Button::L, Action{"Changelog"_i18n, func});
        m_detail_changelog = m_details.get();
    }
}

void EntryMenu::UpdateOptions() {
    const auto launch = [this](){
        nro_launch(m_entry.binary);
    };

    const auto install = [this](){
        App::Push<ProgressBox>(m_entry.image.image, "Downloading "_i18n, m_entry.title, [this](auto pbox){
            return InstallApp(pbox, m_entry);
        }, [this](Result rc){
            homebrew::SignalChange();
            if (rc == Result_TransferCancelled) {
                App::Push<OptionBox>("Download was cancelled."_i18n, "OK"_i18n);
                return;
            }
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to download application"_i18n);
                return;
            }

            App::Notify("Downloaded "_i18n + m_entry.title);
            m_entry.status = EntryStatus::Installed;
            if (IsRetroArchPackage(m_entry)) {
                m_entry.installed_version = "Nightly";
            } else {
                m_entry.installed_version = m_entry.version;
            }
            m_menu.SetDirty();
            UpdateOptions();
        });
    };

    const auto uninstall = [this](){
        App::Push<ProgressBox>(m_entry.image.image, "Uninstalling "_i18n, m_entry.title, [this](auto pbox){
            return UninstallApp(pbox, m_entry);
        }, [this](Result rc){
            homebrew::SignalChange();
            if (rc == Result_TransferCancelled) {
                App::Push<OptionBox>("Uninstall was cancelled."_i18n, "OK"_i18n);
                return;
            }
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to uninstall application"_i18n);
                return;
            }

            App::Notify("Removed "_i18n + m_entry.title);
            m_entry.status = EntryStatus::Get;
            m_entry.installed_version.clear();
            m_menu.SetDirty();
            UpdateOptions();
        });
    };

    const Option install_option{"Install"_i18n, install};
    const Option update_option{"Update"_i18n, install};
    const Option launch_option{"Launch"_i18n, "Launch "_i18n + m_entry.title + '?', launch};
    const Option remove_option{"Remove"_i18n, "Completely remove "_i18n + m_entry.title + '?', uninstall};

    m_options.clear();
    switch (m_entry.status) {
        case EntryStatus::Get:
            m_options.emplace_back(install_option);
            break;
        case EntryStatus::Installed:
            if (!m_entry.binary.empty() && m_entry.binary != "none") {
                if (!IsRetroArchPackage(m_entry) || m_entry.installed_version == "Nightly") {
                    m_options.emplace_back(launch_option);
                } else {
                    m_options.emplace_back(update_option);
                }
            }
            m_options.emplace_back(remove_option);
            break;
        case EntryStatus::Local:
            if (!m_entry.binary.empty() && m_entry.binary != "none") {
                if (!IsRetroArchPackage(m_entry)) {
                    m_options.emplace_back(launch_option);
                }
            }
            m_options.emplace_back(update_option);
            break;
        case EntryStatus::Update:
            m_options.emplace_back(update_option);
            m_options.emplace_back(remove_option);
            break;
    }

    SetIndex(0);
}

void EntryMenu::SetIndex(s64 index) {
    m_index = index;
    const auto option = m_options[m_index];
    if (option.confirm_text.empty()) {
        SetAction(Button::A, Action{option.display_text, option.func});
    } else {
        SetAction(Button::A, Action{option.display_text, [this, option](){
            App::Push<OptionBox>(option.confirm_text, "No"_i18n, "Yes"_i18n, 1, [this, option](auto op_index){
                if (op_index && *op_index) {
                    option.func();
                }
            });
        }});
    }
}


} // namespace sphaira::ui::menu::appstore
