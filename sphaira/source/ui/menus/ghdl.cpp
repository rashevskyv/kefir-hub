#include "ui/menus/ghdl_internal.hpp"
#include "path_util.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/sidebar.hpp"
#include "swkbd.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "log.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <string>
#include <memory>
#include <vector>

namespace sphaira::ui::menu::gh {

Menu::Menu(u32 flags) : MenuBase{"GitHub"_i18n, flags} {
    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);

    this->SetActions(
        std::make_pair(Button::A, Action{"Download"_i18n, [this](){
            if (m_entries.empty()) {
                return;
            }

            DownloadEntries(GetEntry());
        }}),

        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    const Vec4 v{75, GetY() + 1.f + 42.f, 1220.f-45.f*2, 60};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
}

Menu::~Menu() {
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
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

    const auto& text_col = theme->GetColour(ThemeEntryID_TEXT);

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_entries.size(), m_index, [this, text_col](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = m_entries[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_entries.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        nvgSave(vg);
        nvgIntersectScissor(vg, x + text_xoffset, y, w-(x+text_xoffset+50), h);
            gfx::drawTextArgs(vg, x + text_xoffset, y + (h / 2.f), 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s By %s", e.repo.c_str(), e.owner.c_str());
        nvgRestore(vg);

        if (!e.tag.empty()) {
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f), 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "version: %s", e.tag.c_str());
        }
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        Scan();
    }
}

void Menu::SetIndex(s64 index) {
    if (m_entries.empty()) {
        m_index = 0;
        SetTitleSubHeading("");
        UpdateSubheading();
        return;
    }

    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    SetTitleSubHeading(m_entries[m_index].json_path, true);
    UpdateSubheading();
}

void Menu::Scan() {
    m_entries.clear();

    // load from romfs first
    if (R_SUCCEEDED(romfsInit())) {
        LoadEntriesFromPath("romfs:/github/");
        romfsExit();
    }

    // then load custom entries
    LoadEntriesFromPath(paths::GITHUB);

    Sort();

    SetIndex(0);
}

void Menu::LoadEntriesFromPath(const fs::FsPath& path) {
    auto dir = opendir(path);
    if (!dir) {
        return;
    }
    ON_SCOPE_EXIT(closedir(dir));

    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        if (d->d_type != DT_REG) {
            continue;
        }

        const auto ext = std::strrchr(d->d_name, '.');
        if (!ext || strcasecmp(ext, ".json")) {
            continue;
        }

        Entry entry{};
        const auto full_path = fs::AppendPath(path, d->d_name);
        from_json(full_path, entry);

        // parse owner and repo from url if needed
        if (!entry.url.empty()) {
            if (auto repo = path::ParseGitHubRepoUrl(entry.url)) {
                entry.owner = repo->owner;
                entry.repo = repo->repo;
            } else if (entry.owner.empty() || entry.repo.empty()) {
                log_write("ignoring entry with invalid GitHub URL: %s in %s\n", entry.url.c_str(), full_path.s);
                continue;
            }
        }

        if (!entry.direct_url.empty()) {
            if (!path::IsValidDirectAssetUrl(entry.direct_url)) {
                log_write("ignoring entry with invalid direct URL: %s in %s\n", entry.direct_url.c_str(), full_path.s);
                continue;
            }
        }

        // check that we have a owner and repo, OR a direct_url
        if ((entry.owner.empty() || entry.repo.empty()) && entry.direct_url.empty()) {
            continue;
        }

        // For direct_url entries without owner/repo, use filename as display name
        if (!entry.direct_url.empty() && entry.repo.empty()) {
            // Extract filename from URL for display
            const auto basename = path::ExtractBasename(entry.direct_url);
            if (!basename.empty()) {
                entry.repo = std::string(basename);
                // Remove .zip extension for cleaner display
                if (path::EndsWithIC(entry.repo, ".zip")) {
                    entry.repo.resize(entry.repo.size() - 4);
                }
            } else {
                entry.repo = "Direct Link";
            }
            entry.owner = "Direct";
        }

        entry.json_path = full_path;
        m_entries.emplace_back(entry);
    }
}

void Menu::Sort() {
    const auto sorter = [this](Entry& lhs, Entry& rhs) -> bool {
        // handle fallback if multiple entries are added with the same name
        // used for forks of a project.
        // in the rare case of the user adding the same owner and repo,
        // fallback to the filepath, which *is* unqiue
        auto r = strcasecmp(lhs.repo.c_str(), rhs.repo.c_str());
        if (!r) {
            r = strcasecmp(lhs.owner.c_str(), rhs.owner.c_str());
            if (!r) {
                r = strcasecmp(lhs.json_path, rhs.json_path);
            }
        }
        return r < 0;
    };

    std::sort(m_entries.begin(), m_entries.end(), sorter);
}

void Menu::UpdateSubheading() {
    const auto index = m_entries.empty() ? 0 : m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_entries.size()));
}

void DownloadEntries(const Entry& entry) {
    // Handle direct URL entries differently - skip GitHub API
    if (!entry.direct_url.empty()) {
        DoDirectLinkDownload(entry.direct_url);
        return;
    }

    auto gh_entries = std::make_shared<std::vector<GhApiEntry>>();

    App::Push<ProgressBox>(0, "Downloading "_i18n, entry.repo, [entry, gh_entries](auto pbox) -> Result {
        return DownloadReleaseJsonJson(pbox, GenerateApiUrl(entry), *gh_entries);
    }, [entry, gh_entries](Result rc){
        if (rc == Result_TransferCancelled) {
            return;
        }
        App::PushErrorBox(rc, "Failed to download json"_i18n);
        if (R_FAILED(rc) || gh_entries->empty()) {
            return;
        }

        PopupList::Items entry_items;
        for (const auto& e : *gh_entries) {
            std::string str;
            if (!e.name.empty()) {
                str += e.name + "   |  ";
            } else {
                str += e.tag_name + "   |  ";
            }
            if (e.prerelease) {
                str += " (Pre-Release)";
            }
            str += " [" + e.published_at.substr(0, 10) + "]";

            entry_items.emplace_back(std::move(str));
        }

        if (entry_items.empty()) {
            return;
        }

        App::Push<PopupList>("Select release to download for "_i18n + entry.repo, entry_items, [entry, gh_entries](auto op_index){
            if (!op_index || *op_index < 0 || static_cast<size_t>(*op_index) >= gh_entries->size()) {
                return;
            }

            const auto& gh_entry = (*gh_entries)[*op_index];
            const auto& assets = entry.assets;
            PopupList::Items asset_items;
            std::vector<std::optional<AssetEntry>> matched_assets;
            std::vector<GhApiAsset> api_assets;
            bool using_name = false;

            for (const auto& p : gh_entry.assets) {
                std::optional<AssetEntry> matched;
                for (const auto& e : assets) {
                    if (!e.name.empty()) {
                        using_name = true;
                    }

                    if (!e.name.empty() && p.name.find(e.name) != std::string::npos) {
                        matched = e;
                        break;
                    }
                }

                if (!using_name || matched.has_value()) {
                    std::string str = p.name + "   |  ";
                    str += " [" + p.updated_at.substr(0, 10) + "]";

                    asset_items.emplace_back(std::move(str));
                    matched_assets.emplace_back(std::move(matched));
                    api_assets.emplace_back(p);
                }
            }

            if (asset_items.empty()) {
                App::Push<OptionBox>("No downloadable assets found."_i18n, "OK"_i18n);
                return;
            }

            App::Push<PopupList>("Select asset to download for "_i18n + entry.repo, asset_items, [entry, api_assets = std::move(api_assets), matched_assets = std::move(matched_assets)](auto op_index){
                if (!op_index || *op_index < 0 || static_cast<size_t>(*op_index) >= api_assets.size()) {
                    return;
                }

                const auto index = static_cast<size_t>(*op_index);
                const auto asset_entry = api_assets[index];
                const auto matched = matched_assets[index];
                auto pre_install_message = entry.pre_install_message;
                if (matched && !matched->pre_install_message.empty()) {
                    pre_install_message = matched->pre_install_message;
                }

                const auto func = [entry, asset_entry, matched](){
                    App::Push<ProgressBox>(0, "Downloading "_i18n, entry.repo, [entry, asset_entry, matched](auto pbox) -> Result {
                        return DownloadApp(pbox, asset_entry, matched ? &(*matched) : nullptr);
                    }, [entry, matched](Result rc){
                        if (rc == Result_TransferCancelled) {
                            return;
                        }
                        App::PushErrorBox(rc, "Failed to download app!"_i18n);

                        if (R_SUCCEEDED(rc)) {
                            homebrew::SignalChange();
                            App::Notify("Downloaded "_i18n + entry.repo);
                            auto post_install_message = entry.post_install_message;
                            if (matched && !matched->post_install_message.empty()) {
                                post_install_message = matched->post_install_message;
                            }

                            if (!post_install_message.empty()) {
                                App::Push<OptionBox>(post_install_message, "OK"_i18n);
                            }
                        }
                    });
                };

                if (!pre_install_message.empty()) {
                    App::Push<OptionBox>(
                        pre_install_message,
                        "Back"_i18n, "Download"_i18n, 1, [func](auto op_index){
                            if (op_index && *op_index) {
                                func();
                            }
                        }
                    );
                } else {
                    func();
                }
            });
        });
    });
}

bool Download(const std::string& url, const std::vector<AssetEntry>& assets, const std::string& pre_install_message, const std::string& post_install_message) {
    Entry entry{};
    entry.url = url;
    entry.assets = assets;
    entry.pre_install_message = pre_install_message;
    entry.post_install_message = post_install_message;

    // parse owner and repo from url
    if (!entry.url.empty()) {
        if (auto repo = path::ParseGitHubRepoUrl(entry.url)) {
            entry.owner = repo->owner;
            entry.repo = repo->repo;
        } else {
            return false;
        }
    }

    // check that we have an owner and repo
    if (entry.owner.empty() || entry.repo.empty()) {
        return false;
    }

    DownloadEntries(entry);
    return true;
}

void DownloadDirectLink() {
    OpenDirectLinkPrompt();
}

} // namespace sphaira::ui::menu::gh
