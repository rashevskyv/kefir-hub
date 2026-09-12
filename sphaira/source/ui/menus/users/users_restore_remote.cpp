#include "ui/menus/users/users_restore_remote.hpp"

#include "account/account_user.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "log.hpp"
#include "net.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::users {

namespace {

void SkipJsonWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
        pos++;
    }
}

auto ParseJsonString(const std::string& s, size_t& pos) -> std::optional<std::string> {
    SkipJsonWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '"') {
        return std::nullopt;
    }
    pos++;
    std::string out;
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') {
            return out;
        }
        if (c == '\\') {
            if (pos >= s.size()) {
                return std::nullopt;
            }
            char esc = s[pos++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += esc; break;
            }
        } else {
            out += c;
        }
    }
    return std::nullopt;
}

auto ParseJsonInt(const std::string& s, size_t& pos) -> std::optional<s64> {
    SkipJsonWhitespace(s, pos);
    if (pos >= s.size()) {
        return std::nullopt;
    }
    size_t start = pos;
    if (s[pos] == '-' || s[pos] == '+') {
        pos++;
    }
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) {
        return std::nullopt;
    }
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        pos++;
    }
    const auto num_str = s.substr(start, pos - start);
    char* endptr = nullptr;
    const auto val = std::strtoll(num_str.c_str(), &endptr, 10);
    if (endptr == num_str.c_str()) {
        return std::nullopt;
    }
    return val;
}

} // namespace

auto ParseRemoteListResponse(const std::string& json) -> std::optional<std::pair<std::string, std::vector<RemotePackEntry>>> {
    size_t path_pos = json.find("\"path\"");
    if (path_pos == std::string::npos) {
        return std::nullopt;
    }
    size_t colon_pos = json.find(':', path_pos + 6);
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    colon_pos++;
    auto root_path_opt = ParseJsonString(json, colon_pos);
    if (!root_path_opt) {
        return std::nullopt;
    }
    std::string root_path = *root_path_opt;

    size_t entries_pos = json.find("\"entries\"", colon_pos);
    if (entries_pos == std::string::npos) {
        entries_pos = json.find("\"entries\"");
        if (entries_pos == std::string::npos) {
            return std::nullopt;
        }
    }
    size_t array_open = json.find('[', entries_pos);
    if (array_open == std::string::npos) {
        return std::nullopt;
    }

    std::vector<RemotePackEntry> out_entries;
    size_t pos = array_open + 1;
    while (pos < json.size()) {
        SkipJsonWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        if (json[pos] != '{') {
            pos++;
            continue;
        }
        pos++;

        std::string entry_name;
        int entry_type = -1;
        s64 entry_size = 0;
        while (pos < json.size() && json[pos] != '}') {
            SkipJsonWhitespace(json, pos);
            if (pos >= json.size() || json[pos] == '}') {
                break;
            }
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            auto key_opt = ParseJsonString(json, pos);
            if (!key_opt) {
                break;
            }
            SkipJsonWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ':') {
                pos++;
            }
            SkipJsonWhitespace(json, pos);
            if (*key_opt == "name") {
                auto name_val = ParseJsonString(json, pos);
                if (name_val) {
                    entry_name = *name_val;
                }
            } else if (*key_opt == "type") {
                auto type_val = ParseJsonInt(json, pos);
                if (type_val) {
                    entry_type = static_cast<int>(*type_val);
                }
            } else if (*key_opt == "size") {
                auto size_val = ParseJsonInt(json, pos);
                if (size_val && *size_val >= 0) {
                    entry_size = *size_val;
                }
            } else if (pos < json.size() && json[pos] == '"') {
                ParseJsonString(json, pos);
            } else {
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
                    pos++;
                }
            }
        }
        if (pos < json.size() && json[pos] == '}') {
            pos++;
        }

        if (entry_type == static_cast<int>(FsDirEntryType_Dir) && !entry_name.empty()) {
            if (entry_name != "." && entry_name != ".." &&
                entry_name.find('/') == std::string::npos &&
                entry_name.find('\\') == std::string::npos) {
                std::string rem_path = root_path;
                if (!rem_path.empty() && rem_path.back() != '/') {
                    rem_path += '/';
                }
                rem_path += entry_name;
                out_entries.push_back({entry_name, rem_path, false, 0});
            }
        } else if (entry_type == static_cast<int>(FsDirEntryType_File) && !entry_name.empty()) {
            std::string_view name_view{entry_name};
            if (name_view.ends_with(".kefir-user.zip")) {
                if (entry_name.find('/') == std::string::npos &&
                    entry_name.find('\\') == std::string::npos) {
                    std::string rem_path = root_path;
                    if (!rem_path.empty() && rem_path.back() != '/') {
                        rem_path += '/';
                    }
                    rem_path += entry_name;
                    out_entries.push_back({entry_name, rem_path, true, entry_size});
                }
            }
        }
    }

    std::sort(out_entries.begin(), out_entries.end(), [](const auto& a, const auto& b) {
        return a.name > b.name;
    });

    return std::make_pair(root_path, out_entries);
}

auto ParseManifestResponse(const std::string& json, const std::string& remote_pack_root) -> std::optional<std::vector<ManifestFile>> {
    size_t array_open = json.find('[');
    if (array_open == std::string::npos) {
        return std::nullopt;
    }

    std::string root_prefix = remote_pack_root;
    if (!root_prefix.empty() && root_prefix.back() != '/') {
        root_prefix += '/';
    }

    std::vector<ManifestFile> files;
    size_t pos = array_open + 1;
    while (pos < json.size()) {
        SkipJsonWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        if (json[pos] != '{') {
            pos++;
            continue;
        }
        pos++;

        std::string file_path;
        std::optional<s64> file_size;

        while (pos < json.size() && json[pos] != '}') {
            SkipJsonWhitespace(json, pos);
            if (pos >= json.size() || json[pos] == '}') {
                break;
            }
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            auto key_opt = ParseJsonString(json, pos);
            if (!key_opt) {
                break;
            }
            SkipJsonWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ':') {
                pos++;
            }
            SkipJsonWhitespace(json, pos);
            if (*key_opt == "path") {
                auto path_val = ParseJsonString(json, pos);
                if (path_val) {
                    file_path = *path_val;
                }
            } else if (*key_opt == "size") {
                auto size_val = ParseJsonInt(json, pos);
                if (size_val && *size_val >= 0) {
                    file_size = *size_val;
                }
            } else if (pos < json.size() && json[pos] == '"') {
                ParseJsonString(json, pos);
            } else {
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
                    pos++;
                }
            }
        }
        if (pos < json.size() && json[pos] == '}') {
            pos++;
        }

        if (file_path.empty() || !file_size.has_value() || *file_size < 0) {
            return std::nullopt;
        }

        if (!file_path.starts_with(root_prefix)) {
            log_write("[USER_TRANSFER] Manifest path %s does not start with root %s\n",
                file_path.c_str(), root_prefix.c_str());
            return std::nullopt;
        }

        std::string rel = file_path.substr(root_prefix.size());
        if (rel.empty() || rel.front() == '/' || rel.back() == '/' ||
            rel.find('\\') != std::string::npos || rel.find(':') != std::string::npos) {
            log_write("[USER_TRANSFER] Invalid relative path: %s\n", rel.c_str());
            return std::nullopt;
        }

        size_t start = 0;
        bool valid_comps = true;
        while (start < rel.size()) {
            size_t slash = rel.find('/', start);
            std::string comp = (slash == std::string::npos) ? rel.substr(start) : rel.substr(start, slash - start);
            if (comp.empty() || comp == "." || comp == "..") {
                valid_comps = false;
                break;
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        if (!valid_comps) {
            log_write("[USER_TRANSFER] Invalid relative component in: %s\n", rel.c_str());
            return std::nullopt;
        }

        files.push_back({file_path, rel, *file_size});
    }

    if (files.empty()) {
        return std::nullopt;
    }

    return files;
}

namespace {

struct RemoteUserPacksMenu final : MenuBase {
    using Callback = std::function<void(std::vector<account_user::Pack>)>;

    using Entry = RemoteUserPacksEntry;

    RemoteUserPacksMenu(std::string base_url, std::vector<Entry> entries, Callback on_restore)
        : MenuBase{"User Backups on Other Console"_i18n, MenuFlag_None}
        , m_base_url{std::move(base_url)}
        , m_entries{std::move(entries)}
        , m_on_restore{std::move(on_restore)}
    {
        this->SetActions(
            std::make_pair(Button::A, Action{"Restore"_i18n, [this](){ OnRestore(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::X, Action{"Select"_i18n, [this](){
                if (m_entries.empty()) {
                    return;
                }
                m_entries[m_index].selected ^= 1;
                m_selected_count += m_entries[m_index].selected ? 1 : -1;
                if (m_index + 1 < static_cast<s64>(m_entries.size())) {
                    m_index++;
                    m_list->EnsureVisible(m_index, m_entries.size());
                }
                UpdateSubHeading();
            }}),
            std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
                m_selected_count = 0;
                for (auto& e : m_entries) {
                    e.selected ^= 1;
                    if (e.selected) {
                        m_selected_count++;
                    }
                }
                UpdateSubHeading();
            }})
        );

        SetTitleSubHeading("X marks backups to restore. A restores the selected profiles."_i18n, true);
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        UpdateSubHeading();
    }

    ~RemoteUserPacksMenu() {
        auto* vg = App::GetVg();
        for (auto& e : m_entries) {
            if (e.image > 0 && vg) {
                nvgDeleteImage(vg, e.image);
                e.image = 0;
            }
        }
    }

    auto GetShortTitle() const -> const char* override { return "Remote Backups"; }

    void UpdateSubHeading() {
        if (m_entries.empty()) {
            SetSubHeading("0");
        } else if (m_selected_count > 0) {
            SetSubHeading(std::to_string(m_selected_count) + " / " + std::to_string(m_entries.size()));
        } else {
            SetSubHeading(std::to_string(m_entries.size()));
        }
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_entries.empty()) {
            return;
        }
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::X);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_index = i;
            }
        }, this);
    }

    auto TryLoadAvatar(Entry& e) -> bool {
        if (e.avatar_decoded) {
            return false;
        }
        e.avatar_decoded = true;
        if (e.avatar_bytes.empty()) {
            return false;
        }
        auto img = ImageLoadFromMemory(e.avatar_bytes, ImageFlag_JPEG);
        if (img.data.empty()) {
            img = ImageLoadIcon(e.avatar_bytes);
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
                "%s", "No user backups found on the sending console."_i18n.c_str());
            return;
        }

        m_list->Draw(vg, theme, m_entries.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
            auto& e = m_entries[i];
            TryLoadAvatar(e);
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }
            if (e.selected) {
                auto tint = theme->GetColour(ThemeEntryID_FOCUS);
                tint.a *= 0.35f;
                gfx::drawRect(vg, v, tint, 5.f);
            }

            gfx::drawCheckbox(vg, theme, v.x + 16.f, v.y + (v.h - gfx::CHECKBOX_SIZE) / 2.f,
                gfx::CHECKBOX_SIZE, e.selected);

            const float icon_size = 46.f;
            const float icon_x = v.x + 50.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                e.image > 0 ? e.image : App::GetDefaultImage(), 4);

            const float text_x = icon_x + icon_size + 14.f;
            const auto link_status_str = e.pack.link_valid ? "Linked"_i18n : "Local"_i18n;
            const auto link_color = e.pack.link_valid ? nvgRGBA(80, 200, 120, 255) : theme->GetColour(ThemeEntryID_TEXT_INFO);

            float bounds[4]{};
            gfx::textBounds(vg, 0, 0, bounds, link_status_str.c_str());
            const float status_w = bounds[2] - bounds[0] + 20.f;
            gfx::drawText(vg, v.x + v.w - 15.f, v.y + v.h / 2.f, 16.f,
                link_color, link_status_str.c_str(),
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, v.w - (text_x - v.x) - 15.f - status_w, v.h);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", e.pack.nickname.c_str());

            std::string detail_str = !e.pack.created_label.empty() ? e.pack.created_label : e.pack.folder_name;
            detail_str += e.pack.has_playtime ? " В· play hours"_i18n : " В· no play hours"_i18n;
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", detail_str.c_str());
            nvgRestore(vg);
        });
    }

private:
    void OnRestore() {
        if (m_entries.empty()) {
            return;
        }
        std::vector<account_user::Pack> picked;
        for (const auto& e : m_entries) {
            if (e.selected) {
                picked.push_back(e.pack);
            }
        }
        if (picked.empty() && !m_entries.empty()) {
            picked.push_back(m_entries[m_index].pack);
        }
        DownloadRemotePacks(std::move(picked));
    }

    void DownloadRemotePacks(std::vector<account_user::Pack> picked) {
        auto imported_packs = std::make_shared<std::vector<account_user::Pack>>();
        auto download_err = std::make_shared<std::string>();

        const std::string base_url = m_base_url;

        App::Push<ProgressBox>(
            0,
            "Downloading User Backup..."_i18n,
            picked.size() == 1 ? picked.front().nickname : (std::to_string(picked.size()) + " backups"),
            [base_url, picked, imported_packs, download_err](ProgressBox* pbox) -> Result {
                fs::FsNativeSd sd;
                const std::string root_dst = account_user::GetUserPacksRoot();
                R_TRY(sd.CreateDirectoryRecursively(root_dst.c_str()));

                std::vector<std::string> created_dirs;
                std::vector<std::string> created_files;
                bool success = false;
                ON_SCOPE_EXIT({
                    if (!success) {
                        for (const auto& dir : created_dirs) {
                            sd.DeleteDirectoryRecursively(dir.c_str());
                        }
                        for (const auto& file : created_files) {
                            sd.DeleteFile(file.c_str());
                        }
                    }
                });

                for (size_t p_idx = 0; p_idx < picked.size(); ++p_idx) {
                    if (pbox->ShouldExit()) {
                        return Result_TransferCancelled;
                    }

                    const auto& pack_info = picked[p_idx];
                    const std::string remote_path = pack_info.dir;
                    const std::string entry_name = pack_info.folder_name;

                    const std::string prefix = (picked.size() > 1)
                        ? ("[" + std::to_string(p_idx + 1) + "/" + std::to_string(picked.size()) + "] ")
                        : "";

                    if (pack_info.is_archive) {
                        if (pack_info.remote_size <= 0) {
                            log_write("[USER_TRANSFER] Invalid remote archive size %lld for %s\n",
                                static_cast<long long>(pack_info.remote_size), remote_path.c_str());
                            *download_err = "Invalid backup files received from the sending console."_i18n;
                            return Result_FsInvalidType;
                        }

                        pbox->NewTransfer(prefix + entry_name);

                        std::string safe_file = entry_name;
                        while (!safe_file.empty() && (safe_file.back() == '/' || safe_file.back() == '\\')) {
                            safe_file.pop_back();
                        }
                        if (const auto slash = safe_file.find_last_of("/\\"); slash != std::string::npos) {
                            safe_file.erase(0, slash + 1);
                        }
                        if (safe_file.empty()) {
                            safe_file = "user_backup.kefir-user.zip";
                        }

                        std::string stem = safe_file;
                        if (stem.ends_with(".kefir-user.zip")) {
                            stem.erase(stem.size() - 15);
                        }

                        std::string target_file = root_dst + "/" + safe_file;
                        int suffix = 1;
                        while (sd.FileExists(target_file.c_str()) || sd.DirExists(target_file.c_str()) ||
                               std::ranges::find(created_files, target_file) != created_files.end()) {
                            target_file = root_dst + "/" + stem + "_" + std::to_string(suffix++) + ".kefir-user.zip";
                        }

                        const std::string part_path = target_file + ".part";
                        created_files.push_back(part_path);
                        created_files.push_back(target_file);

                        const std::string download_url = base_url + "/download?path=" + curl::EscapeString(remote_path);

                        curl::Api dl_api;
                        dl_api.SetOption(curl::Url{download_url});
                        dl_api.SetOption(curl::Path{part_path});
                        dl_api.SetOption(curl::OnProgress{pbox->OnDownloadProgressCallback()});

                        const auto dl_res = curl::ToFile(dl_api);

                        if (pbox->ShouldExit()) {
                            return Result_TransferCancelled;
                        }

                        if (!dl_res.success) {
                            log_write("[USER_TRANSFER] Failed download %s -> %s\n", download_url.c_str(), part_path.c_str());
                            *download_err = "Failed to download backup files from the sending console."_i18n;
                            return Result_FsInvalidType;
                        }

                        // Open downloaded .part and verify size matches declared remote size
                        fs::File part_file;
                        s64 written_size = 0;
                        if (R_FAILED(sd.OpenFile(part_path.c_str(), FsOpenMode_Read, &part_file)) ||
                            R_FAILED(part_file.GetSize(&written_size)) ||
                            written_size != pack_info.remote_size) {
                            log_write("[USER_TRANSFER] Archive size mismatch for %s: expected %lld, got %lld\n",
                                part_path.c_str(), static_cast<long long>(pack_info.remote_size), static_cast<long long>(written_size));
                            *download_err = "Downloaded backup is incomplete or invalid."_i18n;
                            return Result_FsInvalidType;
                        }

                        R_TRY(sd.RenameFile(part_path.c_str(), target_file.c_str()));

                        const auto pack = account_user::FindUserPack(target_file);
                        if (pack.dir.empty()) {
                            log_write("[USER_TRANSFER] FindUserPack failed on target %s\n", target_file.c_str());
                            *download_err = "Downloaded backup is incomplete or invalid."_i18n;
                            return Result_FsInvalidType;
                        }

                        imported_packs->push_back(pack);
                        continue;
                    }

                    pbox->SetTransfer(prefix + "Fetching file list..."_i18n);

                    const std::string manifest_url = base_url + "/list-recursive?path=" + curl::EscapeString(remote_path);
                    curl::Api manifest_api;
                    manifest_api.SetOption(curl::Url{manifest_url});
                    manifest_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                        return !pbox->ShouldExit();
                    }});

                    const auto manifest_res = curl::ToMemory(manifest_api);
                    if (pbox->ShouldExit()) {
                        return Result_TransferCancelled;
                    }
                    if (!manifest_res.success || manifest_res.data.empty()) {
                        *download_err = "Could not retrieve the file manifest from the sending console."_i18n;
                        return Result_FsInvalidType;
                    }

                    const std::string manifest_json(manifest_res.data.begin(), manifest_res.data.end());
                    const auto files_opt = ParseManifestResponse(manifest_json, remote_path);
                    if (!files_opt || files_opt->empty()) {
                        *download_err = "Invalid backup files received from the sending console."_i18n;
                        return Result_FsInvalidType;
                    }
                    const auto& files = *files_opt;

                    std::string safe_folder = entry_name;
                    while (!safe_folder.empty() && (safe_folder.back() == '/' || safe_folder.back() == '\\')) {
                        safe_folder.pop_back();
                    }
                    if (const auto slash = safe_folder.find_last_of("/\\"); slash != std::string::npos) {
                        safe_folder.erase(0, slash + 1);
                    }
                    if (safe_folder.empty()) {
                        safe_folder = "User_Backup";
                    }

                    std::string target_dir = root_dst + "/" + safe_folder;
                    int suffix = 1;
                    while (sd.DirExists(target_dir.c_str()) || sd.FileExists(target_dir.c_str()) ||
                           std::ranges::find(created_dirs, target_dir) != created_dirs.end()) {
                        target_dir = root_dst + "/" + safe_folder + "_" + std::to_string(suffix++);
                    }

                    R_TRY(sd.CreateDirectoryRecursively(target_dir.c_str()));
                    created_dirs.push_back(target_dir);

                    for (size_t i = 0; i < files.size(); ++i) {
                        if (pbox->ShouldExit()) {
                            return Result_TransferCancelled;
                        }

                        const auto& f = files[i];
                        const std::string dest_file_path = target_dir + "/" + f.rel_path;
                        R_TRY(sd.CreateDirectoryRecursivelyWithPath(dest_file_path.c_str()));

                        pbox->NewTransfer(prefix + f.rel_path);

                        const std::string download_url = base_url + "/download?path=" + curl::EscapeString(f.remote_path);

                        curl::Api dl_api;
                        dl_api.SetOption(curl::Url{download_url});
                        dl_api.SetOption(curl::Path{dest_file_path});
                        dl_api.SetOption(curl::OnProgress{pbox->OnDownloadProgressCallback()});

                        const auto dl_res = curl::ToFile(dl_api);

                        if (pbox->ShouldExit()) {
                            return Result_TransferCancelled;
                        }

                        if (!dl_res.success) {
                            log_write("[USER_TRANSFER] Failed download %s -> %s\n", download_url.c_str(), dest_file_path.c_str());
                            *download_err = "Failed to download backup files from the sending console."_i18n;
                            return Result_FsInvalidType;
                        }

                        fs::File file;
                        s64 written_size = 0;
                        if (R_FAILED(sd.OpenFile(dest_file_path.c_str(), FsOpenMode_Read, &file)) ||
                            R_FAILED(file.GetSize(&written_size)) ||
                            written_size != f.size) {
                            log_write("[USER_TRANSFER] Size mismatch for %s: expected %ld, got %ld\n",
                                dest_file_path.c_str(), f.size, written_size);
                            *download_err = "Failed to download backup files from the sending console."_i18n;
                            return Result_FsInvalidType;
                        }
                    }

                    const auto pack = account_user::FindUserPack(target_dir);
                    if (pack.dir.empty()) {
                        log_write("[USER_TRANSFER] FindUserPack failed on target %s\n", target_dir.c_str());
                        *download_err = "Downloaded backup is incomplete or invalid."_i18n;
                        return Result_FsInvalidType;
                    }

                    imported_packs->push_back(pack);
                }

                success = true;
                R_SUCCEED();
            },
            [this, imported_packs, download_err](Result rc) {
                if (rc == Result_TransferCancelled) {
                    return;
                }
                if (R_FAILED(rc) || imported_packs->empty()) {
                    const std::string msg = !download_err->empty()
                        ? *download_err
                        : "Failed to transfer user backup from the sending console."_i18n;
                    App::Push<OptionBox>(msg, "OK"_i18n);
                    return;
                }

                auto packs = std::move(*imported_packs);
                auto on_restore = m_on_restore;
                SetPop();
                if (on_restore) {
                    on_restore(std::move(packs));
                }
            }
        );
    }

    std::string m_base_url;
    std::vector<Entry> m_entries;
    Callback m_on_restore;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

} // namespace

void OpenRemoteUserPacks(
    std::string base_url,
    std::vector<RemoteUserPacksEntry> entries,
    std::function<void(std::vector<account_user::Pack>)> on_restore)
{
    App::Push<RemoteUserPacksMenu>(std::move(base_url), std::move(entries), std::move(on_restore));
}

} // namespace sphaira::ui::menu::users
