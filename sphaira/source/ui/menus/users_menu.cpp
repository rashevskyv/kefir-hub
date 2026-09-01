#include "ui/menus/users_menu.hpp"

#include "account_user.hpp"
#include "account_restore.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "nand_transfer.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "swkbd.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save_menu.hpp"
#include "download.hpp"
#include "net.hpp"
#include "ui/nvg_util.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/image_crop.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "utils/utils.hpp"

#include <switch/applets/psel.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sphaira::ui::menu::users {
namespace {

auto CollectSaves(const std::vector<AccountUid>& uids) -> std::vector<save::Entry> {
    title::Init();
    std::vector<save::Entry> out;
    for (const auto& uid : uids) {
        auto part = save::Menu::ListAccountSaves(uid);
        out.insert(out.end(), part.begin(), part.end());
    }
    for (auto& e : out) {
        save::detail::LoadControlEntry(e);
        e.selected = true;
    }
    title::Exit();
    return out;
}

auto NormUidHex(std::string s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c != '-') {
            out += static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

auto FindLiveUidForPack(const account_user::Pack& p) -> std::optional<AccountUid> {
    const auto users = account_link::ListUsers();
    if (!p.uid_hex.empty()) {
        const auto want = NormUidHex(p.uid_hex);
        for (const auto& u : users) {
            if (!u.uid_hex.empty() && NormUidHex(u.uid_hex) == want) {
                log_write("[USER] Replace: pack uid %s is already on this console\n", p.uid_hex.c_str());
                return u.uid;
            }
        }
    }
    if (p.nas_id != 0) {
        AccountUid by_nas{};
        if (account_link::FindLiveUidByNasId(p.nas_id, by_nas)) {
            log_write("[USER] Replace: pack nas %llx proven on live uid\n",
                static_cast<unsigned long long>(p.nas_id));
            return by_nas;
        }
        log_write("[USER] Create: pack nas %llx not proven on this console\n",
            static_cast<unsigned long long>(p.nas_id));
    }
    return std::nullopt;
}

auto LiveNameForUid(const AccountUid& uid) -> std::string {
    for (const auto& u : account_link::ListUsers()) {
        if (u.uid.uid[0] == uid.uid[0] && u.uid.uid[1] == uid.uid[1]) {
            return u.nickname;
        }
    }
    return {};
}

struct SavePickMenu final : MenuBase {
    using Callback = std::function<void(std::optional<std::vector<save::Entry>>)>;

    SavePickMenu(std::vector<save::Entry> entries, Callback cb)
        : MenuBase{"Backup saves"_i18n, MenuFlag_None}
        , m_entries{std::move(entries)}
        , m_cb{std::move(cb)}
    {
        title::Init();
        this->SetActions(
            std::make_pair(Button::A, Action{"Backup"_i18n, [this](){
                std::vector<save::Entry> picked;
                for (auto& e : m_entries) {
                    if (e.selected) {
                        picked.push_back(e);
                    }
                }
                auto cb = m_cb;
                SetPop();
                cb(std::move(picked));
            }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                auto cb = m_cb;
                SetPop();
                cb(std::nullopt);
            }}),
            std::make_pair(Button::X, Action{"Select"_i18n, [this](){
                if (m_entries.empty()) {
                    return;
                }
                m_entries[m_index].selected ^= 1;
                if (m_index + 1 < static_cast<s64>(m_entries.size())) {
                    m_index++;
                    m_list->EnsureVisible(m_index, m_entries.size());
                }
            }})
        );
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 70.f});
        SetTitleSubHeading("X marks games to back up. A backs up the marked saves."_i18n, true);
        SetSubHeading(std::to_string(m_entries.size()));
    }

    ~SavePickMenu() {
        title::Exit();
    }

    auto GetShortTitle() const -> const char* override { return "Saves"; }

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

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        if (m_entries.empty()) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 22.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", "No saves on this account"_i18n.c_str());
            return;
        }
        m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& e = m_entries[i];
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            }
            gfx::drawCheckbox(vg, theme, v.x + 16.f, v.y + (v.h - gfx::CHECKBOX_SIZE) / 2.f,
                gfx::CHECKBOX_SIZE, e.selected);
            const char* name = e.GetName();
            if (!name || !name[0]) {
                name = "Unknown";
            }
            gfx::drawTextArgs(vg, v.x + 56.f, v.y + v.h / 2.f, 18.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", name);
        });
    }

private:
    std::vector<save::Entry> m_entries;
    Callback m_cb;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct RestoreBackupMenu final : MenuBase {
    using Callback = std::function<void(std::optional<std::vector<account_user::Pack>>)>;

    struct Entry {
        account_user::Pack pack;
        int image{};
        bool selected{};
        bool avatar_tried{};
    };

    RestoreBackupMenu(std::vector<account_user::Pack> packs, Callback cb, bool allow_delete = true)
        : MenuBase{"Restore Backup"_i18n, MenuFlag_None}
        , m_cb{std::move(cb)}
    {
        for (auto& p : packs) {
            Entry e;
            e.pack = std::move(p);
            m_entries.push_back(std::move(e));
        }
        this->SetActions(
            std::make_pair(Button::A, Action{"Restore"_i18n, [this](){
                std::vector<account_user::Pack> picked;
                for (const auto& e : m_entries) {
                    if (e.selected) {
                        picked.push_back(e.pack);
                    }
                }
                if (picked.empty() && !m_entries.empty()) {
                    picked.push_back(m_entries[m_index].pack);
                }
                auto cb = m_cb;
                SetPop();
                if (cb) {
                    cb(std::move(picked));
                }
            }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                auto cb = m_cb;
                SetPop();
                if (cb) {
                    cb(std::nullopt);
                }
            }}),
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
            }}),
            std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){
                ConfirmDeletePacks();
            }})
        );
        if (!allow_delete) {
            this->RemoveAction(Button::SELECT);
            SetTitleSubHeading("X marks backups to restore. A restores the selected profiles."_i18n, true);
        } else {
            SetTitleSubHeading("X marks backups. Minus deletes. A restores."_i18n, true);
        }
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        UpdateSubHeading();
    }

    ~RestoreBackupMenu() {
        auto* vg = App::GetVg();
        for (auto& e : m_entries) {
            if (e.image > 0 && vg) {
                nvgDeleteImage(vg, e.image);
                e.image = 0;
            }
        }
    }

    auto GetShortTitle() const -> const char* override { return "Restore"; }

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
        if (e.avatar_tried) {
            return false;
        }
        e.avatar_tried = true;
        fs::FsNativeSd sd;
        std::vector<u8> jpeg;
        if (R_FAILED(sd.read_entire_file((e.pack.dir + "/avatar.jpg").c_str(), jpeg)) || jpeg.empty()) {
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
                "%s", "No user backups found"_i18n.c_str());
            return;
        }
        int loaded = 0;
        m_list->Draw(vg, theme, m_entries.size(), [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
            auto& e = m_entries[i];
            if (loaded < 2 && TryLoadAvatar(e)) {
                loaded++;
            }
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
            detail_str += e.pack.has_playtime ? " · play hours"_i18n : " · no play hours"_i18n;
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", detail_str.c_str());
            nvgRestore(vg);
        });
    }

private:
    void ConfirmDeletePacks() {
        if (m_entries.empty()) {
            return;
        }
        std::vector<s64> idxs;
        if (m_selected_count > 0) {
            for (s64 i = 0; i < static_cast<s64>(m_entries.size()); i++) {
                if (m_entries[i].selected) {
                    idxs.push_back(i);
                }
            }
        } else {
            idxs.push_back(m_index);
        }
        const auto msg = (idxs.size() > 1)
            ? "Delete the selected backups from the SD card?"_i18n
            : "Delete this backup from the SD card?"_i18n;
        App::Push<OptionBox>(msg, "Cancel"_i18n, "Delete"_i18n, 1, [this, idxs](auto op) {
            if (!op || *op != 1) {
                return;
            }
            for (auto it = idxs.rbegin(); it != idxs.rend(); ++it) {
                const auto i = *it;
                if (i < 0 || static_cast<size_t>(i) >= m_entries.size()) {
                    continue;
                }
                if (R_FAILED(account_user::DeleteUserPack(m_entries[i].pack.dir))) {
                    App::Push<OptionBox>("Could not delete the backup."_i18n, "OK"_i18n);
                    return;
                }
                if (m_entries[i].image > 0) {
                    nvgDeleteImage(App::GetVg(), m_entries[i].image);
                }
                if (m_entries[i].selected) {
                    m_selected_count--;
                }
                m_entries.erase(m_entries.begin() + i);
            }
            if (m_index >= static_cast<s64>(m_entries.size())) {
                m_index = m_entries.empty() ? 0 : static_cast<s64>(m_entries.size()) - 1;
            }
            UpdateSubHeading();
        });
    }

    std::vector<Entry> m_entries;
    Callback m_cb;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

struct RemotePackEntry {
    std::string name;
    std::string remote_path;
};

inline void SkipJsonWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
        pos++;
    }
}

inline auto ParseJsonString(const std::string& s, size_t& pos) -> std::optional<std::string> {
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

inline auto ParseJsonInt(const std::string& s, size_t& pos) -> std::optional<s64> {
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

inline auto ParseRemoteListResponse(const std::string& json) -> std::optional<std::pair<std::string, std::vector<RemotePackEntry>>> {
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
                ParseJsonInt(json, pos);
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
                out_entries.push_back({entry_name, rem_path});
            }
        }
    }

    std::sort(out_entries.begin(), out_entries.end(), [](const auto& a, const auto& b) {
        return a.name > b.name;
    });

    return std::make_pair(root_path, out_entries);
}

struct ManifestFile {
    std::string remote_path;
    std::string rel_path;
    s64 size{};
};

inline auto ParseManifestResponse(const std::string& json, const std::string& remote_pack_root) -> std::optional<std::vector<ManifestFile>> {
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

struct RemoteUserPacksMenu final : MenuBase {
    using Callback = std::function<void(std::vector<account_user::Pack>)>;

    struct Entry {
        account_user::Pack pack;
        std::vector<u8> avatar_bytes;
        int image{};
        bool selected{};
        bool avatar_decoded{};
    };

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

        m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
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
            detail_str += e.pack.has_playtime ? " · play hours"_i18n : " · no play hours"_i18n;
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
                bool success = false;
                ON_SCOPE_EXIT({
                    if (!success) {
                        for (const auto& dir : created_dirs) {
                            sd.DeleteDirectoryRecursively(dir.c_str());
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

struct RestoreSourceItem {
    std::string label;
    std::string description;
    std::function<void()> action;
};

struct RestoreSourceMenu final : MenuBase {
    using Callback = std::function<void(std::vector<account_user::Pack>)>;

    RestoreSourceMenu(Callback on_restore)
        : MenuBase{"Restore User Backup"_i18n, MenuFlag_None}
        , m_on_restore{std::move(on_restore)}
    {
        m_items = {
            {
                "Local backup library"_i18n,
                "Restore user backups from the default library (/config/kefir/user_packs)."_i18n,
                [this]() { OpenLocalLibrary(); }
            },
            {
                "Browse folder..."_i18n,
                "Select a folder or user backup package on the microSD card."_i18n,
                [this]() { OpenBrowseFolder(); }
            },
            {
                "Other console..."_i18n,
                "Restore user backups from another console running Share User Backups."_i18n,
                [this]() { ProbeOtherConsole(); }
            },
        };

        this->SetActions(
            std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OnSelect(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
        );

        m_list = std::make_unique<List>(1, 6, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
        m_list->SetLayout(List::Layout::GRID);
        m_list->SetPageJump(false);
        SetIndex(0);
    }

    ~RestoreSourceMenu() = default;

    auto GetShortTitle() const -> const char* override { return "Restore"; }

    void Update(Controller* controller, TouchInfo* touch) override {
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

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);

        m_list->Draw(vg, theme, m_items.size(), [vg, theme, this](auto*, auto*, Vec4 v, auto i) {
            const auto& item = m_items[i];
            const auto is_selected = m_index == static_cast<s64>(i);
            const auto text_id = is_selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
            if (is_selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f - 10.f, 18.f,
                theme->GetColour(text_id), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f + 14.f, 14.f,
                theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        });
    }

    void OnFocusGained() override {
        MenuBase::OnFocusGained();
        SetIndex(m_index);
        if (m_pending_browse_folder) {
            const auto folder = *m_pending_browse_folder;
            m_pending_browse_folder.reset();
            const auto packs = account_user::ListUserPacks(folder.toString());
            if (packs.empty()) {
                App::Push<OptionBox>("No user backups found in the selected folder."_i18n, "OK"_i18n);
            } else {
                OpenPacksRestore(packs, false);
            }
        }
    }

private:
    void SetIndex(s64 index) {
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

    void OnSelect() {
        if (!m_items.empty() && m_items[m_index].action) {
            m_items[m_index].action();
        }
    }

    void OpenLocalLibrary() {
        const auto packs = account_user::ListUserPacks();
        if (packs.empty()) {
            App::Push<OptionBox>("No user backups found under /config/kefir/user_packs."_i18n, "OK"_i18n);
            return;
        }
        OpenPacksRestore(packs, true);
    }

    void OpenBrowseFolder() {
        auto browser = std::make_unique<filebrowser::Menu>(MenuFlag_None);
        browser->SetFolderPicker([this](const fs::FsPath& folder) {
            m_pending_browse_folder = folder;
        }, "Select user backup folder"_i18n, "Restore user backups from this folder?"_i18n);
        App::Push(std::move(browser));
    }

    void OpenPacksRestore(std::vector<account_user::Pack> packs, bool allow_delete) {
        App::Push<RestoreBackupMenu>(std::move(packs), [this](auto picked) {
            if (!picked || picked->empty()) {
                return;
            }
            if (m_on_restore) {
                m_on_restore(std::move(*picked));
            }
        }, allow_delete);
    }

    void ProbeOtherConsole() {
        net::RequireConnection([this](){
            u32 ip{};
            std::string initial_prefix;
            if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0) {
                char prefix[32]{};
                std::snprintf(prefix, sizeof(prefix), "%u.%u.%u.",
                    ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF);
                initial_prefix = prefix;
            }

            std::string input;
            if (R_FAILED(swkbd::ShowText(input, "Enter sending console IP address"_i18n.c_str(),
                    initial_prefix.empty() ? nullptr : initial_prefix.c_str())) || input.empty()) {
                return;
            }
            while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r' || input.front() == '\n')) {
                input.erase(input.begin());
            }
            while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r' || input.back() == '\n')) {
                input.pop_back();
            }
            if (input.empty()) {
                return;
            }

            auto responding_url = std::make_shared<std::string>();
            auto probed_ok = std::make_shared<bool>(false);
            auto list_ok = std::make_shared<bool>(false);
            auto remote_entries = std::make_shared<std::vector<RemoteUserPacksMenu::Entry>>();
            auto on_restore = m_on_restore;

            App::Push<ProgressBox>(
                0,
                "Testing Connection..."_i18n,
                input,
                [input, responding_url, probed_ok, list_ok, remote_entries](auto pbox) -> Result {
                    std::string base_input = input;
                    while (!base_input.empty() && (base_input.back() == '/' || base_input.back() == '\\')) {
                        base_input.pop_back();
                    }

                    std::vector<std::string> candidate_urls;
                    if (base_input.rfind("http://", 0) == 0 || base_input.rfind("https://", 0) == 0) {
                        candidate_urls.push_back(base_input);
                    } else if (base_input.find(':') != std::string::npos) {
                        candidate_urls.push_back("http://" + base_input);
                    } else {
                        for (u16 port = 8080; port <= 8090; ++port) {
                            candidate_urls.push_back("http://" + base_input + ":" + std::to_string(port));
                        }
                    }

                    for (const auto& url : candidate_urls) {
                        if (pbox->ShouldExit()) {
                            return pbox->ShouldExitResult();
                        }
                        pbox->SetTransfer(url);
                        curl::Api api;
                        api.SetOption(curl::Url{url});
                        api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                            return !pbox->ShouldExit();
                        }});
                        const auto res = curl::Probe(api, curl::ProbeType::Http);
                        if (res.success) {
                            *responding_url = url;
                            *probed_ok = true;
                            break;
                        }
                        if (pbox->ShouldExit()) {
                            return pbox->ShouldExitResult();
                        }
                    }

                    if (!*probed_ok || responding_url->empty()) {
                        return Result_FsEmpty;
                    }

                    pbox->SetTransfer("Fetching backup list..."_i18n);

                    curl::Api list_api;
                    list_api.SetOption(curl::Url{*responding_url + "/list"});
                    list_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                        return !pbox->ShouldExit();
                    }});

                    const auto list_res = curl::ToMemory(list_api);
                    if (pbox->ShouldExit()) {
                        return Result_TransferCancelled;
                    }
                    if (!list_res.success || list_res.data.empty()) {
                        return Result_FsInvalidType;
                    }

                    const std::string list_json(list_res.data.begin(), list_res.data.end());
                    const auto parsed = ParseRemoteListResponse(list_json);
                    if (!parsed) {
                        return Result_FsInvalidType;
                    }

                    *list_ok = true;
                    const auto& candidates = parsed->second;
                    for (const auto& cand : candidates) {
                        if (pbox->ShouldExit()) {
                            return Result_TransferCancelled;
                        }
                        pbox->SetTransfer(cand.name);

                        RemoteUserPacksMenu::Entry item;
                        item.pack.folder_name = cand.name;
                        item.pack.dir = cand.remote_path;

                        const std::string prof_url = *responding_url + "/download?path=" + curl::EscapeString(cand.remote_path + "/profile.json");
                        curl::Api prof_api;
                        prof_api.SetOption(curl::Url{prof_url});
                        prof_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                            return !pbox->ShouldExit();
                        }});
                        const auto prof_res = curl::ToMemory(prof_api);
                        if (prof_res.success && !prof_res.data.empty()) {
                            const std::string prof_json(prof_res.data.begin(), prof_res.data.end());
                            item.pack.nickname = account_user::ReadJsonField(prof_json, "nickname");
                            item.pack.uid_hex = account_user::ReadJsonField(prof_json, "uid");
                            const std::string created = account_user::ReadJsonField(prof_json, "created");
                            item.pack.created_label = account_user::FormatPackCreated(cand.name, created);
                            const std::string link_status = account_user::ReadJsonField(prof_json, "link_status");
                            item.pack.link_valid = (link_status == "linked");
                        }
                        if (item.pack.created_label.empty()) {
                            item.pack.created_label = account_user::FormatPackCreated(cand.name, {});
                        }
                        if (item.pack.nickname.empty()) {
                            item.pack.nickname = "User";
                        }

                        const std::string manifest_url = *responding_url + "/list-recursive?path=" + curl::EscapeString(cand.remote_path);
                        curl::Api m_api;
                        m_api.SetOption(curl::Url{manifest_url});
                        m_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                            return !pbox->ShouldExit();
                        }});
                        const auto m_res = curl::ToMemory(m_api);
                        if (m_res.success && !m_res.data.empty()) {
                            const std::string m_json(m_res.data.begin(), m_res.data.end());
                            const auto files_opt = ParseManifestResponse(m_json, cand.remote_path);
                            if (files_opt) {
                                for (const auto& f : *files_opt) {
                                    if (f.rel_path == "avatar.jpg") {
                                        item.pack.has_avatar = true;
                                    } else if (f.rel_path == "pdm/PlayEvent.dat" || f.rel_path == "PlayEvent.dat") {
                                        item.pack.has_playtime = true;
                                    } else if (!item.pack.link_valid && (f.rel_path.starts_with("baas/") || f.rel_path.starts_with("nas/"))) {
                                        item.pack.link_valid = true;
                                    }
                                }
                            }
                        }

                        if (item.pack.has_avatar) {
                            const std::string av_url = *responding_url + "/download?path=" + curl::EscapeString(cand.remote_path + "/avatar.jpg");
                            curl::Api av_api;
                            av_api.SetOption(curl::Url{av_url});
                            av_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                                return !pbox->ShouldExit();
                            }});
                            const auto av_res = curl::ToMemory(av_api);
                            if (av_res.success && !av_res.data.empty()) {
                                item.avatar_bytes = std::move(av_res.data);
                            }
                        }

                        remote_entries->push_back(std::move(item));
                    }

                    R_SUCCEED();
                },
                [responding_url, probed_ok, list_ok, remote_entries, on_restore](Result rc) {
                    if (rc == Result_TransferCancelled) {
                        return;
                    }
                    if (!*probed_ok) {
                        App::Push<OptionBox>(
                            "Could not connect to the remote console.\n\n"
                            "Confirm that both consoles are connected to the same local network and that the source console has Share User Backups active."_i18n,
                            "OK"_i18n
                        );
                        return;
                    }
                    if (!*list_ok) {
                        App::Push<OptionBox>(
                            "Could not retrieve the user backup list from the sending console."_i18n,
                            "OK"_i18n
                        );
                        return;
                    }
                    if (remote_entries->empty()) {
                        App::Push<OptionBox>(
                            "No user backups found on the sending console."_i18n,
                            "OK"_i18n
                        );
                        return;
                    }

                    App::Push<RemoteUserPacksMenu>(*responding_url, std::move(*remote_entries), on_restore);
                }
            );
        });
    }

private:
    Callback m_on_restore;
    std::vector<RestoreSourceItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    std::optional<fs::FsPath> m_pending_browse_folder;
};

struct AvatarPickMenu final : MenuBase {
    using Callback = std::function<void(std::vector<u8>)>;
    enum class Kind { Existing, FromSd, Sgdb };

    struct Tile {
        Kind kind{Kind::Existing};
        std::vector<u8> jpeg;
        int image{};
        std::string label;
    };

    explicit AvatarPickMenu(Callback cb)
        : MenuBase{"Choose an avatar"_i18n, MenuFlag_None}
        , m_cb{std::move(cb)}
    {
        LoadTiles();
        this->SetActions(
            std::make_pair(Button::A, Action{"Select"_i18n, [this](){ Activate(); }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){ SetPop(); }})
        );
        const Vec4 list_pos{40.f, 110.f, 1200.f, 530.f};
        const Vec4 item_pos{70.f, 120.f, 160.f, 160.f};
        m_list = std::make_unique<List>(6, 12, list_pos, item_pos, Vec2{14.f, 14.f});
        SetTitleSubHeading("A selects. SteamGridDB asks for a game name, then lets you choose a matching game."_i18n, true);
        SetSubHeading(std::to_string(m_tiles.size()));
    }

    ~AvatarPickMenu() {
        auto* vg = App::GetVg();
        for (auto& t : m_tiles) {
            if (t.image > 0 && vg) {
                nvgDeleteImage(vg, t.image);
                t.image = 0;
            }
        }
    }

    auto GetShortTitle() const -> const char* override { return "Avatar"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_tiles.empty()) {
            return;
        }
        m_list->OnUpdate(controller, touch, m_index, m_tiles.size(), [this](bool touched, auto i) {
            if (touched && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_index = i;
            }
        }, this);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        m_list->Draw(vg, theme, m_tiles.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& t = m_tiles[i];
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }
            if (t.kind == Kind::FromSd) {
                const Vec4 icon_rect{v.x + 16.f, v.y + 12.f, v.w - 32.f, v.h - 48.f};
                DrawElementContain(icon_rect, ThemeEntryID_ICON_FILE);
                gfx::drawTextArgs(vg, v.x + v.w / 2.f, v.y + v.h - 18.f, 14.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                    "%s", "From SD"_i18n.c_str());
            } else if (t.kind == Kind::Sgdb) {
                gfx::drawTextArgs(vg, v.x + v.w / 2.f, v.y + v.h / 2.f, 18.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                    "SteamGridDB");
            } else {
                const Vec4 inner{v.x + 8.f, v.y + 8.f, v.w - 16.f, v.h - 16.f};
                gfx::drawImage(vg, inner, t.image > 0 ? t.image : App::GetDefaultImage(), 5.f);
            }
        });
    }

private:
    void AddDecodedTile(std::span<const u8> raw) {
        if (raw.empty()) {
            return;
        }
        auto img = ImageLoadFromMemory(raw, ImageFlag_JPEG);
        if (img.data.empty()) {
            img = ImageLoadFromMemory(raw);
        }
        if (img.data.empty() || img.w <= 0 || img.h <= 0) {
            return;
        }
        Tile t;
        t.jpeg = ImageNormalizeAvatar(raw);
        if (t.jpeg.empty()) {
            t.jpeg = ImageNormalizeIcon(raw);
        }
        if (t.jpeg.empty()) {
            t.jpeg.assign(raw.begin(), raw.end());
        }
        t.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
        m_tiles.push_back(std::move(t));
    }

    void LoadTiles() {
        for (const auto& base : App::GetAccountList()) {
            std::vector<u8> jpeg;
            if (R_SUCCEEDED(account_user::LoadImageJpeg(base.uid, jpeg))) {
                AddDecodedTile(jpeg);
            }
        }
        fs::FsNativeSd sd;
        fs::Dir dir;
        const auto extra = paths::DATA_ROOT + "/avatars";
        if (R_SUCCEEDED(sd.OpenDirectory(extra.c_str(), FsDirOpenMode_ReadFiles, &dir))) {
            std::vector<FsDirectoryEntry> ents;
            dir.ReadAll(ents);
            for (const auto& e : ents) {
                if (e.type != FsDirEntryType_File) {
                    continue;
                }
                const auto ext = path::Extension(e.name);
                static constexpr std::string_view kImg[] = {"jpg", "jpeg", "png", "bmp"};
                if (!path::IsAnyOfIC(ext, kImg)) {
                    continue;
                }
                std::vector<u8> raw;
                if (R_SUCCEEDED(sd.read_entire_file((extra + "/" + e.name).c_str(), raw))) {
                    AddDecodedTile(raw);
                }
            }
        }
        m_tiles.push_back(Tile{Kind::FromSd, {}, 0, "From SD"});
        m_tiles.push_back(Tile{Kind::Sgdb, {}, 0, "SteamGridDB"});
    }

    void Activate() {
        if (m_index < 0 || static_cast<size_t>(m_index) >= m_tiles.size()) {
            return;
        }
        auto& t = m_tiles[m_index];
        if (t.kind == Kind::FromSd) {
            OpenSd();
            return;
        }
        if (t.kind == Kind::Sgdb) {
            std::string query;
            if (R_FAILED(swkbd::ShowText(query, "Game or icon name"_i18n.c_str(), nullptr, 1, 64)) || query.empty()) {
                return;
            }
            steamgriddb::ShowIconPicker(query, [this](std::vector<u8> icon) {
                if (icon.empty()) {
                    return;
                }
                auto jpeg = ImageNormalizeIcon(icon);
                if (jpeg.empty()) {
                    jpeg = std::move(icon);
                }
                Finish(std::move(jpeg));
            });
            return;
        }
        Finish(t.jpeg);
    }

    void OpenSd() {
        App::Push<filepicker::Menu>(
            filepicker::Callback{[this](const fs::FsPath& path) -> bool {
                const auto ext = path::Extension(path);
                const auto flags = path::EqualsIC(ext, "jpg") || path::EqualsIC(ext, "jpeg")
                    ? ImageFlag_JPEG : ImageFlag_None;
                auto raw = ImageLoadFromFile(path, flags);
                if (raw.data.empty()) {
                    raw = ImageLoadFromFile(path);
                }
                if (raw.data.empty() || raw.w <= 0 || raw.h <= 0) {
                    App::Push<OptionBox>("Could not read that image."_i18n, "OK"_i18n);
                    return false;
                }
                auto source = path.toString();
                if (const auto slash = source.find_last_of("/\\"); slash != std::string::npos) {
                    source.erase(0, slash + 1);
                }
                image_crop::Show(std::move(raw), std::move(source),
                    [this](std::vector<u8> jpeg, std::string) {
                        if (!jpeg.empty()) {
                            Finish(std::move(jpeg));
                        }
                    });
                return true;
            }},
            std::vector<std::string>{"jpg", "jpeg", "png", "bmp"});
    }

    void Finish(std::vector<u8> jpeg) {
        auto cb = m_cb;
        SetPop();
        if (cb) {
            cb(std::move(jpeg));
        }
    }

    std::vector<Tile> m_tiles;
    Callback m_cb;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

auto StatusColour(Theme* theme, const account_link::User& u) -> NVGcolor {
    if (!u.linked_known) {
        return theme->GetColour(ThemeEntryID_TEXT_INFO);
    }
    switch (u.kind) {
        case account_link::LinkKind::Official:
        case account_link::LinkKind::Offline:
            return nvgRGBA(80, 200, 120, 255);
        case account_link::LinkKind::None:
        default:
            return nvgRGBA(230, 60, 60, 255);
    }
}

void DrawLinkDot(NVGcontext* vg, const Vec4& image_v, const account_link::User& u) {
    NVGcolor fill = nvgRGBA(160, 160, 160, 255);
    if (u.linked_known) {
        if (u.kind == account_link::LinkKind::Official || u.kind == account_link::LinkKind::Offline) {
            fill = nvgRGBA(80, 200, 120, 255);
        } else {
            fill = nvgRGBA(230, 60, 60, 255);
        }
    }
    const float r = std::min(6.f, image_v.w * 0.08f);
    const float cx = image_v.x + r + 5.f;
    const float cy = image_v.y + r + 5.f;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r + 1.6f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 190));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillColor(vg, fill);
    nvgFill(vg);
}

} // namespace

Menu::Menu() : grid::Menu{"Users"_i18n, MenuFlag_None} {
    if (m_layout.Get() == LayoutType::LayoutType_HbMenu) {
        m_layout.Set(LayoutType::LayoutType_GridDetail);
    }
    this->SetActions(
        std::make_pair(Button::A, Action{"Options"_i18n, [this](){ ShowContextMenu(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
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
    FreeImages();
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_items.empty()) {
        Refresh();
    }
}

void Menu::FreeImages() {
    auto* vg = App::GetVg();
    for (auto& u : m_items) {
        if (u.image) {
            nvgDeleteImage(vg, u.image);
            u.image = 0;
        }
    }
}

void Menu::OnLayoutChange() {
    m_index = 0;
    m_name_scroll.Reset();
    m_status_scroll.Reset();
    m_uid_scroll.Reset();
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
    if (m_layout.Get() == LayoutType::LayoutType_Grid) {
        const Vec4 content_pos{40, 97, 1200, 539};
        const Vec2 pad{10, 40};
        const Vec4 v{93, 150, 174, 174};
        m_list = std::make_unique<List>(6, 6*2, content_pos, v, pad);
    }
    SetIndex(0);
}

void Menu::Refresh() {
    FreeImages();
    const auto listed = account_link::ListUsers();
    m_items.clear();
    m_selected_count = 0;
    for (auto& u : listed) {
        Item item;
        static_cast<account_link::User&>(item) = std::move(u);
        m_items.push_back(std::move(item));
    }
    SetIndex(m_index);
}

void Menu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        SetTitleSubHeading("No user profiles"_i18n, true);
        SetSubHeading("");
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    const auto& item = m_items[m_index];
    SetTitleSubHeading(item.nickname + "  ·  " + StatusLabel(item), true);
    SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_items.size()));
}

auto Menu::StatusLabel(const account_link::User& u) const -> std::string {
    if (!u.linked_known) {
        return "Link status unavailable"_i18n;
    }
    switch (u.kind) {
        case account_link::LinkKind::Official:
        case account_link::LinkKind::Offline:
            return "Linked"_i18n;
        case account_link::LinkKind::None:
        default:
            return "Not linked"_i18n;
    }
}

auto Menu::TryLoadAvatar(Item& u) -> bool {
    if (u.avatar_tried) {
        return false;
    }
    u.avatar_tried = true;
    std::vector<u8> jpeg;
    if (R_FAILED(account_user::LoadImageJpeg(u.uid, jpeg)) || jpeg.empty()) {
        return false;
    }
    auto img = ImageLoadFromMemory(jpeg, ImageFlag_JPEG);
    if (img.data.empty()) {
        img = ImageLoadIcon(jpeg);
    }
    if (img.data.empty()) {
        return false;
    }
    u.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
    return true;
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
        m_list->EnsureVisible(m_index, m_items.size());
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

auto Menu::SelectedUsers() const -> std::vector<account_link::User> {
    std::vector<account_link::User> out;
    for (const auto& u : m_items) {
        if (u.selected) {
            out.push_back(u);
        }
    }
    if (out.empty() && !m_items.empty()) {
        const auto i = (m_index >= 0 && static_cast<size_t>(m_index) < m_items.size()) ? m_index : 0;
        out.push_back(m_items[i]);
    }
    return out;
}

auto Menu::SelectedUids(bool all) const -> std::vector<AccountUid> {
    std::vector<AccountUid> uids;
    if (all) {
        for (const auto& u : m_items) {
            uids.push_back(u.uid);
        }
        return uids;
    }
    for (const auto& u : SelectedUsers()) {
        uids.push_back(u.uid);
    }
    return uids;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (m_items.empty()) {
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
            "%s", "No user profiles"_i18n.c_str());
        return;
    }

    int loaded{};
    m_list->Draw(vg, theme, m_items.size(), [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
        auto& item = m_items[i];
        if (loaded < 2 && TryLoadAvatar(item)) {
            loaded++;
        }
        const auto layout = m_layout.Get();
        const auto status = StatusLabel(item);
        Vec4 image_v;
        if (layout == LayoutType::LayoutType_List) {
            const auto selected = m_index == i;
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
            const float icon_x = v.x + 10.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                item.image ?: App::GetDefaultImage(), 4);
            image_v = Vec4{icon_x, icon_y, icon_size, icon_size};
            const float text_x = icon_x + icon_size + 14.f;
            float status_w = 0.f;
            if (!status.empty()) {
                float bounds[4]{};
                gfx::textBounds(vg, 0, 0, bounds, status.c_str());
                status_w = bounds[2] - bounds[0] + 20.f;
                gfx::drawText(vg, v.x + v.w - 15.f, v.y + v.h / 2.f, 16.f,
                    StatusColour(theme, item), status.c_str(),
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            }
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, v.w - (text_x - v.x) - 15.f - status_w, v.h);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", item.nickname.c_str());
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", item.uid_hex.c_str());
            nvgRestore(vg);
        } else if (layout == LayoutType::LayoutType_Grid) {
            const auto selected = m_index == i;
            if (!selected) {
                DrawElement(v, ThemeEntryID_GRID);
                nvgSave(vg);
                nvgIntersectScissor(vg, v.x + 4.f, v.y - 28.f, v.w - 8.f, 26.f);
                gfx::drawTextArgs(vg, v.x + v.w / 2.f, v.y - 14.f, 15.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT),
                    "%s", item.nickname.c_str());
                nvgRestore(vg);
            } else {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
                gfx::drawAppLable(vg, theme, m_name_scroll, v.x, v.y, v.w, item.nickname.c_str());
            }
            image_v = v;
            gfx::drawImage(vg, image_v, item.image ?: App::GetDefaultImage(), 5);
            DrawLinkDot(vg, image_v, item);
        } else if (layout == LayoutType::LayoutType_GridDetail) {
            const auto selected = m_index == i;
            auto text_id = ThemeEntryID_TEXT;
            if (selected) {
                text_id = ThemeEntryID_TEXT_SELECTED;
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }

            image_v = v;
            image_v.x += 20;
            image_v.y += 20;
            image_v.w = 115;
            image_v.h = 115;

            const auto text_off = 148;
            const auto text_x = v.x + text_off;
            const auto text_clip_w = v.w - 30.f - text_off;
            const float font_size = 18;
            m_name_scroll.Draw(vg, selected, text_x, v.y + 45, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), item.nickname.c_str());
            m_status_scroll.Draw(vg, selected, text_x, v.y + 80, text_clip_w, font_size, NVG_ALIGN_LEFT, StatusColour(theme, item), status.c_str());
            m_uid_scroll.Draw(vg, selected, text_x, v.y + 115, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(ThemeEntryID_TEXT_INFO), item.uid_hex.c_str());

            gfx::drawImage(vg, image_v, item.image ?: App::GetDefaultImage(), 5);
        } else {
            image_v = DrawEntry(vg, theme, layout, v, m_index == i, item.image,
                item.nickname.c_str(), status.c_str(), item.uid_hex.c_str(), item.selected);
        }
        DrawSelectionMark(vg, theme, layout, v, image_v, item.selected, m_selected_count > 0);
    });

    if (m_layout.Get() == LayoutType::LayoutType_Grid && m_index >= 0 && m_index < static_cast<s64>(m_items.size())) {
        const auto& cur = m_items[m_index];
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, 580.f, 16.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "ID: %s", cur.uid_hex.c_str());
    }
}

void Menu::ShowContextMenu() {
    auto options = std::make_unique<Sidebar>(
        m_items.empty() ? "Users"_i18n : m_items[m_index].nickname, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryHeader>("PROFILE"_i18n);
    options->Add<SidebarEntryCallback>("Create user"_i18n, [this](){
        ConfirmCreate();
    }, true, "Add a new local profile (up to 8)."_i18n);

    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Rename"_i18n, [this](){
            ConfirmRename();
        }, true, "Change this profile's display name."_i18n);
        options->Add<SidebarEntryCallback>("Change avatar"_i18n, [this](){
            ConfirmChangeAvatar();
        }, true, "Pick an existing profile avatar, an SD image, or SteamGridDB."_i18n);
        options->Add<SidebarEntryCallback>("Delete user"_i18n, [this](){
            ConfirmDelete();
        }, true, "Remove the profile after a hold confirm. You can back up first. Saves are deleted after."_i18n);
    }

    options->Add<SidebarEntryHeader>("BACKUP & RESTORE USER"_i18n);
    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Backup user"_i18n, [this](){
            ConfirmBackup();
        }, true, "Back up name, avatar, Nintendo Account link and this user's play hours to SD."_i18n);
    }
    options->Add<SidebarEntryCallback>("Restore Backup"_i18n, [this](){
        ConfirmRestoreBackup();
    }, true, "Restore backups: name, avatar and Nintendo Account link. Play hours are not restored by this action."_i18n);

    options->Add<SidebarEntryHeader>("CONSOLE MOVE"_i18n);
    options->Add<SidebarEntryCallback>("Backup profiles & play hours"_i18n, [this](){
        ConfirmNandBackup();
    }, true, "All profiles on this console plus play hours, same user IDs. For moving to another console. If a save is locked, Hub skips it and leaves a TegraExplorer script."_i18n);
    options->Add<SidebarEntryCallback>("Restore profiles & play hours"_i18n, [this](){
        ConfirmNandRestore();
    }, true, "Write that pack into this console. Hours and profiles here are replaced. If a save is locked, use restore.te in TegraExplorer."_i18n);

    options->Add<SidebarEntryHeader>("NINTENDO ACCOUNT"_i18n);
    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Link Nintendo Account"_i18n, [this](){
            ConfirmLinkNintendoAccount();
        }, true, "Link all currently unlinked profiles to the official Nintendo Account donor. Already linked profiles will not be changed. Console will reboot."_i18n);
        options->Add<SidebarEntryCallback>("Unlink Nintendo Account"_i18n, [this](){
            ConfirmUnlinkNintendoAccount();
        }, true, "Remove Nintendo Account link data from selected profiles, or from all linked profiles if none are selected. Console will reboot."_i18n);
    }

    options->Add<SidebarEntryHeader>("VIEW"_i18n);
    SidebarEntryArray::Items layout_items;
    layout_items.push_back("List"_i18n);
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    auto layout_index = m_layout.Get();
    if (layout_index > LayoutType::LayoutType_GridDetail) {
        layout_index = LayoutType::LayoutType_GridDetail;
    }
    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_layout.Set(index_out);
        OnLayoutChange();
    }, layout_index, "Choose how user profiles are displayed."_i18n);
}

void Menu::ConfirmCreate() {
    if (m_items.size() >= ACC_USER_LIST_SIZE) {
        App::Push<OptionBox>("The console already has 8 user profiles."_i18n, "OK"_i18n);
        return;
    }
    const auto rc = pselShowUserCreator();
    App::ResetTouchAfterApplet();
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Could not open user creator."_i18n);
        return;
    }
    Refresh();
}

void Menu::ConfirmRename() {
    if (m_items.empty()) {
        return;
    }
    std::string name;
    if (R_FAILED(swkbd::ShowText(name, "Rename"_i18n.c_str(), m_items[m_index].nickname.c_str(), 1, 31)) || name.empty()) {
        return;
    }
    RunRename(name);
}

void Menu::ConfirmChangeAvatar() {
    if (m_items.empty()) {
        return;
    }
    App::Push(std::make_unique<AvatarPickMenu>([this](std::vector<u8> jpeg) {
        if (!jpeg.empty()) {
            RunSetAvatar(std::move(jpeg));
        }
    }));
}

void Menu::ConfirmBackup() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    App::Push<OptionBox>(
        "This backup will reboot the console."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this, uids](auto op) {
            if (!op || *op != 1) {
                return;
            }
            const auto packs = account_user::ListUserPacks();
            std::string existing_when;
            u32 existing_users = 0;
            for (const auto& uid : uids) {
                const auto hex = account_link::UidHex(uid);
                for (const auto& p : packs) {
                    if (!p.uid_hex.empty() && p.uid_hex == hex) {
                        existing_users++;
                        if (existing_when.empty()) {
                            existing_when = !p.created_label.empty() ? p.created_label : p.folder_name;
                        }
                        break;
                    }
                }
            }
            if (existing_users == 0) {
                RunBackup(uids, false);
                return;
            }
            std::string msg;
            if (uids.size() == 1) {
                msg = "A backup of this user already exists"_i18n + " (" + existing_when + "). " +
                    "Overwrite it, or keep both?"_i18n;
            } else {
                msg = "One or more of these users already have a backup. Overwrite, or keep both?"_i18n;
            }
            App::Push<OptionBox>(msg, "Cancel"_i18n, "Overwrite"_i18n, "Keep both"_i18n, 1,
                [this, uids](auto op2) {
                    if (!op2 || *op2 == 0) {
                        return;
                    }
                    RunBackup(uids, *op2 == 1);
                });
        });
}

void Menu::ConfirmNandBackup() {
    App::Push<OptionBox>(
        "Copy every profile on this console plus their play hours to SD (same users, with hours). If the system holds a save, Hub skips it and writes a TegraExplorer script instead of killing services. Not the same as Backup user."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunNandBackup();
            }
        });
}

void Menu::ConfirmNandRestore() {
    App::Push<OptionBox>(
        "Write a profiles & play hours pack into this console? Users and hours here will be replaced. If a save is locked, use restore.te in TegraExplorer. Back up SYSTEM first. Y selects the pack folder."_i18n,
        "Cancel"_i18n, "Choose folder"_i18n, 1,
        [this](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[this](const fs::FsPath& path, const filebrowser::FsEntry&) -> bool {
                    RunNandRestore(path.toString());
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{paths::DATA_ROOT + "/nand_transfer"},
                true);
        });
}

void Menu::ConfirmRestoreBackup() {
    App::Push<RestoreSourceMenu>([this](std::vector<account_user::Pack> picked) {
        ConfirmPickedRestorePacks(std::move(picked));
    });
}

void Menu::ConfirmPickedRestorePacks(std::vector<account_user::Pack> picked) {
    if (picked.empty()) {
        return;
    }
    u32 new_slots = 0;
    u32 replace_slots = 0;
    std::string existing_name;
    for (const auto& p : picked) {
        if (const auto live = FindLiveUidForPack(p)) {
            replace_slots++;
            if (existing_name.empty()) {
                existing_name = LiveNameForUid(*live);
                if (existing_name.empty()) {
                    existing_name = p.nickname;
                }
            }
        } else {
            new_slots++;
        }
    }
    const auto available_slots = ACC_USER_LIST_SIZE - m_items.size();
    if (new_slots > available_slots) {
        App::Push<OptionBox>(
            "Cannot restore: selecting " + std::to_string(new_slots) +
            " new profile(s) would exceed the maximum of 8 users (available: " +
            std::to_string(available_slots) + ")."_i18n, "OK"_i18n);
        return;
    }
    bool any_new_link = false;
    for (const auto& p : picked) {
        if (p.link_valid && !FindLiveUidForPack(p)) {
            any_new_link = true;
            break;
        }
    }
    std::string gate_reason;
    if (any_new_link && account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }
    auto picked_packs = std::move(picked);
    auto go = [this, picked_packs](auto op) mutable {
        if (op && *op == 1) {
            RunPrepareRestoreSnapshot(std::move(picked_packs));
        }
    };
    if (replace_slots && !new_slots) {
        App::Push<OptionBox>(
            "This user is already on this console"_i18n +
            (existing_name.empty() ? std::string(".") : (" (" + existing_name + ").")) + "\n\n" +
            "Restore will replace that profile: name and avatar only. The existing Nintendo Account link is left as-is. It will not add a second user.\n\n"
            "We still copy the current account save to SD first, in case something goes wrong."_i18n,
            "Cancel"_i18n, "Replace"_i18n, 1, std::move(go));
        return;
    }
    if (replace_slots && new_slots) {
        App::Push<OptionBox>(
            std::to_string(replace_slots) + " " +
            "backup(s) match accounts already on this console and will replace them. "_i18n +
            std::to_string(new_slots) + " " +
            "will be added as new profiles.\n\n"
            "We copy the current account save to SD first, in case something goes wrong."_i18n,
            "Cancel"_i18n, "Continue"_i18n, 1, std::move(go));
        return;
    }
    App::Push<OptionBox>(
        "Restore will add a profile and may change the account save (0010).\n\n"
        "Before that, we copy the raw 0010 save file to SD. That file is the rollback if something goes wrong.\n\n"
        "• If Hub can read it now, the copy happens here.\n"
        "• If not, TegraExplorer will copy it automatically after OK.\n\n"
        "Then open Kefir Hub yourself to continue the restore."_i18n,
        "Cancel"_i18n, "Continue"_i18n, 1, std::move(go));
}

void Menu::RunPrepareRestoreSnapshot(std::vector<account_user::Pack> packs) {
    if (packs.empty()) {
        return;
    }
    auto dirs = std::make_shared<std::vector<std::string>>();
    for (const auto& p : packs) {
        dirs->push_back(p.dir);
    }
    auto live_ok = std::make_shared<bool>(false);
    App::Push<ProgressBox>(0, "Snapshot account save"_i18n, "Snapshot account save"_i18n,
        [dirs, live_ok](auto pbox) -> Result {
            pbox->NewTransfer("Copying 0010"_i18n);
            account_restore::InstallRestoreTeScripts();
            const auto dump_rc = account_restore::Dump0010ReadOnly(pbox);
            if (R_SUCCEEDED(dump_rc) && account_restore::SnapshotOk()) {
                *live_ok = true;
                R_TRY(account_restore::SavePending(*dirs, "ready", true));
                R_SUCCEED();
            }
            log_write("[RESTORE] live 0010 dump failed 0x%X, TE fallback\n", dump_rc);
            R_TRY(account_restore::WriteNandFlag());
            R_TRY(account_restore::SavePending(*dirs, "wait_dump", false));
            R_SUCCEED();
        },
        [live_ok, packs = std::move(packs)](Result rc) mutable {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Could not prepare the 0010 snapshot."_i18n, "OK"_i18n);
                return;
            }
            if (*live_ok) {
                log_write("[RESTORE] live dump ok; continuing restore in same session\n");
                StartRestoreBackup(std::move(packs));
                return;
            }
            App::Push<OptionBox>(
                "Hub could not copy the raw account save while the system is running. Horizon is holding it.\n\n"
                "We still need that file before restore. It is the rollback if the console later fails to boot.\n\n"
                "After OK:\n"
                "• TegraExplorer starts and copies 8000000000000010 by itself.\n"
                "• When it finishes, the console returns to CFW.\n"
                "• Open Kefir Hub yourself. We will continue the restore."_i18n,
                "OK"_i18n,
                [](auto op) {
                    if (!op) {
                        return;
                    }
                    if (!account_restore::LaunchTegraDump()) {
                        App::Push<OptionBox>(
                            "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try Restore Backup again."_i18n,
                            "OK"_i18n);
                    }
                });
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

auto OfferPendingRestore() -> bool {
    static bool s_offered = false;
    if (s_offered) {
        return false;
    }
    auto pending = account_restore::LoadPending();
    if (pending.rolled_back) {
        s_offered = true;
        account_restore::ClearPending();
        App::Push<OptionBox>(
            "Account save 0010 was rolled back. Restore was cancelled."_i18n,
            "OK"_i18n);
        return true;
    }
    if (!pending.present || pending.phase == "applied") {
        return false;
    }

    if (pending.phase == "wait_link") {
        s_offered = true;
        fs::FsNativeSd sd;
        if (sd.FileExists(account_restore::LinkAppliedOkPath())) {
            account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok);
            App::Push<OptionBox>(
                "Profile created and Nintendo Account link applied. Reboot is done."_i18n,
                "OK"_i18n);
        } else {
            App::Push<OptionBox>(
                "TegraExplorer did not apply the Nintendo Account link.\n\n"
                "An extra unlinked profile may exist on this console.\n"
                "If the console will not boot: hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te"_i18n,
                "OK"_i18n);
        }
        return true;
    }

    bool upgraded_from_wait_dump = false;
    if (pending.phase == "wait_dump") {
        if (account_restore::SnapshotOk()) {
            account_restore::SavePending(pending.pack_dirs, "ready", true);
            pending.phase = "ready";
            pending.snapshot_ok = true;
            upgraded_from_wait_dump = true;
            log_write("[RESTORE] wait_dump→ready (snapshot on SD)\n");
        } else {
            s_offered = true;
            App::Push<OptionBox>(
                "The account save dump is not on SD yet.\n\n"
                "After OK, TegraExplorer will dump 0010 automatically.\n"
                "When it finishes, open Kefir Hub yourself to continue the restore."_i18n,
                "OK"_i18n,
                [](auto op) {
                    if (!op) {
                        return;
                    }
                    if (!account_restore::LaunchTegraDump()) {
                        App::Push<OptionBox>(
                            "Could not start TegraExplorer. Put TegraExplorer.bin in /bootloader/payloads/ and try again."_i18n,
                            "OK"_i18n);
                    }
                });
            return true;
        }
    }

    if (pending.phase != "ready" || pending.pack_dirs.empty()) {
        return false;
    }
    // Persist ready if state still had has_0010=false from an older Hub.
    if (pending.snapshot_ok) {
        account_restore::SavePending(pending.pack_dirs, "ready", true);
    }

    s_offered = true;
    std::vector<account_user::Pack> packs;
    for (const auto& dir : pending.pack_dirs) {
        auto pack = account_user::FindUserPack(dir);
        if (!pack.dir.empty()) {
            packs.push_back(std::move(pack));
        }
    }
    if (packs.empty()) {
        App::Push<OptionBox>("Pending restore packs are missing from SD."_i18n, "OK"_i18n);
        return true;
    }

    // User already confirmed Restore before TE; dump is on SD — continue without re-prompt.
    if (upgraded_from_wait_dump) {
        log_write("[RESTORE] auto-continuing StartRestoreBackup after TE dump\n");
        StartRestoreBackup(std::move(packs));
        return true;
    }

    App::Push<OptionBox>(
        "Unfinished restore is ready (0010 snapshot is on SD).\n\n"
        "Continue will create the profile, then reboot the console.\n\n"
        "If it does not boot:\n"
        "• hekate > payloads > tegraexplorer > Undo_restore_if_wont_boot.te\n"
        "• TegraExplorer is controlled with the power and volume buttons.\n"
        "• That puts the old profiles back and cancels this restore."_i18n,
        "Later"_i18n, "Cancel restore"_i18n, "Continue"_i18n, 2,
        [packs = std::move(packs)](auto op) mutable {
            if (!op) {
                return;
            }
            if (*op == 1) {
                account_restore::ClearPending();
                App::Push<OptionBox>("Restore cancelled. The 0010 snapshot was removed."_i18n, "OK"_i18n);
                return;
            }
            if (*op == 2) {
                StartRestoreBackup(std::move(packs));
            }
        });
    return true;
}

void Menu::ConfirmDelete() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    const auto msg = (uids.size() > 1)
        ? "Delete the selected users? This cannot be undone. Their game saves will also be deleted. Hold A to confirm."_i18n
        : "Delete this user? This cannot be undone. Their game saves will also be deleted. Hold A to confirm."_i18n;
    App::Push<HoldConfirmBox>(msg, [this, uids](bool ok) {
        if (!ok) {
            return;
        }
        App::Push<OptionBox>(
            "Back up the user profile (name, avatar, Nintendo link) first?"_i18n,
            "Skip"_i18n, "Backup account"_i18n, 1,
            [this, uids](auto op) {
                if (!op) {
                    return;
                }
                const bool backup_account = *op == 1;
                auto saves = CollectSaves(uids);
                if (saves.empty()) {
                    RunDelete(backup_account, {});
                    return;
                }
                App::Push<OptionBox>(
                    "Back up game saves for these users? You can pick which games."_i18n,
                    "Skip"_i18n, "Choose saves"_i18n, 1,
                    [this, backup_account, saves](auto op) mutable {
                        if (!op) {
                            return;
                        }
                        if (*op == 0) {
                            RunDelete(backup_account, {});
                            return;
                        }
                        App::Push<SavePickMenu>(std::move(saves), [this, backup_account](auto picked) {
                            if (!picked) {
                                return;
                            }
                            RunDelete(backup_account, std::move(*picked));
                        });
                    });
            });
    });
}

void Menu::ConfirmLinkNintendoAccount() {
    std::string gate_reason;
    if (account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }

    bool any_needed = false;
    for (const auto& u : m_items) {
        if (u.linked_known && !u.horizon_linked) {
            any_needed = true;
            break;
        }
    }
    if (!any_needed) {
        App::Push<OptionBox>("All user profiles are already linked."_i18n, "OK"_i18n);
        return;
    }

    App::Push<OptionBox>(
        "Link all currently unlinked profiles to the official Nintendo Account donor? Already linked profiles will not be changed. The console will reboot immediately."_i18n,
        "Cancel"_i18n, "Link and reboot"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunLinkNintendoAccount();
            }
        });
}

void Menu::RunRename(const std::string& nickname) {
    if (m_items.empty()) {
        return;
    }
    const auto uid = m_items[m_index].uid;
    App::Push<ProgressBox>(0, "Rename"_i18n, nickname, [uid, nickname](auto pbox) -> Result {
        pbox->NewTransfer("Renaming user"_i18n);
        R_TRY(account_user::Rename(uid, nickname));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Could not rename the user."_i18n, "OK"_i18n);
            return;
        }
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunSetAvatar(std::vector<u8> jpeg) {
    if (m_items.empty()) {
        return;
    }
    const auto uid = m_items[m_index].uid;
    App::Push<ProgressBox>(0, "Change avatar"_i18n, "Change avatar"_i18n, [uid, jpeg = std::move(jpeg)](auto pbox) -> Result {
        pbox->NewTransfer("Writing avatar"_i18n);
        R_TRY(account_user::SetImageJpeg(uid, jpeg));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Could not change the avatar."_i18n, "OK"_i18n);
            return;
        }
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunNandBackup() {
    auto report = std::make_shared<nand_transfer::Report>();
    App::Push<ProgressBox>(0, "Backup profiles & play hours"_i18n, "Backup profiles & play hours"_i18n,
        [report](auto pbox) -> Result {
            R_TRY(nand_transfer::Export(pbox, *report));
            R_SUCCEED();
        }, [this, report](Result rc) {
            if (R_FAILED(rc) || report->dir.empty()) {
                App::Push<OptionBox>(
                    "Horizon would not open the system saves. hekate > payloads > tegraexplorer, run dump.te (also under TegraExplorer/scripts). That dumps play hours without Horizon."_i18n,
                    "OK"_i18n);
                return;
            }
            std::string msg = "Copied to "_i18n + report->dir + ". ";
            if (report->save_0010 && report->save_00F0) {
                msg += "Profiles and play hours are in the pack. On the other console: Restore profiles & play hours."_i18n;
            } else {
                if (report->save_0010) {
                    msg += "Profiles copied. "_i18n;
                } else {
                    msg += "Profiles were locked by the system. "_i18n;
                }
                if (report->save_00F0) {
                    msg += "Play hours copied. "_i18n;
                } else {
                    msg += "Play hours were locked. hekate > payloads > tegraexplorer, dump.te (or TegraExplorer/scripts/dump.te). "_i18n;
                }
                msg += "On the other console: Restore profiles & play hours, or restore.te if a save stays locked."_i18n;
            }
            App::Push<OptionBox>(msg, "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunNandRestore(const std::string& dir) {
    if (!nand_transfer::IsPack(dir)) {
        App::Push<OptionBox>(
            "That folder is not a profiles & play hours pack."_i18n,
            "OK"_i18n);
        return;
    }
    auto report = std::make_shared<nand_transfer::Report>();
    App::Push<ProgressBox>(0, "Restore profiles & play hours"_i18n, "Restore profiles & play hours"_i18n,
        [dir, report](auto pbox) -> Result {
            pbox->NewTransfer("Writing system saves"_i18n);
            R_TRY(nand_transfer::Import(pbox, dir, *report));
            R_SUCCEED();
        }, [this, report](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsInvalidType)
                    ? "That folder is not a profiles & play hours pack."_i18n
                    : "Horizon would not open the system saves to write. hekate > payloads > tegraexplorer, run restore.te from the pack (also under TegraExplorer/scripts)."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
            std::string msg = "Wrote pack into this NAND. Reboot required. "_i18n;
            if (!report->save_00F0) {
                msg += "Play hours (00F0) could not be opened. Close games and retry, or use restore.te. "_i18n;
            }
            if (!report->save_0010) {
                msg += "Account save (0010) could not be opened. "_i18n;
            }
            App::Push<OptionBox>(
                msg,
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op) {
                    if (op && *op == 1) {
                        utils::requestForcedReboot();
                    }
                });
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunBackup(std::vector<AccountUid> uids, bool overwrite_existing) {
    if (uids.empty()) {
        return;
    }
    auto dirs = std::make_shared<std::vector<std::string>>();
    App::Push<ProgressBox>(0, "Backup user"_i18n, "Backup user"_i18n,
        [uids = std::move(uids), dirs, overwrite_existing](auto pbox) -> Result {
        pbox->NewTransfer("Writing user pack"_i18n);
        R_TRY(account_user::ExportUserPacks(uids, *dirs, overwrite_existing));
        R_SUCCEED();
    }, [dirs](Result rc) {
        const bool terminated = account_link::ConsumeAccountDaemonsTerminated();
        if (R_FAILED(rc) || dirs->empty()) {
            if (terminated) {
                utils::requestForcedReboot();
                return;
            }
            App::Push<OptionBox>("Could not write the user pack."_i18n, "OK"_i18n);
            return;
        }
        utils::requestForcedReboot();
    }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

namespace {

struct RestoreReport {
    u32 profiles_restored{};
    u32 created_count{};
    u32 replaced_count{};
    u32 links_staged{};
    u32 unlinked_restored{};
    u32 link_malformed_count{};
    u32 failed_creations{};
    bool link_stage_failed{};
    bool needs_te_link{};
};

auto StageCreateLinkForTe(fs::FsNativeSd& sd, const AccountUid& dest_uid, const account_link::LinkPackage& pkg) -> Result {
    R_TRY(sd.CreateDirectoryRecursively(account_restore::LinkBaasDir()));
    R_TRY(sd.CreateDirectoryRecursively(account_restore::LinkNasDir()));

    if (pkg.baas_data.size() < 24) {
        log_write("[USER] StageCreateLinkForTe: baas too small\n");
        return Result_FsInvalidType;
    }
    auto baas_for_uid = pkg.baas_data;
    std::memcpy(baas_for_uid.data(), &dest_uid, sizeof(AccountUid));
    const auto baas_name = account_link::UidDashedLinkalho(dest_uid) + ".dat";
    const auto baas_path = std::string(account_restore::LinkBaasDir()) + "/" + baas_name;
    R_TRY(sd.write_entire_file(baas_path.c_str(), baas_for_uid));

    for (const auto& nf : pkg.nas_files) {
        const auto nas_path = std::string(account_restore::LinkNasDir()) + "/" + nf.filename;
        R_TRY(sd.write_entire_file(nas_path.c_str(), nf.data));
    }

    const auto uid_txt =
        "rfc=" + account_link::UidDashedRfc(dest_uid) + "\n" +
        "linkalho=" + account_link::UidDashedLinkalho(dest_uid) + "\n" +
        "baas=" + baas_name + "\n";
    R_TRY(sd.write_entire_file(
        (std::string(account_restore::LinkStagingDir()) + "/uid.txt").c_str(),
        std::vector<u8>(uid_txt.begin(), uid_txt.end())));

    log_write("[USER] staged TE link baas=%s nas_files=%zu\n",
        baas_name.c_str(), pkg.nas_files.size());
    R_SUCCEED();
}

} // namespace

void Menu::RunRestoreBackup(std::vector<account_user::Pack> picked_packs) {
    StartRestoreBackup(std::move(picked_packs));
}

void StartRestoreBackup(std::vector<account_user::Pack> picked_packs) {
    auto report = std::make_shared<RestoreReport>();
    App::Push<ProgressBox>(0, "Restore Backup"_i18n, "Restoring profiles..."_i18n,
        [picked_packs = std::move(picked_packs), report](auto pbox) mutable -> Result {
            fs::FsNativeSd sd;
            std::vector<account_link::TargetLink> links_to_stage;

            // Fresh staging dir; never leave stale baas/nas for TE.
            if (sd.DirExists(account_restore::LinkStagingDir())) {
                sd.DeleteDirectoryRecursively(account_restore::LinkStagingDir());
            }
            sd.DeleteFile(account_restore::LinkAppliedOkPath());

            for (size_t i = 0; i < picked_packs.size(); i++) {
                const auto& p = picked_packs[i];
                pbox->NewTransfer("Restoring user profile"_i18n);

                std::vector<u8> jpeg;
                sd.read_entire_file((p.dir + "/avatar.jpg").c_str(), jpeg);
                const std::string name = !p.nickname.empty() ? p.nickname : "User";

                account_link::LinkPackage pkg;
                const auto link_load_rc = account_link::LoadUserPackLinkPackage(p.dir, pkg);
                AccountUid dest_uid{};
                bool have_dest = false;
                bool created = false;

                if (const auto live = FindLiveUidForPack(p)) {
                    dest_uid = *live;
                    have_dest = true;
                    log_write("[USER] Replace onto live uid %s (pack uid %s nas %llx); name/avatar only, no TE link\n",
                        account_link::UidHex(dest_uid).c_str(),
                        p.uid_hex.c_str(),
                        static_cast<unsigned long long>(p.nas_id));
                    const auto rename_rc = account_user::Rename(dest_uid, name);
                    if (R_FAILED(rename_rc)) {
                        log_write("[USER] rename existing 0x%X\n", rename_rc);
                    }
                    if (!jpeg.empty()) {
                        const auto av_rc = account_user::SetImageJpeg(dest_uid, jpeg);
                        if (R_FAILED(av_rc)) {
                            log_write("[USER] avatar existing 0x%X\n", av_rc);
                        }
                    }
                    report->profiles_restored++;
                    report->replaced_count++;
                }

                if (!have_dest) {
                    log_write("[USER] Create new uid for pack %s nas %llx\n",
                        p.uid_hex.c_str(),
                        static_cast<unsigned long long>(p.nas_id));
                    const auto create_rc = account_user::Create(name, dest_uid, jpeg);
                    if (R_FAILED(create_rc)) {
                        log_write("[USER] Create user failed 0x%X\n", create_rc);
                        report->failed_creations++;
                        continue;
                    }
                    have_dest = true;
                    created = true;
                    report->profiles_restored++;
                    report->created_count++;
                }

                // Create + valid baas/nas → SD staging for TE. Never ApplyLinkPackages / Horizon 0010 write.
                // Replace (proven nas): name/avatar only — no TE link, no 0010 write.
                if (created && R_SUCCEEDED(link_load_rc) && pkg.nas_id != 0) {
                    links_to_stage.push_back({dest_uid, std::move(pkg)});
                } else if (have_dest && !created) {
                    // Replace path intentionally skips link rewrite.
                } else if (have_dest && !sd.DirExists((p.dir + "/baas").c_str())) {
                    report->unlinked_restored++;
                } else if (have_dest) {
                    log_write("[USER] Link package invalid in %s (0x%X)\n", p.dir.c_str(), link_load_rc);
                    report->link_malformed_count++;
                    report->unlinked_restored++;
                }
                // Pack may still contain pdm/playtime; Restore Backup ignores it.
            }

            if (!links_to_stage.empty()) {
                pbox->NewTransfer("Staging Nintendo Account link for TegraExplorer"_i18n);
                for (const auto& target : links_to_stage) {
                    const auto stage_rc = StageCreateLinkForTe(sd, target.uid, target.pkg);
                    if (R_FAILED(stage_rc)) {
                        log_write("[USER] StageCreateLinkForTe failed 0x%X\n", stage_rc);
                        report->link_stage_failed = true;
                        R_TRY(stage_rc);
                    }
                    report->links_staged++;
                }
                auto pending = account_restore::LoadPending();
                const auto pack_dirs = pending.present ? pending.pack_dirs : std::vector<std::string>{};
                const bool snap_ok = pending.present ? pending.snapshot_ok : account_restore::SnapshotOk();
                if (!pack_dirs.empty()) {
                    R_TRY(account_restore::SavePending(pack_dirs, "wait_link", snap_ok));
                } else {
                    // Snapshot prep should have written packs; keep wait_link even if state was cleared.
                    std::vector<std::string> dirs;
                    for (const auto& p : picked_packs) {
                        dirs.push_back(p.dir);
                    }
                    R_TRY(account_restore::SavePending(dirs, "wait_link", snap_ok));
                }
                report->needs_te_link = true;
                log_write("[USER] Create link staged; wait_link for TE apply (%u)\n", report->links_staged);
            }

            R_SUCCEED();
        },
        [report](Result /*rc*/) {
            if (report->profiles_restored == 0) {
                App::Push<OptionBox>("Could not restore user profiles."_i18n, "OK"_i18n);
                return;
            }

            if (report->needs_te_link && !report->link_stage_failed) {
                log_write("[USER] launching account_0010_apply_link.te\n");
                if (!account_restore::LaunchTegraRomfs(account_restore::ApplyLinkTeName())) {
                    App::Push<OptionBox>(
                        "Profile was created, but TegraExplorer could not start to apply the Nintendo Account link.\n\n"
                        "Put TegraExplorer.bin in /bootloader/payloads/ and open Kefir Hub again, or run Undo if the console will not boot."_i18n,
                        "OK"_i18n);
                }
                return;
            }

            auto pending = account_restore::LoadPending();
            if (pending.present) {
                account_restore::SavePending(pending.pack_dirs, "applied", pending.snapshot_ok);
            }

            if (report->link_stage_failed || report->link_malformed_count) {
                std::string msg = "Restored " + std::to_string(report->profiles_restored) + " user profile(s)."_i18n;
                msg += " " + "Nintendo Account link data was invalid or could not be applied."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }

            // Create without link: reboot for a clean user list. Replace-only: stay in Hub.
            if (report->created_count > 0) {
                log_write("[USER] Create without TE link; rebooting for clean user list\n");
                utils::requestForcedReboot();
                return;
            }

            std::string msg = "Restored " + std::to_string(report->profiles_restored) + " user profile(s)."_i18n;
            if (report->replaced_count > 0) {
                msg += " " + "Name and avatar updated on existing profile(s)."_i18n;
            }
            if (report->unlinked_restored > 0) {
                msg += " " + std::to_string(report->unlinked_restored) + " profile(s) restored without link."_i18n;
            }
            App::Push<OptionBox>(msg, "OK"_i18n);
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunDelete(bool backup_account, std::vector<save::Entry> save_backup) {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    auto all_saves = CollectSaves(uids);
    auto helper = std::make_shared<save::Menu>(MenuFlag_None);
    App::Push<ProgressBox>(0, "Delete user"_i18n, "Delete user"_i18n,
        [uids, backup_account, save_backup = std::move(save_backup), all_saves = std::move(all_saves), helper](auto pbox) mutable -> Result {
            if (backup_account) {
                pbox->NewTransfer("Backing up account"_i18n);
                std::vector<std::string> dirs;
                R_TRY(account_user::ExportUserPacks(uids, dirs));
            }
            if (!save_backup.empty()) {
                pbox->NewTransfer("Backing up saves"_i18n);
                R_TRY(helper->BackupSavesOn(pbox, save_backup));
            }
            pbox->NewTransfer("Deleting users"_i18n);
            for (const auto& uid : uids) {
                R_TRY(account_user::Delete(uid));
            }
            if (!all_saves.empty()) {
                pbox->NewTransfer("Deleting saves"_i18n);
                R_TRY(helper->DeleteSavesOn(pbox, all_saves));
            }
            R_SUCCEED();
        }, [this](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Could not delete the user."_i18n, "OK"_i18n);
                Refresh();
                return;
            }
            App::Push<OptionBox>("User deleted."_i18n, "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunLinkNintendoAccount() {
    App::Push<ProgressBox>(0, "Link Nintendo Account"_i18n, "Linking account..."_i18n,
        [](auto pbox) -> Result {
            pbox->NewTransfer("Applying Nintendo Account link"_i18n);
            u32 count = 0;
            R_TRY(account_link::LinkAllFromRomfsDonor(count));
            R_SUCCEED();
        },
        [this](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Failed to link Nintendo Account."_i18n, "OK"_i18n);
            } else {
                utils::requestForcedReboot();
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::ConfirmUnlinkNintendoAccount() {
    std::string gate_reason;
    if (account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }

    std::vector<AccountUid> targets;
    if (m_selected_count > 0) {
        for (const auto& u : m_items) {
            if (u.selected && u.linked_known && u.horizon_linked) {
                targets.push_back(u.uid);
            }
        }
    } else {
        for (const auto& u : m_items) {
            if (u.linked_known && u.horizon_linked) {
                targets.push_back(u.uid);
            }
        }
    }

    if (targets.empty()) {
        App::Push<OptionBox>("No linked profiles to unlink."_i18n, "OK"_i18n);
        return;
    }

    const auto msg = (m_selected_count > 0)
        ? "Unlink Nintendo Account from the selected linked profiles? Link data is removed from the system save. The console will reboot immediately."_i18n
        : "Unlink Nintendo Account from all linked profiles? Link data is removed from the system save. The console will reboot immediately."_i18n;

    App::Push<OptionBox>(
        msg,
        "Cancel"_i18n, "Unlink and reboot"_i18n, 1,
        [this, targets = std::move(targets)](auto op) mutable {
            if (op && *op == 1) {
                RunUnlinkNintendoAccount(std::move(targets));
            }
        });
}

void Menu::RunUnlinkNintendoAccount(std::vector<AccountUid> uids) {
    App::Push<ProgressBox>(0, "Unlink Nintendo Account"_i18n, "Unlinking account..."_i18n,
        [uids = std::move(uids)](auto pbox) -> Result {
            pbox->NewTransfer("Removing Nintendo Account link"_i18n);
            u32 count = 0;
            R_TRY(account_link::UnlinkLinkedProfiles(uids, count));
            R_SUCCEED();
        },
        [](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Failed to unlink Nintendo Account."_i18n, "OK"_i18n);
            } else {
                utils::requestForcedReboot();
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

} // namespace sphaira::ui::menu::users
