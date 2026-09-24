#include "ui/menus/users/users_nand_library.hpp"
#include "ui/menus/users/users_nand_library_internal.hpp"

#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/menus/users/users_restore_remote.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include <yyjson.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::users {

struct NandPackDetailMenu final : MenuBase {
    using RestoreCb = std::function<void(const std::string& dir, bool restore_play_hours)>;

    struct Entry {
        nand_transfer::PackUser user;
        int image{};
        bool avatar_tried{};
    };

    NandPackDetailMenu(nand_transfer::PackInfo pack, RestoreCb on_restore)
        : MenuBase{GetPackDisplayName(pack), MenuFlag_None}
        , m_pack{std::move(pack)}
        , m_on_restore{std::move(on_restore)}
    {
        for (auto& u : nand_transfer::ListPackUsers(m_pack.dir)) {
            Entry e;
            e.user = std::move(u);
            m_entries.push_back(std::move(e));
        }

        this->SetActions(
            std::make_pair(Button::A, Action{"Restore"_i18n, [this](){ ConfirmRestore(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
        );

        SetTitleSubHeading(
            "+ opens options. A restores this pack."_i18n, true);
        m_list = std::make_unique<List>(2, 4, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 555.f, 110.f}, Vec2{20.f, 15.f});
        UpdateSubHeading();
    }

    ~NandPackDetailMenu() {
        FreeImages();
    }

    auto GetShortTitle() const -> const char* override { return "Pack"; }

    void FreeImages() {
        auto* vg = App::GetVg();
        for (auto& e : m_entries) {
            if (e.image > 0 && vg) {
                nvgDeleteImage(vg, e.image);
                e.image = 0;
            }
        }
    }

    void UpdateSubHeading() {
        std::string sub = std::to_string(m_entries.size()) + " " + "accounts"_i18n;
        sub += m_pack.save_00F0 ? " - play hours yes"_i18n : " - play hours no"_i18n;
        SetSubHeading(sub);
    }

    static constexpr s64 GridSlotToEntry(s64 grid_idx) {
        return (grid_idx % 2) * 4 + (grid_idx / 2);
    }

    static constexpr s64 EntryToGridSlot(s64 entry_idx) {
        return (entry_idx % 4) * 2 + (entry_idx / 4);
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_entries.empty()) {
            return;
        }
        const auto old_index = m_index;
        m_list->OnUpdate(controller, touch, m_index, 8, [this](bool touch, auto i) {
            const auto entry_idx = GridSlotToEntry(i);
            if (entry_idx < static_cast<s64>(m_entries.size())) {
                if (touch && m_index == i) {
                    FireAction(Button::A);
                } else {
                    App::PlaySoundEffect(SoundEffect_Focus);
                    m_index = i;
                }
            }
        }, this);
        if (GridSlotToEntry(m_index) >= static_cast<s64>(m_entries.size())) {
            m_index = old_index;
        }
    }

    auto TryLoadAvatar(Entry& e) -> bool {
        if (e.avatar_tried) {
            return false;
        }
        e.avatar_tried = true;
        if (e.user.avatar_path.empty()) {
            return false;
        }
        std::vector<u8> jpeg;
        if (!nand_transfer::ReadPackUserAvatar(m_pack, e.user, jpeg) || jpeg.empty()) {
            return false;
        }
        auto img = ImageLoadFromMemory(jpeg, ImageFlag_JPEG);
        if (img.data.empty()) {
            img = ImageLoadIcon(jpeg);
        }
        if (img.data.empty()) {
            return false;
        }
        e.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
        return true;
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        if (m_entries.empty()) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 22.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", "No accounts found in this pack"_i18n.c_str());
            return;
        }
        int loaded = 0;
        m_list->Draw(vg, theme, 8, m_index, [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto entry_idx = GridSlotToEntry(i);
            if (entry_idx >= static_cast<s64>(m_entries.size())) {
                return;
            }
            auto& e = m_entries[entry_idx];
            if (loaded < 2 && TryLoadAvatar(e)) {
                loaded++;
            }
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }

            const float icon_size = 46.f;
            const float icon_x = v.x + 20.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                e.image > 0 ? e.image : App::GetDefaultImage(), 4);

            const float text_x = icon_x + icon_size + 14.f;
            const float text_w = std::max(0.f, v.x + v.w - text_x - 12.f);
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, text_w, v.h);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", e.user.nickname.c_str());
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", e.user.uid.c_str());
            nvgRestore(vg);
        });
    }

private:
    void ConfirmRestore() {
        PromptNandPackRestore(m_pack.dir, m_pack.save_00F0, m_on_restore, [this](){
            SetPop();
        });
    }

    void ShowContextMenu() {
        auto options = std::make_unique<Sidebar>(GetPackDisplayName(m_pack), Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryHeader>("ACTIONS"_i18n);

        auto restore_entry = options->Add<SidebarEntryCallback>("Restore"_i18n, [this](){
            ConfirmRestore();
        }, true, "Restore profiles & play hours from this pack to console."_i18n);
        restore_entry->SetIcon(ActionIcon::Save);

        auto restore_remote_entry = options->Add<SidebarEntryCallback>("Restore from another console"_i18n, [this](){
            OpenRemoteNandTransfer(NandLibraryMode::Restore, [this](const std::string& dir, bool hours){
                SetPop();
                if (m_on_restore) {
                    m_on_restore(dir, hours);
                }
            });
        }, true, "Download and restore a backup from another console over Console Transfer."_i18n);
        restore_remote_entry->SetIcon(ActionIcon::Move);

        auto send_entry = options->Add<SidebarEntryCallback>("Send to another console"_i18n, [](){
            menu::StartConsoleTransferShareNandBackups();
        }, true, "Share backup(s) with another console over Console Transfer."_i18n);
        send_entry->SetIcon(ActionIcon::Move);
    }

    nand_transfer::PackInfo m_pack;
    RestoreCb m_on_restore;
    std::vector<Entry> m_entries;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

void OpenNandPackDetail(const nand_transfer::PackInfo& pack, RestoreCb on_restore) {
    App::Push<NandPackDetailMenu>(pack, std::move(on_restore));
}
struct RemoteNandPack {
    std::string name;
    std::string remote_path;
    bool is_archive{};
    s64 size{};
};

void DownloadRemoteNandPacks(
    const std::string& base_url,
    const std::vector<RemoteNandPack>& packs,
    std::function<void(const std::string& last_pack_path)> on_complete)
{
    auto last_path = std::make_shared<std::string>();
    auto downloaded_count = std::make_shared<size_t>(0);
    const size_t total_count = packs.size();

    App::Push<ProgressBox>(0, "Downloading profiles & play hours backup..."_i18n, "",
        [base_url, packs, last_path, downloaded_count](auto pbox) -> Result {
            fs::FsNativeSd sd;
            const std::string root_dst = paths::DATA_ROOT + "/nand_transfer";
            R_TRY(sd.CreateDirectoryRecursively(root_dst.c_str()));

            std::string active_part_file;
            std::string active_staging_dir;
            bool success = false;

            ON_SCOPE_EXIT({
                if (!success) {
                    if (!active_part_file.empty()) {
                        sd.DeleteFile(active_part_file.c_str());
                    }
                    if (!active_staging_dir.empty()) {
                        sd.DeleteDirectoryRecursively(active_staging_dir.c_str());
                    }
                }
            });

            for (size_t i = 0; i < packs.size(); ++i) {
                if (pbox->ShouldExit()) return Result_TransferCancelled;
                const auto& pack = packs[i];

                std::string title = (packs.size() > 1)
                    ? ("[" + std::to_string(i + 1) + "/" + std::to_string(packs.size()) + "] " + pack.name)
                    : pack.name;
                pbox->NewTransfer(title);

                if (pack.is_archive) {
                    if (pack.size <= 0) {
                        return Result_FsInvalidType;
                    }

                    std::string stem = pack.name;
                    std::string ext = ".zip";
                    if (stem.ends_with(".kefir-nand.zip")) {
                        stem.erase(stem.size() - 15);
                        ext = ".kefir-nand.zip";
                    } else if (stem.ends_with(".zip")) {
                        stem.erase(stem.size() - 4);
                        ext = ".zip";
                    }

                    std::string target = root_dst + "/" + pack.name;
                    int suffix = 1;
                    while (sd.FileExists(target.c_str()) || sd.DirExists(target.c_str())) {
                        target = root_dst + "/" + stem + "_" + std::to_string(suffix++) + ext;
                    }

                    const std::string part_path = target + ".part";
                    active_part_file = part_path;

                    curl::Api dl_api;
                    dl_api.SetOption(curl::Url{base_url + "/download?path=" + curl::EscapeString(pack.remote_path)});
                    dl_api.SetOption(curl::Path{part_path});
                    dl_api.SetOption(curl::OnProgress{pbox->OnDownloadProgressCallback()});
                    const auto dl_res = curl::ToFile(dl_api);
                    if (pbox->ShouldExit()) return Result_TransferCancelled;
                    if (!dl_res.success) return Result_FsInvalidType;

                    fs::File file;
                    s64 written_size = 0;
                    if (R_FAILED(sd.OpenFile(part_path.c_str(), FsOpenMode_Read, &file)) ||
                        R_FAILED(file.GetSize(&written_size)) ||
                        written_size != pack.size) {
                        return Result_FsInvalidType;
                    }
                    file.Close();

                    R_TRY(sd.RenameFile(part_path.c_str(), target.c_str()));
                    active_part_file.clear();

                    if (!nand_transfer::IsPackArchive(target)) {
                        sd.DeleteFile(target.c_str());
                        return Result_FsInvalidType;
                    }

                    *last_path = target;
                    (*downloaded_count)++;
                } else {
                    const std::string manifest_url = base_url + "/list-recursive?path=" + curl::EscapeString(pack.remote_path);
                    curl::Api list_api;
                    list_api.SetOption(curl::Url{manifest_url});
                    list_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
                    const auto list_res = curl::ToMemory(list_api);
                    if (pbox->ShouldExit()) return Result_TransferCancelled;
                    if (!list_res.success || list_res.data.empty()) return Result_FsInvalidType;

                    const std::string manifest_json(list_res.data.begin(), list_res.data.end());
                    const auto files_opt = ParseManifestResponse(manifest_json, pack.remote_path);
                    if (!files_opt || files_opt->empty()) {
                        return Result_FsInvalidType;
                    }
                    const auto& files = *files_opt;

                    std::string safe_name = pack.name;
                    while (!safe_name.empty() && (safe_name.back() == '/' || safe_name.back() == '\\')) {
                        safe_name.pop_back();
                    }
                    if (const auto slash = safe_name.find_last_of("/\\"); slash != std::string::npos) {
                        safe_name.erase(0, slash + 1);
                    }
                    if (safe_name.empty()) {
                        safe_name = "nand_pack";
                    }

                    std::string staging_dir = root_dst + "/_staging_" + safe_name + "_" + std::to_string(i);
                    int stg_suffix = 1;
                    while (sd.DirExists(staging_dir.c_str()) || sd.FileExists(staging_dir.c_str())) {
                        staging_dir = root_dst + "/_staging_" + safe_name + "_" + std::to_string(i) + "_" + std::to_string(stg_suffix++);
                    }

                    R_TRY(sd.CreateDirectoryRecursively(staging_dir.c_str()));
                    active_staging_dir = staging_dir;

                    for (const auto& f : files) {
                        if (pbox->ShouldExit()) return Result_TransferCancelled;
                        if (f.size < 0) return Result_FsInvalidType;

                        const std::string dest_path = staging_dir + "/" + f.rel_path;
                        R_TRY(sd.CreateDirectoryRecursivelyWithPath(dest_path.c_str()));

                        pbox->NewTransfer(title + ": " + f.rel_path);

                        curl::Api dl_api;
                        dl_api.SetOption(curl::Url{base_url + "/download?path=" + curl::EscapeString(f.remote_path)});
                        dl_api.SetOption(curl::Path{dest_path});
                        dl_api.SetOption(curl::OnProgress{pbox->OnDownloadProgressCallback()});
                        const auto dl_res = curl::ToFile(dl_api);
                        if (pbox->ShouldExit()) return Result_TransferCancelled;
                        if (!dl_res.success) return Result_FsInvalidType;

                        fs::File file;
                        s64 written_size = 0;
                        if (R_FAILED(sd.OpenFile(dest_path.c_str(), FsOpenMode_Read, &file)) ||
                            R_FAILED(file.GetSize(&written_size)) ||
                            written_size != f.size) {
                            return Result_FsInvalidType;
                        }
                        file.Close();
                    }

                    if (!nand_transfer::IsPack(staging_dir)) {
                        return Result_FsInvalidType;
                    }

                    std::string target_dir = root_dst + "/" + safe_name;
                    int suffix = 1;
                    while (sd.DirExists(target_dir.c_str()) || sd.FileExists(target_dir.c_str())) {
                        target_dir = root_dst + "/" + safe_name + "_" + std::to_string(suffix++);
                    }

                    R_TRY(sd.RenameDirectory(staging_dir.c_str(), target_dir.c_str()));
                    active_staging_dir.clear();

                    if (!nand_transfer::IsPack(target_dir)) {
                        sd.DeleteDirectoryRecursively(target_dir.c_str());
                        return Result_FsInvalidType;
                    }

                    *last_path = target_dir;
                    (*downloaded_count)++;
                }
            }
            success = true;
            R_SUCCEED();
        },
        [on_complete, last_path, downloaded_count, total_count](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_FAILED(rc) || *downloaded_count == 0 || *downloaded_count != total_count) {
                App::Push<OptionBox>("Failed to download backup files from the sending console."_i18n, "OK"_i18n);
                return;
            }
            if (on_complete) {
                on_complete(*last_path);
            }
        }
    );
}

void OpenRemoteNandTransfer(
    NandLibraryMode mode,
    std::function<void(const std::string& dir, bool restore_play_hours)> on_restore,
    std::function<void()> on_refresh)
{
    ConnectConsoleTransfer([mode, on_restore, on_refresh](const std::string& base_url) {
        auto remote_packs = std::make_shared<std::vector<RemoteNandPack>>();
        App::Push<ProgressBox>(0, "Fetching backup list..."_i18n, "",
            [base_url, remote_packs](auto pbox) -> Result {
                curl::Api api;
                api.SetOption(curl::Url{base_url + "/list"});
                api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
                const auto res = curl::ToMemory(api);
                if (pbox->ShouldExit()) return Result_TransferCancelled;
                if (!res.success || res.data.empty()) return Result_FsInvalidType;

                yyjson_doc* doc = yyjson_read((const char*)res.data.data(), res.data.size(), 0);
                if (!doc) return Result_FsInvalidType;
                ON_SCOPE_EXIT(yyjson_doc_free(doc));

                yyjson_val* root = yyjson_doc_get_root(doc);
                yyjson_val* path_val = yyjson_obj_get(root, "path");
                std::string root_path = yyjson_get_str(path_val) ? yyjson_get_str(path_val) : "";
                if (!root_path.empty() && root_path.back() != '/') root_path += '/';

                yyjson_val* entries = yyjson_obj_get(root, "entries");
                if (!yyjson_is_arr(entries)) return Result_FsInvalidType;

                size_t idx, max;
                yyjson_val* item;
                yyjson_arr_foreach(entries, idx, max, item) {
                    const char* name = yyjson_get_str(yyjson_obj_get(item, "name"));
                    if (!name || !*name || std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
                    std::string_view sv{name};
                    if (sv.starts_with(".") || sv.starts_with("_") || sv.ends_with(".part") || sv.ends_with(".te")) continue;

                    int type = yyjson_get_int(yyjson_obj_get(item, "type"));
                    s64 size = (s64)yyjson_get_sint(yyjson_obj_get(item, "size"));
                    bool is_dir = (type == (int)FsDirEntryType_Dir);
                    bool is_archive = (type == (int)FsDirEntryType_File) && (sv.ends_with(".kefir-nand.zip") || sv.ends_with(".zip"));

                    if (is_dir || is_archive) {
                        remote_packs->push_back({
                            .name = name,
                            .remote_path = root_path + name,
                            .is_archive = is_archive,
                            .size = is_archive ? size : 0,
                        });
                    }
                }
                std::sort(remote_packs->begin(), remote_packs->end(), [](const auto& a, const auto& b) { return a.name > b.name; });
                R_SUCCEED();
            },
            [base_url, remote_packs, mode, on_restore, on_refresh](Result rc) {
                if (rc == Result_TransferCancelled) return;
                if (R_FAILED(rc)) {
                    App::Push<OptionBox>("Could not retrieve the profiles & play hours backup list from the sending console."_i18n, "OK"_i18n);
                    return;
                }
                if (remote_packs->empty()) {
                    App::Push<OptionBox>("No profiles & play hours backups found on the sending console."_i18n, "OK"_i18n);
                    return;
                }

                PopupList::Items items;
                if (mode == NandLibraryMode::Manage && remote_packs->size() > 1) {
                    items.push_back("[Receive All Backups]"_i18n);
                }
                for (const auto& p : *remote_packs) {
                    std::string label = p.name;
                    if (p.is_archive && p.size > 0) {
                        char sz[32]{};
                        if (p.size >= 1024 * 1024) {
                            std::snprintf(sz, sizeof(sz), " (%.1f MiB)", (double)p.size / (1024.0 * 1024.0));
                        } else {
                            std::snprintf(sz, sizeof(sz), " (%lld KiB)", (long long)(p.size / 1024));
                        }
                        label += sz;
                    }
                    items.push_back(std::move(label));
                }

                const std::string title = (mode == NandLibraryMode::Restore)
                    ? "Restore from another console"_i18n
                    : "Receive from another console"_i18n;

                auto popup = std::make_unique<PopupList>(title, items, [base_url, remote_packs, mode, on_restore, on_refresh](std::optional<s64> op) {
                    if (!op || *op < 0) return;
                    const size_t choice = static_cast<size_t>(*op);

                    std::vector<RemoteNandPack> to_download;
                    const bool has_all = (mode == NandLibraryMode::Manage && remote_packs->size() > 1);
                    if (has_all && choice == 0) {
                        to_download = *remote_packs;
                    } else {
                        const size_t pack_idx = has_all ? (choice - 1) : choice;
                        if (pack_idx < remote_packs->size()) {
                            to_download.push_back((*remote_packs)[pack_idx]);
                        }
                    }
                    if (to_download.empty()) return;

                    DownloadRemoteNandPacks(base_url, to_download, [mode, on_restore, on_refresh](const std::string& last_path) {
                        if (mode == NandLibraryMode::Restore) {
                            std::optional<nand_transfer::PackInfo> matched_pack;
                            for (const auto& p : nand_transfer::ListPacks()) {
                                if (p.dir == last_path) {
                                    matched_pack = p;
                                    break;
                                }
                            }
                            if (!matched_pack) {
                                App::Push<OptionBox>("Downloaded backup is incomplete or invalid."_i18n, "OK"_i18n);
                                return;
                            }
                            PromptNandPackRestore(matched_pack->dir, matched_pack->save_00F0, on_restore);
                        } else {
                            App::Push<OptionBox>("Backup received successfully."_i18n, "OK"_i18n);
                            if (on_refresh) on_refresh();
                        }
                    });
                });
                popup->SetRemoteMarkers(std::vector<bool>(items.size(), true));
                App::Push(std::move(popup));
            }
        );
    });
}

} // namespace sphaira::ui::menu::users
