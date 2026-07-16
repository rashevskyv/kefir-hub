#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "dumper.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "utils/utils.hpp"

#include "ui/menus/game_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"

#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/es.hpp"
#include "yati/container/base.hpp"
#include "yati/container/nsp.hpp"

#include <utility>
#include <cstring>
#include <algorithm>
#include <minIni.h>

namespace sphaira::ui::menu::game {
namespace {

struct ContentInfoEntry {
    NsApplicationContentMetaStatus status{};
    std::vector<NcmContentInfo> content_infos{};
    std::vector<NcmRightsId> ncm_rights_id{};
};

struct TikEntry {
    FsRightsId id{};
    u8 key_gen{};
    std::vector<u8> tik_data{};
    std::vector<u8> cert_data{};
};

struct NspEntry {
    // application name.
    std::string application_name{};
    // name of the nsp (name [id][v0][BASE].nsp).
    fs::FsPath path{};
    // tickets and cert data, will be empty if title key crypto isn't used.
    std::vector<TikEntry> tickets{};
    // all the collections for this nsp, such as nca's and tickets.
    std::vector<yati::container::CollectionEntry> collections{};
    // raw nsp data (header, file table and string table).
    std::vector<u8> nsp_data{};
    // size of the entier nsp.
    s64 nsp_size{};
    // copy of ncm cs, it is not closed.
    NcmContentStorage cs{};
    // copy of the icon, if invalid, it will use the default icon.
    int icon{};

    // todo: benchmark manual sdcard read and decryption vs ncm.
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) {
        if (off < nsp_data.size()) {
            *bytes_read = size = ClipSize(off, size, nsp_data.size());
            std::memcpy(buf, nsp_data.data() + off, size);
            R_SUCCEED();
        }

        // adjust offset.
        off -= nsp_data.size();

        for (const auto& collection : collections) {
            if (InRange(off, collection.offset, collection.size)) {
                // adjust offset relative to the collection.
                off -= collection.offset;
                *bytes_read = size = ClipSize(off, size, collection.size);

                if (collection.name.ends_with(".nca")) {
                    const auto id = ncm::GetContentIdFromStr(collection.name.c_str());
                    return ncmContentStorageReadContentIdFile(&cs, buf, size, &id, off);
                } else if (collection.name.ends_with(".tik") || collection.name.ends_with(".cert")) {
                    FsRightsId id;
                    keys::parse_hex_key(&id, collection.name.c_str());

                    const auto it = std::ranges::find_if(tickets, [&id](auto& e){
                        return !std::memcmp(&id, &e.id, sizeof(id));
                    });
                    R_UNLESS(it != tickets.end(), Result_GameBadReadForDump);

                    const auto& data = collection.name.ends_with(".tik") ? it->tik_data : it->cert_data;
                    std::memcpy(buf, data.data() + off, size);
                    R_SUCCEED();
                }
            }
        }

        log_write("did not find collection...\n");
        return 0x1;
    }

private:
    static auto InRange(s64 off, s64 offset, s64 size) -> bool {
        return off < offset + size && off >= offset;
    }

    static auto ClipSize(s64 off, s64 size, s64 file_size) -> s64 {
        return std::min(size, file_size - off);
    }
};

struct NspSource final : dump::BaseSource {
    NspSource(const std::vector<NspEntry>& entries) : m_entries{entries} {
        m_is_file_based_emummc = App::IsFileBaseEmummc();
    }

    Result Read(const std::string& path, void* buf, s64 off, s64 size, u64* bytes_read) override {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(e.path.s) != path.npos;
        });
        R_UNLESS(it != m_entries.end(), Result_GameBadReadForDump);

        const auto rc = it->Read(buf, off, size, bytes_read);
        if (m_is_file_based_emummc) {
            svcSleepThread(2e+6); // 2ms
        }
        return rc;
    }

    auto GetName(const std::string& path) const -> std::string {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(e.path.s) != path.npos;
        });

        if (it != m_entries.end()) {
            return it->application_name;
        }

        return {};
    }

    auto GetSize(const std::string& path) const -> s64 {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(e.path.s) != path.npos;
        });

        if (it != m_entries.end()) {
            return it->nsp_size;
        }

        return 0;
    }

    auto GetIcon(const std::string& path) const -> int override {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(e.path.s) != path.npos;
        });

        if (it != m_entries.end()) {
            return it->icon;
        }

        return App::GetDefaultImage();
    }

private:
    std::vector<NspEntry> m_entries{};
    bool m_is_file_based_emummc{};
};

Result Notify(Result rc, const std::string& error_message) {
    if (R_FAILED(rc)) {
        App::Push<ui::ErrorBox>(rc,
            i18n::get(error_message)
        );
    } else {
        App::Notify("Success"_i18n);
    }

    return rc;
}
Result GetMetaEntries(const Entry& e, title::MetaEntries& out, u32 flags = title::ContentFlag_All) {
    return title::GetMetaEntries(e.app_id, out, flags);
}

bool LoadControlImage(Entry& e, title::ThreadResultData* result) {
    if (!e.image && result && !result->icon.empty()) {
        TimeStamp ts;
        const auto image = ImageLoadFromMemory(result->icon, ImageFlag_JPEG);
        if (!image.data.empty()) {
            e.image = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            return true;
        }
    }

    return false;
}

void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result) {
    if (result) {
        e.status = result->status;
        e.lang = result->lang;
        e.status = result->status;
    }
}

void LoadControlEntry(Entry& e, bool force_image_load = false) {
    if (e.status != title::NacpLoadStatus::Loaded) {
        LoadResultIntoEntry(e, title::Get(e.app_id));
    }

    if (force_image_load && e.status == title::NacpLoadStatus::Loaded) {
        LoadControlImage(e, title::Get(e.app_id));
    }
}

auto isRightsIdValid(FsRightsId id) -> bool {
    FsRightsId empty_id{};
    return 0 != std::memcmp(std::addressof(id), std::addressof(empty_id), sizeof(id));
}



auto BuildNspPath(const Entry& e, const NsApplicationContentMetaStatus& status) -> fs::FsPath {
    fs::FsPath name_buf = e.GetName();
    title::utilsReplaceIllegalCharacters(name_buf, true);

    char version[sizeof(NacpStruct::display_version) + 1]{};
    if (status.meta_type == NcmContentMetaType_Patch) {
        u64 program_id;
        fs::FsPath path;
        if (R_SUCCEEDED(title::GetControlPathFromStatus(status, &program_id, &path))) {
            char display_version[0x10];
            if (R_SUCCEEDED(nca::ParseControl(path, program_id, display_version, sizeof(display_version), nullptr, offsetof(NacpStruct, display_version)))) {
                std::snprintf(version, sizeof(version), "%s ", display_version);
            }
        }
    }

    fs::FsPath path;
    if (App::GetApp()->m_dump_app_folder.Get()) {
        std::snprintf(path, sizeof(path), "%s/%s %s[%016lX][v%u][%s].nsp", name_buf.s, name_buf.s, version, status.application_id, status.version, ncm::GetMetaTypeShortStr(status.meta_type));
    } else {
        std::snprintf(path, sizeof(path), "%s %s[%016lX][v%u][%s].nsp", name_buf.s, version, status.application_id, status.version, ncm::GetMetaTypeShortStr(status.meta_type));
    }

    return path;
}

Result BuildContentEntry(const NsApplicationContentMetaStatus& status, ContentInfoEntry& out) {
    auto& cs = title::GetNcmCs(status.storageID);
    auto& db = title::GetNcmDb(status.storageID);
    const auto app_id = ncm::GetAppId(status.meta_type, status.application_id);

    auto id_min = status.application_id;
    auto id_max = status.application_id;
    // workaround N bug where they don't check the full range in the ID filter.
    // https://github.com/Atmosphere-NX/Atmosphere/blob/1d3f3c6e56b994b544fc8cd330c400205d166159/libraries/libstratosphere/source/ncm/ncm_on_memory_content_meta_database_impl.cpp#L22
    if (status.storageID == NcmStorageId_None || status.storageID == NcmStorageId_GameCard) {
        id_min -= 1;
        id_max += 1;
    }

    s32 meta_total;
    s32 meta_entries_written;
    NcmContentMetaKey key;
    R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(meta_total), std::addressof(meta_entries_written), std::addressof(key), 1, (NcmContentMetaType)status.meta_type, app_id, id_min, id_max, NcmContentInstallType_Full));
    log_write("ncmContentMetaDatabaseList(): AppId: %016lX Id: %016lX total: %d written: %d storageID: %u key.id %016lX\n", app_id, status.application_id, meta_total, meta_entries_written, status.storageID, key.id);
    R_UNLESS(meta_total == 1, Result_GameMultipleKeysFound);
    R_UNLESS(meta_entries_written == 1, Result_GameMultipleKeysFound);

    std::vector<NcmContentInfo> cnmt_infos;
    for (s32 i = 0; ; i++) {
        s32 entries_written;
        NcmContentInfo info_out;
        R_TRY(ncmContentMetaDatabaseListContentInfo(std::addressof(db), std::addressof(entries_written), std::addressof(info_out), 1, std::addressof(key), i));

        if (!entries_written) {
            break;
        }

        // check if we need to fetch tickets.
        NcmRightsId ncm_rights_id;
        R_TRY(ncmContentStorageGetRightsIdFromContentId(std::addressof(cs), std::addressof(ncm_rights_id), std::addressof(info_out.content_id), FsContentAttributes_All));

        if (isRightsIdValid(ncm_rights_id.rights_id)) {
            const auto it = std::ranges::find_if(out.ncm_rights_id, [&ncm_rights_id](auto& e){
                return !std::memcmp(&e, &ncm_rights_id, sizeof(ncm_rights_id));
            });

            if (it == out.ncm_rights_id.end()) {
                out.ncm_rights_id.emplace_back(ncm_rights_id);
            }
        }

        if (info_out.content_type == NcmContentType_Meta) {
            cnmt_infos.emplace_back(info_out);
        } else {
            out.content_infos.emplace_back(info_out);
        }
    }

    // append cnmt at the end of the list, following StandardNSP spec.
    out.content_infos.insert_range(out.content_infos.end(), cnmt_infos);
    out.status = status;
    R_SUCCEED();
}

Result BuildNspEntry(const Entry& e, const ContentInfoEntry& info, const keys::Keys& keys, NspEntry& out) {
    out.application_name = e.GetName();
    out.path = BuildNspPath(e, info.status);
    s64 offset{};

    for (auto& e : info.content_infos) {
        char nca_name[0x200];
        std::snprintf(nca_name, sizeof(nca_name), "%s%s", utils::hexIdToStr(e.content_id).str, e.content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");

        u64 size;
        ncmContentInfoSizeToU64(std::addressof(e), std::addressof(size));

        out.collections.emplace_back(nca_name, offset, size);
        offset += size;
    }

    for (auto& ncm_rights_id : info.ncm_rights_id) {
        const auto rights_id = ncm_rights_id.rights_id;
        const auto key_gen = ncm_rights_id.key_generation;

        TikEntry entry{rights_id, key_gen};
        log_write("rights id is valid, fetching common ticket and cert\n");

        u64 tik_size;
        u64 cert_size;
        R_TRY(es::GetCommonTicketAndCertificateSize(&tik_size, &cert_size, &rights_id));
        log_write("got tik_size: %zu cert_size: %zu\n", tik_size, cert_size);

        entry.tik_data.resize(tik_size);
        entry.cert_data.resize(cert_size);
        R_TRY(es::GetCommonTicketAndCertificateData(&tik_size, &cert_size, entry.tik_data.data(), entry.tik_data.size(), entry.cert_data.data(), entry.cert_data.size(), &rights_id));
        log_write("got tik_data: %zu cert_data: %zu\n", tik_size, cert_size);

        // patch fake ticket / convert personalised to common if needed.
        R_TRY(es::PatchTicket(entry.tik_data, entry.cert_data, key_gen, keys, App::GetApp()->m_dump_convert_to_common_ticket.Get()));

        char tik_name[0x200];
        std::snprintf(tik_name, sizeof(tik_name), "%s%s", utils::hexIdToStr(rights_id).str, ".tik");

        char cert_name[0x200];
        std::snprintf(cert_name, sizeof(cert_name), "%s%s", utils::hexIdToStr(rights_id).str, ".cert");

        out.collections.emplace_back(tik_name, offset, entry.tik_data.size());
        offset += entry.tik_data.size();

        out.collections.emplace_back(cert_name, offset, entry.cert_data.size());
        offset += entry.cert_data.size();

        out.tickets.emplace_back(entry);
    }

    out.nsp_data = yati::container::Nsp::Build(out.collections, out.nsp_size);
    out.cs = title::GetNcmCs(info.status.storageID);

    R_SUCCEED();
}

Result BuildNspEntries(Entry& e, u32 flags, std::vector<NspEntry>& out) {
    LoadControlEntry(e);

    title::MetaEntries meta_entries;
    R_TRY(GetMetaEntries(e, meta_entries, flags));

    keys::Keys keys;
    R_TRY(keys::parse_keys(keys, true));

    for (const auto& status : meta_entries) {
        ContentInfoEntry info;
        R_TRY(BuildContentEntry(status, info));

        NspEntry nsp;
        R_TRY(BuildNspEntry(e, info, keys, nsp));
        out.emplace_back(nsp).icon = e.image;
    }

    R_UNLESS(!out.empty(), Result_GameNoNspEntriesBuilt);
    R_SUCCEED();
}

void FreeEntry(NVGcontext* vg, Entry& e) {
    nvgDeleteImage(vg, e.image);
    e.image = 0;
}

void LaunchEntry(const Entry& e) {
    const auto rc = appletRequestLaunchApplication(e.app_id, nullptr);
    Notify(rc, "Failed to launch application");
}

Result CreateSave(u64 app_id, AccountUid uid) {
    u64 actual_size;
    auto data = std::make_unique<NsApplicationControlData>();
    R_TRY(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, data.get(), sizeof(NsApplicationControlData), &actual_size));

    FsSaveDataAttribute attr{};
    attr.application_id = app_id;
    attr.uid = uid;
    attr.save_data_type = FsSaveDataType_Account;

    FsSaveDataCreationInfo info{};
    info.save_data_size = data->nacp.user_account_save_data_size;
    info.journal_size = data->nacp.user_account_save_data_journal_size;
    info.available_size = data->nacp.user_account_save_data_size; // todo: check what this should be.
    info.owner_id = data->nacp.save_data_owner_id;
    info.save_data_space_id = FsSaveDataSpaceId_User;

    // https://switchbrew.org/wiki/Filesystem_services#CreateSaveDataFileSystem
    FsSaveDataMetaInfo meta{};
    meta.size = 0x40060;
    meta.type = FsSaveDataMetaType_Thumbnail;

    R_TRY(fsCreateSaveDataFileSystem(&attr, &info, &meta));

    R_SUCCEED();
}

struct GameComponentRow {
    NsApplicationContentMetaStatus status{};
    u64 size{};
    u32 content_count{};
    u32 rights_count{};
};

struct GameTicketRow {
    FsRightsId id{};
    u8 key_generation{};
    u8 meta_type{};
    u64 ticket_size{};
    bool personalized{};
};

struct GameSaveRow {
    FsSaveDataInfo info{};
    std::string account{};
};

auto ContentFlagFromMetaType(u8 meta_type) -> u32 {
    return title::ContentMetaTypeToContentFlag(meta_type);
}

Result LoadGameSummary(Entry& entry) {
    if (entry.summary_attempted) {
        return entry.summary_result;
    }

    entry.summary_attempted = true;
    entry.layeredfs = fs::FsNativeSd().DirExists(title::GetContentsPath(entry.app_id));

    title::MetaEntries entries;
    entry.summary_result = GetMetaEntries(entry, entries);
    if (R_FAILED(entry.summary_result)) {
        return entry.summary_result;
    }

    for (const auto& status : entries) {
        ContentInfoEntry info;
        if (const auto rc = BuildContentEntry(status, info); R_FAILED(rc)) {
            entry.summary_result = rc;
            continue;
        }

        entry.content_flags |= ContentFlagFromMetaType(status.meta_type);
        u64 size{};
        for (const auto& content : info.content_infos) {
            u64 content_size{};
            ncmContentInfoSizeToU64(&content, &content_size);
            size += content_size;
        }

        if (status.storageID == NcmStorageId_SdCard) {
            entry.sd_size += size;
        } else if (status.storageID == NcmStorageId_BuiltInUser) {
            entry.nand_size += size;
        }
    }

    return entry.summary_result;
}

void DrawGameBadges(NVGcontext* vg, Theme*, const Vec4& image, const Entry& entry) {
    struct Badge { const char* text; NVGcolor colour; };
    std::array<Badge, 5> badges{};
    size_t count{};

    if (entry.content_flags & title::ContentFlag_Application) {
        badges[count++] = {"Base", nvgRGBA(0, 78, 190, 255)};
    }
    if (entry.content_flags & title::ContentFlag_AddOnContent) {
        badges[count++] = {"DLC", nvgRGBA(112, 35, 175, 255)};
    }
    if (entry.content_flags & (title::ContentFlag_Patch | title::ContentFlag_DataPatch)) {
        badges[count++] = {"Update", nvgRGBA(190, 76, 0, 255)};
    }
    if (entry.layeredfs) {
        badges[count++] = {"LayeredFS", nvgRGBA(0, 112, 58, 255)};
    }
    if (entry.summary_attempted && R_SUCCEEDED(entry.summary_result) && !entry.content_flags && !entry.layeredfs) {
        badges[count++] = {"-", nvgRGBA(180, 24, 24, 255)};
    }

    if (!count) {
        return;
    }

    const bool compact = image.w < 80.f;
    const float font = compact ? 8.f : (image.w < 130.f ? 11.f : 13.f);
    const float height = font + (compact ? 5.f : 7.f);
    const float margin = compact ? 2.f : 5.f;
    const float gap = compact ? 1.f : 3.f;
    float bounds[4]{};
    float width = compact ? 20.f : 26.f;
    for (size_t i = 0; i < count; i++) {
        nvgFontSize(vg, font);
        gfx::textBounds(vg, 0, 0, bounds, badges[i].text);
        width = std::max(width, bounds[2] - bounds[0] + (compact ? 6.f : 12.f));
    }
    width = std::min(width, image.w - margin * 2.f);

    const float x = image.x + margin;
    float y = image.y + margin;
    for (size_t i = 0; i < count; i++) {
        gfx::drawRect(vg, x - 1.f, y - 1.f, width + 2.f, height + 2.f, nvgRGBA(0, 0, 0, 255), 5.f);
        gfx::drawRect(vg, x, y, width, height, badges[i].colour, 4.f);
        gfx::drawText(vg, x + width * 0.5f, y + height * 0.5f, font,
            nvgRGBA(255, 255, 255, 255), badges[i].text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        y += height + gap;
    }
}

struct DetailsMenu final : MenuBase {
    DetailsMenu(Entry entry, std::function<void(u32)> dump_callback)
    : MenuBase{"Game Details"_i18n, MenuFlag_None}
    , m_entry{std::move(entry)}
    , m_dump_callback{std::move(dump_callback)} {
        SetTitleSubHeading(m_entry.GetName());

        auto control = std::make_unique<NsApplicationControlData>();
        u64 actual_size{};
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, m_entry.app_id, control.get(), sizeof(*control), &actual_size))) {
            std::snprintf(m_display_version, sizeof(m_display_version), "%s", control->nacp.display_version);
            m_supported_languages = __builtin_popcount(control->nacp.supported_language_flag);
            m_save_size = control->nacp.user_account_save_data_size;
            m_save_journal_size = control->nacp.user_account_save_data_journal_size;
        }

        m_layeredfs = fs::FsNativeSd().DirExists(title::GetContentsPath(m_entry.app_id));

        title::MetaEntries entries;
        m_load_result = GetMetaEntries(m_entry, entries);
        if (R_SUCCEEDED(m_load_result)) {
            for (const auto& status : entries) {
                ContentInfoEntry info;
                if (const auto rc = BuildContentEntry(status, info); R_FAILED(rc)) {
                    if (R_SUCCEEDED(m_partial_load_result)) {
                        m_partial_load_result = rc;
                    }
                    m_component_failures++;
                    continue;
                }

                GameComponentRow row{};
                row.status = status;
                row.content_count = info.content_infos.size();
                row.rights_count = info.ncm_rights_id.size();
                for (const auto& content : info.content_infos) {
                    u64 size{};
                    ncmContentInfoSizeToU64(&content, &size);
                    row.size += size;
                }
                m_components.emplace_back(row);
            }
        }

        this->SetActions(
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::L3, Action{"Launch"_i18n, [this](){ LaunchEntry(m_entry); }}),
            std::make_pair(Button::A, Action{"Actions"_i18n, [this](){ ShowComponentActions(); }}),
            std::make_pair(Button::START, Action{"Game actions"_i18n, [this](){ ShowGameActions(); }})
        );

        const Vec4 row{420, 245, 810, 66};
        const Vec2 pad{0, 8};
        m_list = std::make_unique<List>(1, 5, m_pos, row, pad);
    }

    auto GetShortTitle() const -> const char* override { return "Game Details"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        m_list->OnUpdate(controller, touch, m_index, m_components.size(), [this](bool touch, auto index){
            if (touch && m_index == index) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_index = index;
            }
        }, this);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);

        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawRect(vg, 30, 90, 350, 555, theme->GetColour(ThemeEntryID_GRID));
        gfx::drawImage(vg, 77, 115, 256, 256, m_entry.image ? m_entry.image : App::GetDefaultImage());

        gfx::drawTextArgs(vg, 55, 395, 23.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "%s", m_entry.GetName());
        gfx::drawTextArgs(vg, 55, 428, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", m_entry.GetAuthor());
        gfx::drawTextArgs(vg, 55, 470, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Title ID  %016lX", m_entry.app_id);
        gfx::drawTextArgs(vg, 55, 500, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Version   %s", m_display_version[0] ? m_display_version : "—");
        gfx::drawTextArgs(vg, 55, 530, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Languages %u", m_supported_languages);
        gfx::drawTextArgs(vg, 55, 560, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Contents folder %s", m_layeredfs ? "Present" : "Absent");
        gfx::drawTextArgs(vg, 55, 590, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Save quota %.2f MB + %.2f MB journal",
            static_cast<double>(m_save_size) / 0x100000, static_cast<double>(m_save_journal_size) / 0x100000);

        gfx::drawTextArgs(vg, 420, 105, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Installed components"_i18n.c_str());
        gfx::drawTextArgs(vg, 420, 145, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info,
            "Base, updates, DLC and data patches reported by NCM"_i18n.c_str());

        if (R_FAILED(m_load_result)) {
            gfx::drawTextArgs(vg, 420, 220, 21.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "Failed to load content metadata (0x%X)", m_load_result);
            return;
        }

        if (m_components.empty()) {
            gfx::drawTextArgs(vg, 420, 220, 21.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", "No installed components found"_i18n.c_str());
            return;
        }

        if (m_component_failures) {
            gfx::drawTextArgs(vg, 420, 185, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info,
                "%u component(s) could not be read (0x%X)", m_component_failures, m_partial_load_result);
        }

        m_list->Draw(vg, theme, m_components.size(), [this](auto* vg, auto* theme, auto v, auto index){
            const auto& component = m_components[index];
            const bool selected = index == m_index;
            const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            const auto secondary = theme->GetColour(ThemeEntryID_TEXT_INFO);

            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            }

            gfx::drawTextArgs(vg, v.x + 18, v.y + 13, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                "%s", ncm::GetMetaTypeStr(component.status.meta_type));
            gfx::drawTextArgs(vg, v.x + 245, v.y + 14, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                "v%u · %s", component.status.version, ncm::GetStorageIdStr(component.status.storageID));
            gfx::drawTextArgs(vg, v.x + 18, v.y + 42, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                "%.2f MB · %u contents · %u rights", static_cast<double>(component.size) / 0x100000,
                component.content_count, component.rights_count);
        });
    }

private:
    void ShowComponentActions() {
        if (m_components.empty()) {
            return;
        }

        const auto component = m_components[m_index];
        auto options = std::make_unique<Sidebar>("Component Actions"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryCallback>("Dump NSP"_i18n, [this, component](){
            m_dump_callback(ContentFlagFromMetaType(component.status.meta_type));
        }, true, "Export only this installed component as an NSP."_i18n);

        options->Add<SidebarEntryCallback>("Content information"_i18n, [component](){
            char message[512];
            std::snprintf(message, sizeof(message),
                "Title ID: %016lX\nType: %s\nVersion: %u\nStorage: %s\nSize: %.2f MB\nContent files: %u\nRights IDs: %u",
                component.status.application_id, ncm::GetMetaTypeStr(component.status.meta_type), component.status.version,
                ncm::GetStorageIdStr(component.status.storageID), static_cast<double>(component.size) / 0x100000,
                component.content_count, component.rights_count);
            App::Push<OptionBox>(message, "Back"_i18n, "OK"_i18n, 0, [](auto){});
        }, "Show the raw installed content metadata."_i18n);
    }

    void ShowGameActions() {
        auto options = std::make_unique<Sidebar>("Game Actions"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryCallback>("Launch"_i18n, [this](){ LaunchEntry(m_entry); }, "Launch this game."_i18n);
        options->Add<SidebarEntryCallback>("Dump all components"_i18n, [this](){ m_dump_callback(title::ContentFlag_All); }, true,
            "Export base, updates, DLC and data patches."_i18n);
        options->Add<SidebarEntryCallback>(m_layeredfs ? "Show contents path"_i18n : "Create contents folder"_i18n, [this](){
            if (m_layeredfs) {
                App::Notify(title::GetContentsPath(m_entry.app_id).toString());
                return;
            }
            const auto rc = fs::FsNativeSd().CreateDirectory(title::GetContentsPath(m_entry.app_id));
            App::PushErrorBox(rc, "Folder create failed!"_i18n);
            if (R_SUCCEEDED(rc)) {
                m_layeredfs = true;
                App::Notify("Folder created!"_i18n);
            }
        }, "Inspect or create the Atmosphere contents folder for this title."_i18n);
    }

private:
    Entry m_entry{};
    std::vector<GameComponentRow> m_components{};
    std::unique_ptr<List> m_list{};
    std::function<void(u32)> m_dump_callback{};
    s64 m_index{};
    Result m_load_result{};
    Result m_partial_load_result{};
    u32 m_component_failures{};
    char m_display_version[sizeof(NacpStruct::display_version) + 1]{};
    u32 m_supported_languages{};
    u64 m_save_size{};
    u64 m_save_journal_size{};
    bool m_layeredfs{};
};

struct DbiDetailsMenu final : MenuBase {
    enum class Tab : u8 { Content, Tickets, Saves };

    DbiDetailsMenu(std::vector<Entry>* entries, s64 index,
        std::function<void(Entry, u32)> dump_callback,
        std::function<void(s64)> selection_callback)
    : MenuBase{"Game Details"_i18n, MenuFlag_None}
    , m_entries{entries}
    , m_game_index{index}
    , m_dump_callback{std::move(dump_callback)}
    , m_selection_callback{std::move(selection_callback)} {
        this->SetActions(
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::L, Action{"Previous game"_i18n, [this](){ ChangeGame(-1); }}),
            std::make_pair(Button::R, Action{"Next game"_i18n, [this](){ ChangeGame(1); }}),
            std::make_pair(Button::L2, Action{"Previous tab"_i18n, [this](){ ChangeTab(-1); }}),
            std::make_pair(Button::R2, Action{"Next tab"_i18n, [this](){ ChangeTab(1); }}),
            std::make_pair(Button::L3, Action{"Launch"_i18n, [this](){ LaunchEntry(CurrentEntry()); }}),
            std::make_pair(Button::A, Action{"Actions"_i18n, [this](){ ShowCurrentActions(); }}),
            std::make_pair(Button::START, Action{"Game actions"_i18n, [this](){ ShowGameActions(); }})
        );

        const Vec4 row{45, 382, 1190, 54};
        const Vec2 pad{0, 5};
        m_list = std::make_unique<List>(1, 4, Vec4{40, 97, 1200, 539}, row, pad);
        LoadGame();
    }

    auto GetShortTitle() const -> const char* override { return "Game Details"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        const auto count = CurrentCount();
        m_list->OnUpdate(controller, touch, m_row_index, count, [this](bool touch, auto index){
            if (touch && m_row_index == index) {
                FireAction(Button::A);
            } else {
                m_row_index = index;
            }
        }, this);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);

        const auto& entry = CurrentEntry();
        const auto text = theme->GetColour(ThemeEntryID_TEXT);
        const auto info = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const auto grid = theme->GetColour(ThemeEntryID_GRID);

        gfx::drawRect(vg, 30, 90, 1220, 550, grid, 5.f);
        const Vec4 cover{50, 108, 190, 190};
        gfx::drawImage(vg, cover, entry.image ? entry.image : App::GetDefaultImage(), 7.f);
        DrawGameBadges(vg, theme, cover, entry);

        gfx::drawTextArgs(vg, 265, 105, 28.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "%s", entry.GetName());
        gfx::drawTextArgs(vg, 265, 140, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info, "%s", entry.GetAuthor());
        gfx::drawTextArgs(vg, 265, 178, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Title ID   %016lX", entry.app_id);
        gfx::drawTextArgs(vg, 265, 207, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Version    %s", m_display_version[0] ? m_display_version : "-");
        gfx::drawText(vg, 265, 245, 18.f, text, "Languages", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        m_language_scroll.Draw(vg, true, 370, 245, 415, 16.f, NVG_ALIGN_LEFT, info,
            m_languages.empty() ? "-" : m_languages.c_str());
        gfx::drawTextArgs(vg, 265, 265, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text,
            "Atmosphere mods folder  %s", entry.layeredfs ? "Found" : "Not found");
        gfx::drawText(vg, 265, 300, 14.f, info,
            "LayeredFS loads replacement game files from /atmosphere/contents/<Title ID>",
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        gfx::drawTextArgs(vg, 810, 112, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "NAND       %s", FormatBytes(entry.nand_size).c_str());
        gfx::drawTextArgs(vg, 810, 141, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "SD         %s", FormatBytes(entry.sd_size).c_str());
        gfx::drawTextArgs(vg, 810, 178, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Components %zu", m_components.size());
        gfx::drawTextArgs(vg, 810, 207, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Tickets    %zu", m_tickets.size());
        gfx::drawTextArgs(vg, 810, 236, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Saves      %zu (%s allocated)", m_saves.size(), FormatBytes(m_save_allocated_size).c_str());
        gfx::drawTextArgs(vg, 810, 265, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text, "Save quota %s + %s", FormatBytes(m_save_size).c_str(), FormatBytes(m_save_journal_size).c_str());

        const std::array<std::string, 3> tab_names{"Content"_i18n, "Tickets"_i18n, "Saves"_i18n};
        const std::array<size_t, 3> tab_counts{m_components.size(), m_tickets.size(), m_saves.size()};
        const float tab_y = 312.f;
        const float tab_gap = 6.f;
        const float tab_w = (1204.f - tab_gap * 2.f) / tab_names.size();
        gfx::drawRect(vg, 30.f, tab_y + 4.f, 1220.f, 50.f, nvgRGBA(8, 8, 8, 210), 5.f);
        for (size_t i = 0; i < tab_names.size(); i++) {
            const bool selected = i == static_cast<size_t>(m_tab);
            const float x = 38.f + i * (tab_w + tab_gap);
            const float y = selected ? tab_y - 4.f : tab_y + 4.f;
            const float h = selected ? 58.f : 50.f;
            const auto fill = selected ? nvgRGBA(245, 245, 245, 255) : nvgRGBA(30, 30, 30, 255);
            const auto label_colour = selected ? nvgRGBA(20, 20, 20, 255) : nvgRGBA(255, 255, 255, 255);
            gfx::drawRect(vg, x - 1.f, y - 1.f, tab_w + 2.f, h + 2.f, nvgRGBA(0, 0, 0, 255), 6.f);
            gfx::drawRect(vg, x, y, tab_w, h, fill, 5.f);
            if (selected) {
                gfx::drawRect(vg, x + 10.f, y + h - 5.f, tab_w - 20.f, 4.f,
                    theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 2.f);
            }
            char label[96];
            std::snprintf(label, sizeof(label), "%s  [%zu]", tab_names[i].c_str(), tab_counts[i]);
            gfx::drawText(vg, x + tab_w * 0.5f, y + h * 0.5f, 20.f,
                label_colour, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }

        if (R_FAILED(m_load_result) && !m_components.empty()) {
            gfx::drawTextArgs(vg, 810, 294, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                theme->GetColour(ThemeEntryID_ERROR), "Partial metadata read (0x%X)", m_load_result);
        }

        if (R_FAILED(m_load_result) && m_components.empty() && m_tab != Tab::Saves) {
            gfx::drawTextArgs(vg, 55, 405, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info,
                "Unable to read installed metadata (0x%X)", m_load_result);
            return;
        }

        if (!CurrentCount()) {
            const char* empty = m_tab == Tab::Content ? "No installed components" :
                (m_tab == Tab::Tickets ? "No rights IDs or tickets" : "No save data");
            gfx::drawText(vg, 640, 475, 22.f, info, empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            return;
        }

        m_list->Draw(vg, theme, CurrentCount(), [this](auto* vg, auto* theme, auto v, auto index){
            const bool selected = index == m_row_index;
            const auto primary = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            const auto secondary = theme->GetColour(ThemeEntryID_TEXT_INFO);
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 4.f);
            } else {
                gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_BACKGROUND), 4.f);
            }

            if (m_tab == Tab::Content) {
                const auto& row = m_components[index];
                gfx::drawTextArgs(vg, v.x + 15, v.y + 9, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", ncm::GetMetaTypeStr(row.status.meta_type));
                gfx::drawTextArgs(vg, v.x + 300, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "v%u | %s | %s", row.status.version, ncm::GetStorageIdStr(row.status.storageID), FormatBytes(row.size).c_str());
                gfx::drawTextArgs(vg, v.x + 760, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%u contents | %u rights", row.content_count, row.rights_count);
            } else if (m_tab == Tab::Tickets) {
                const auto& row = m_tickets[index];
                gfx::drawTextArgs(vg, v.x + 15, v.y + 9, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", utils::hexIdToStr(row.id).str);
                gfx::drawTextArgs(vg, v.x + 530, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s | key generation %u", ncm::GetMetaTypeStr(row.meta_type), row.key_generation);
                gfx::drawTextArgs(vg, v.x + 930, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s", row.ticket_size ? "Common" : (row.personalized ? "Personalized" : "Missing"));
            } else {
                const auto& row = m_saves[index];
                gfx::drawTextArgs(vg, v.x + 15, v.y + 9, 19.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, primary,
                    "%s", row.account.c_str());
                gfx::drawTextArgs(vg, v.x + 350, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "%s | %s", save::GetSaveTypeLabel(row.info.save_data_type), FormatBytes(row.info.size).c_str());
                gfx::drawTextArgs(vg, v.x + 760, v.y + 10, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, secondary,
                    "Save ID %016lX", row.info.save_data_id);
            }
        });
    }

private:
    auto CurrentEntry() -> Entry& { return (*m_entries)[m_game_index]; }
    auto CurrentEntry() const -> const Entry& { return (*m_entries)[m_game_index]; }

    static auto FormatBytes(u64 bytes) -> std::string {
        char out[32];
        if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
            std::snprintf(out, sizeof(out), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024ULL * 1024ULL) {
            std::snprintf(out, sizeof(out), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ULL) {
            std::snprintf(out, sizeof(out), "%.2f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(out, sizeof(out), "%lu B", bytes);
        }
        return out;
    }

    auto CurrentCount() const -> size_t {
        switch (m_tab) {
            case Tab::Content: return m_components.size();
            case Tab::Tickets: return m_tickets.size();
            case Tab::Saves: return m_saves.size();
        }
        return 0;
    }

    void ChangeGame(s64 delta) {
        if (!m_entries || m_entries->empty()) return;
        const auto count = static_cast<s64>(m_entries->size());
        m_game_index = (m_game_index + delta + count) % count;
        LoadGame();
        m_selection_callback(m_game_index);
    }

    void ChangeTab(s64 delta) {
        constexpr s64 count = 3;
        m_tab = static_cast<Tab>((static_cast<s64>(m_tab) + delta + count) % count);
        m_row_index = 0;
        m_list->SetYoff(0);
    }

    void LoadGame() {
        auto& entry = CurrentEntry();
        LoadControlEntry(entry, true);
        LoadGameSummary(entry);

        m_components.clear();
        m_tickets.clear();
        m_saves.clear();
        m_save_allocated_size = 0;
        m_row_index = 0;
        m_list->SetYoff(0);
        m_display_version[0] = '\0';
        m_languages.clear();
        m_language_scroll.Reset();
        m_save_size = 0;
        m_save_journal_size = 0;

        auto control = std::make_unique<NsApplicationControlData>();
        u64 actual_size{};
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, entry.app_id, control.get(), sizeof(*control), &actual_size))) {
            std::snprintf(m_display_version, sizeof(m_display_version), "%s", control->nacp.display_version);
            m_save_size = control->nacp.user_account_save_data_size;
            m_save_journal_size = control->nacp.user_account_save_data_journal_size;

            constexpr std::array<const char*, 16> language_names{
                "US English", "UK English", "Japanese", "French", "German", "Latin Spanish", "Spanish", "Italian",
                "Dutch", "Canadian French", "Portuguese", "Russian", "Korean", "Traditional Chinese", "Simplified Chinese", "Brazilian Portuguese"
            };
            for (size_t i = 0; i < language_names.size(); i++) {
                if (control->nacp.supported_language_flag & (1U << i)) {
                    if (!m_languages.empty()) m_languages += ", ";
                    m_languages += language_names[i];
                }
            }
        }

        std::vector<FsRightsId> personalized_ids;
        s32 personalized_count{};
        if (R_SUCCEEDED(es::CountPersonalizedTicket(&personalized_count)) && personalized_count > 0) {
            personalized_ids.resize(personalized_count);
            s32 written{};
            if (R_FAILED(es::ListPersonalizedTicket(&written, personalized_ids.data(), personalized_ids.size()))) {
                personalized_ids.clear();
            } else {
                personalized_ids.resize(written);
            }
        }

        title::MetaEntries entries;
        m_load_result = GetMetaEntries(entry, entries);
        if (R_SUCCEEDED(m_load_result)) {
            for (const auto& status : entries) {
                ContentInfoEntry info;
                if (const auto rc = BuildContentEntry(status, info); R_FAILED(rc)) {
                    m_load_result = rc;
                    continue;
                }

                GameComponentRow component{};
                component.status = status;
                component.content_count = info.content_infos.size();
                component.rights_count = info.ncm_rights_id.size();
                for (const auto& content : info.content_infos) {
                    u64 size{};
                    ncmContentInfoSizeToU64(&content, &size);
                    component.size += size;
                }
                m_components.emplace_back(component);

                for (const auto& rights : info.ncm_rights_id) {
                    const auto duplicate = std::ranges::find_if(m_tickets, [&rights](const auto& ticket){
                        return !std::memcmp(&ticket.id, &rights.rights_id, sizeof(ticket.id));
                    });
                    if (duplicate != m_tickets.end()) continue;

                    GameTicketRow ticket{};
                    ticket.id = rights.rights_id;
                    ticket.key_generation = rights.key_generation;
                    ticket.meta_type = status.meta_type;
                    es::GetCommonTicketSize(&ticket.ticket_size, &ticket.id);
                    ticket.personalized = std::ranges::any_of(personalized_ids, [&ticket](const auto& id){
                        return !std::memcmp(&id, &ticket.id, sizeof(id));
                    });
                    m_tickets.emplace_back(ticket);
                }
            }
        }

        LoadSaves(entry.app_id);
        SetTitleSubHeading(entry.GetName());
        SetSubHeading(std::to_string(m_game_index + 1) + " / " + std::to_string(m_entries->size()));
        SetStorageHighlight(entry.nand_size, entry.sd_size);
    }

    void LoadSaves(u64 app_id) {
        constexpr std::array<FsSaveDataSpaceId, 4> spaces{
            FsSaveDataSpaceId_System, FsSaveDataSpaceId_User,
            FsSaveDataSpaceId_Temporary, FsSaveDataSpaceId_SdUser
        };
        const auto accounts = App::GetAccountList();

        for (const auto space : spaces) {
            FsSaveDataFilter filter{};
            filter.attr.application_id = app_id;
            filter.filter_by_application_id = true;

            FsSaveDataInfoReader reader;
            if (R_FAILED(fsOpenSaveDataInfoReaderWithFilter(&reader, space, &filter))) continue;

            std::array<FsSaveDataInfo, 32> rows{};
            while (true) {
                s64 read{};
                if (R_FAILED(fsSaveDataInfoReaderRead(&reader, rows.data(), rows.size(), &read)) || !read) break;
                for (s64 i = 0; i < read; i++) {
                    const auto& save_info = rows[i];
                    if (save_info.application_id != app_id) continue;
                    if (std::ranges::any_of(m_saves, [&save_info](const auto& row){
                        return row.info.save_data_id == save_info.save_data_id;
                    })) continue;

                    GameSaveRow row{};
                    row.info = save_info;
                    row.account = save::GetSaveTypeLabel(save_info.save_data_type);
                    if (save_info.save_data_type == FsSaveDataType_Account) {
                        const auto account = std::ranges::find_if(accounts, [&save_info](const auto& candidate){
                            return !std::memcmp(&candidate.uid, &save_info.uid, sizeof(save_info.uid));
                        });
                        if (account != accounts.end()) row.account = account->nickname;
                    }
                    m_save_allocated_size += save_info.size;
                    m_saves.emplace_back(std::move(row));
                }
            }
            fsSaveDataInfoReaderClose(&reader);
        }
    }

    void ShowCurrentActions() {
        if (!CurrentCount()) return;
        if (m_tab == Tab::Content) {
            const auto component = m_components[m_row_index];
            auto options = std::make_unique<Sidebar>("Component Actions"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));
            options->Add<SidebarEntryCallback>("Dump NSP"_i18n, [this, component](){
                m_dump_callback(CurrentEntry(), ContentFlagFromMetaType(component.status.meta_type));
            }, true, "Export only this installed component as an NSP."_i18n);
            options->Add<SidebarEntryCallback>("Content information"_i18n, [component](){
                char message[512];
                std::snprintf(message, sizeof(message),
                    "Title ID: %016lX\nType: %s\nVersion: %u\nStorage: %s\nSize: %s\nContent files: %u\nRights IDs: %u",
                    component.status.application_id, ncm::GetMetaTypeStr(component.status.meta_type), component.status.version,
                    ncm::GetStorageIdStr(component.status.storageID), FormatBytes(component.size).c_str(),
                    component.content_count, component.rights_count);
                App::Push<OptionBox>(message, "Back"_i18n, "OK"_i18n, 0, [](auto){});
            }, "Show installed content metadata."_i18n);
        } else if (m_tab == Tab::Tickets) {
            const auto& ticket = m_tickets[m_row_index];
            char message[512];
            std::snprintf(message, sizeof(message), "Rights ID: %s\nComponent: %s\nKey generation: %u\nTicket: %s\nTicket size: %s",
                utils::hexIdToStr(ticket.id).str, ncm::GetMetaTypeStr(ticket.meta_type), ticket.key_generation,
                ticket.ticket_size ? "Common" : (ticket.personalized ? "Personalized" : "Missing"),
                FormatBytes(ticket.ticket_size).c_str());
            App::Push<OptionBox>(message, "Back"_i18n, "OK"_i18n, 0, [](auto){});
        } else {
            const auto& row = m_saves[m_row_index];
            char message[512];
            std::snprintf(message, sizeof(message), "Account: %s\nType: %s\nSave ID: %016lX\nSize: %s\nStorage space: %u",
                row.account.c_str(), save::GetSaveTypeLabel(row.info.save_data_type), row.info.save_data_id,
                FormatBytes(row.info.size).c_str(), row.info.save_data_space_id);
            App::Push<OptionBox>(message, "Back"_i18n, "OK"_i18n, 0, [](auto){});
        }
    }

    void ShowGameActions() {
        auto options = std::make_unique<Sidebar>("Game Actions"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));
        options->Add<SidebarEntryCallback>("Launch"_i18n, [this](){ LaunchEntry(CurrentEntry()); }, "Launch this game."_i18n);
        options->Add<SidebarEntryCallback>("Dump all components"_i18n, [this](){
            m_dump_callback(CurrentEntry(), title::ContentFlag_All);
        }, true, "Export base, updates, DLC and data patches."_i18n);
        options->Add<SidebarEntryCallback>(CurrentEntry().layeredfs ? "Show mods folder path"_i18n : "Create mods folder"_i18n, [this](){
            auto& entry = CurrentEntry();
            if (entry.layeredfs) {
                App::Notify(title::GetContentsPath(entry.app_id).toString());
                return;
            }
            const auto rc = fs::FsNativeSd().CreateDirectory(title::GetContentsPath(entry.app_id));
            App::PushErrorBox(rc, "Folder create failed!"_i18n);
            if (R_SUCCEEDED(rc)) {
                entry.layeredfs = true;
                App::Notify("Folder created!"_i18n);
            }
        }, "LayeredFS uses this Atmosphere folder to replace game files with mods. Creating an empty folder does not install a mod."_i18n);
    }

private:
    std::vector<Entry>* m_entries{};
    s64 m_game_index{};
    s64 m_row_index{};
    Tab m_tab{};
    std::unique_ptr<List> m_list{};
    std::vector<GameComponentRow> m_components{};
    std::vector<GameTicketRow> m_tickets{};
    std::vector<GameSaveRow> m_saves{};
    std::function<void(Entry, u32)> m_dump_callback{};
    std::function<void(s64)> m_selection_callback{};
    Result m_load_result{};
    char m_display_version[sizeof(NacpStruct::display_version) + 1]{};
    std::string m_languages{};
    ScrollingText m_language_scroll{};
    u64 m_save_size{};
    u64 m_save_journal_size{};
    u64 m_save_allocated_size{};
};

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"Games"_i18n, flags} {
    this->SetActions(
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){ ToggleCurrentSelection(); }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){ InvertSelection(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
                SetPop();
            }
        }}),
        std::make_pair(Button::A, Action{"Details"_i18n, [this](){
            if (m_entries.empty()) {
                return;
            }
            auto& entry = m_entries[m_index];
            LoadControlEntry(entry, true);
            App::Push<DbiDetailsMenu>(&m_entries, m_index, [this](Entry entry, u32 flags) mutable {
                std::vector<Entry> targets;
                targets.emplace_back(entry);
                DumpEntries(std::move(targets), flags, false);
            }, [this](s64 index) {
                SetIndex(index);
            });
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Game Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            if (m_entries.size()) {
                auto targets = GetSelectedEntries();
                u32 common_flags = title::ContentFlag_All;
                bool all_have_content = !targets.empty();
                for (const auto& target : targets) {
                    const auto entry = std::ranges::find_if(m_entries, [&target](const auto& candidate){
                        return candidate.app_id == target.app_id;
                    });
                    if (entry == m_entries.end()) {
                        all_have_content = false;
                        common_flags = 0;
                        continue;
                    }
                    LoadGameSummary(*entry);
                    if (R_FAILED(entry->summary_result) || !entry->content_flags) {
                        all_have_content = false;
                    }
                    common_flags &= entry->content_flags;
                }

                options->Add<SidebarEntryHeader>("LIBRARY"_i18n);
                options->Add<SidebarEntryCallback>("Sort By"_i18n, [this](){
                    auto options = std::make_unique<Sidebar>("Sort Options"_i18n, Sidebar::Side::RIGHT);
                    ON_SCOPE_EXIT(App::Push(std::move(options)));

                    SidebarEntryArray::Items sort_items;
                    sort_items.push_back("Updated"_i18n);
                    sort_items.push_back("Alphabetical"_i18n);
                    sort_items.push_back("Publisher"_i18n);

                    SidebarEntryArray::Items order_items;
                    order_items.push_back("Descending"_i18n);
                    order_items.push_back("Ascending"_i18n);

                    SidebarEntryArray::Items layout_items;
                    layout_items.push_back("Icon"_i18n);
                    layout_items.push_back("Grid"_i18n);
                    layout_items.push_back("HB Menu"_i18n);

                    options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
                        m_sort.Set(index_out);
                        SortAndFindLastFile(false);
                    }, m_sort.Get(), "Select which field to sort games by."_i18n);

                    options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this](s64& index_out){
                        m_order.Set(index_out);
                        SortAndFindLastFile(false);
                    }, m_order.Get(), "Sort games from newest to oldest or A to Z."_i18n);

                    auto current_layout = m_layout.Get();
                    if (current_layout == grid::LayoutType_List) {
                        current_layout = grid::LayoutType_Grid;
                        m_layout.Set(current_layout);
                    }
                    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
                        m_layout.Set(index_out + 1);
                        OnLayoutChange();
                    }, current_layout - 1, "Choose how games are displayed on screen."_i18n);

                    options->Add<SidebarEntryBool>("Hide forwarders"_i18n, m_hide_forwarders.Get(), [this](bool& v_out){
                        m_hide_forwarders.Set(v_out);
                        m_dirty = true;
                    }, "Hide game forwarder shortcuts from the list."_i18n);
                }, "Change display order and layout for games."_i18n);

                #if 0
                options->Add<SidebarEntryCallback>("Info"_i18n, [this](){

                });
                #endif

                options->Add<SidebarEntryCallback>("Launch random game"_i18n, [this](){
                    const auto random_index = randomGet64() % std::size(m_entries);
                    auto& e = m_entries[random_index];
                    LoadControlEntry(e, true);

                    App::Push<OptionBox>(
                        "Launch "_i18n + e.GetName(),
                        "Back"_i18n, "Launch"_i18n, 1, [this, &e](auto op_index){
                            if (op_index && *op_index) {
                                LaunchEntry(e);
                            }
                        }, e.image
                    );
                }, "Pick and launch a random game from your library."_i18n);

                options->Add<SidebarEntryCallback>("Dump options"_i18n, [this](){
                    App::DisplayDumpOptions(false);
                }, "Configure dump output settings such as folder structure and ticket handling."_i18n);

                options->Add<SidebarEntryHeader>("SELECTED GAMES"_i18n,
                    std::to_string(targets.size()) + " " + "selected"_i18n);

                if (targets.size() == 1) {
                    options->Add<SidebarEntryCallback>("List meta records"_i18n, [this](){
                    title::MetaEntries meta_entries;
                    const auto target = GetSelectedEntries().front();
                    const auto rc = GetMetaEntries(target, meta_entries);
                    if (R_FAILED(rc)) {
                        App::Push<ui::ErrorBox>(rc,
                            i18n::get("Failed to list application meta entries")
                        );
                        return;
                    }

                    if (meta_entries.empty()) {
                        App::Notify("No meta entries found...\n"_i18n);
                        return;
                    }

                    PopupList::Items items;
                    for (auto& e : meta_entries) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "Type: %s Storage: %s [%016lX][v%u]", ncm::GetMetaTypeStr(e.meta_type), ncm::GetStorageIdStr(e.storageID), e.application_id, e.version);
                        items.emplace_back(buf);
                    }

                    App::Push<PopupList>(
                        "Entries"_i18n, items, [this, meta_entries](auto op_index){
                            #if 0
                            if (op_index) {
                                const auto& e = meta_entries[*op_index];
                            }
                            #endif
                        }
                    );
                    }, "Show all installed content meta records for the selected game."_i18n);
                }

                if (all_have_content) {
                    options->Add<SidebarEntryCallback>("Dump"_i18n, [this, common_flags](){
                    auto options = std::make_unique<Sidebar>("Select content to dump"_i18n, Sidebar::Side::RIGHT);
                    ON_SCOPE_EXIT(App::Push(std::move(options)));

                    options->Add<SidebarEntryCallback>("Dump All"_i18n, [this](){
                        DumpGames(title::ContentFlag_All);
                    }, true, "Dump all content: base game, updates, and DLC."_i18n);
                    if (common_flags & title::ContentFlag_Application) {
                        options->Add<SidebarEntryCallback>("Dump Application"_i18n, [this](){
                            DumpGames(title::ContentFlag_Application);
                        }, true, "Dump the base application NSP only."_i18n);
                    }
                    if (common_flags & title::ContentFlag_Patch) {
                        options->Add<SidebarEntryCallback>("Dump Patch"_i18n, [this](){
                            DumpGames(title::ContentFlag_Patch);
                        }, true, "Dump the game update/patch NSP only."_i18n);
                    }
                    if (common_flags & title::ContentFlag_AddOnContent) {
                        options->Add<SidebarEntryCallback>("Dump AddOnContent"_i18n, [this](){
                            DumpGames(title::ContentFlag_AddOnContent);
                        }, true, "Dump downloadable content (DLC) NSP only."_i18n);
                    }
                    if (common_flags & title::ContentFlag_DataPatch) {
                        options->Add<SidebarEntryCallback>("Dump DataPatch"_i18n, [this](){
                            DumpGames(title::ContentFlag_DataPatch);
                        }, true, "Dump data patch NSP only."_i18n);
                    }
                    }, true, "Export content shared by all selected games as NSP files."_i18n);
                }

                options->Add<SidebarEntryCallback>("Create mods folders"_i18n, [this](){
                    CreateContentsFolders();
                }, "Create Atmosphere LayeredFS folders for all selected games."_i18n);

                options->Add<SidebarEntryCallback>("Create save"_i18n, [this](){
                    ui::PopupList::Items items{};
                    const auto accounts = App::GetAccountList();
                    for (auto& p : accounts) {
                        items.emplace_back(p.nickname);
                    }

                    App::Push<ui::PopupList>(
                        "Select user to create save for"_i18n, items, [this, accounts](auto op_index){
                            if (op_index) {
                                CreateSaves(accounts[*op_index].uid);
                            }
                        }
                    );
                }, "Manually create save data entries for all selected games."_i18n);

                // completely deletes the application record and all data.
                options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
                    const auto targets = GetSelectedEntries();
                    const auto buf = targets.size() == 1
                        ? "Are you sure you want to delete "_i18n + targets.front().GetName() + "?"
                        : "Are you sure you want to delete the selected games?"_i18n;
                    App::Push<OptionBox>(
                        buf,
                        "Back"_i18n, "Delete"_i18n, 0, [this](auto op_index){
                            if (op_index && *op_index) {
                                DeleteGames();
                            }
                        }, targets.front().image
                    );
                }, true, "Permanently delete all selected games and their data."_i18n);
            }

            options->Add<SidebarEntryCallback>("Advanced options"_i18n, [this](){
                auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
                ON_SCOPE_EXIT(App::Push(std::move(options)));

                options->Add<SidebarEntryCallback>("Refresh"_i18n, [this](){
                    m_dirty = true;
                    App::PopToMenu();
                }, "Rescan the game library and reload the list."_i18n);

                options->Add<SidebarEntryBool>("Title cache"_i18n, m_title_cache.Get(), [this](bool& v_out){
                    m_title_cache.Set(v_out);
                }, "Cache game names and icons to speed up loading."_i18n);

                options->Add<SidebarEntryCallback>("Delete title cache"_i18n, [this](){
                    App::Push<OptionBox>(
                        "Are you sure you want to delete the title cache?"_i18n,
                        "Back"_i18n, "Delete"_i18n, 0, [this](auto op_index){
                            if (op_index && *op_index) {
                                m_dirty = true;
                                title::Clear();
                                App::PopToMenu();
                            }
                        }
                    );
                }, "Clear cached game metadata to force a fresh reload."_i18n);
            }, "Access developer and maintenance tools."_i18n);
        }})
    );

    OnLayoutChange();

    nsInitialize();
    es::Initialize();
    title::Init();
}

Menu::~Menu() {
    title::Exit();

    FreeEntries();
    nsExit();
    es::Exit();
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_dirty) {
        App::Notify("Updating application record list"_i18n);
        SortAndFindLastFile(true);
    }

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

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    if (m_layout.Get() == grid::LayoutType_HbMenu) {
        auto& e = m_entries[m_index];
        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", e.app_id);
        DrawHbMenuHeader(vg, theme, e.image, e.GetName(), e.GetAuthor(), title_id, e.GetAuthor());
        DrawGameBadges(vg, theme, Vec4{80.f, 120.f, 200.f, 200.f}, e);
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;
    int summary_load_count = 0;

    m_list->Draw(vg, theme, m_entries.size(), [this, &image_load_count, &summary_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto& [x, y, w, h] = v;
        auto& e = m_entries[pos];

        if (e.status == title::NacpLoadStatus::None) {
            title::PushAsync(e.app_id);
            e.status = title::NacpLoadStatus::Progress;
        } else if (e.status == title::NacpLoadStatus::Progress) {
            LoadResultIntoEntry(e, title::GetAsync(e.app_id));
        }

        // lazy load image
        if (image_load_count < image_load_max) {
            if (LoadControlImage(e, title::GetAsync(e.app_id))) {
                image_load_count++;
            }
        }

        if (!e.summary_attempted && summary_load_count < 1) {
            LoadGameSummary(e);
            summary_load_count++;
            if (pos == m_index) {
                SetStorageHighlight(e.nand_size, e.sd_size);
            }
        }

        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", e.app_id);

        const auto selected = pos == m_index;
        const auto image_v = DrawEntry(vg, theme, m_layout.Get(), v, selected, e.image, e.GetName(), e.GetAuthor(), title_id);
        auto badge_v = image_v;
        if (m_layout.Get() == grid::LayoutType_HbMenu) {
            badge_v.y += 28.f;
            badge_v.h -= 28.f;
        }
        DrawGameBadges(vg, theme, badge_v, e);

        if (e.selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_FOCUS), 5);
            gfx::drawText(vg, x + w / 2, y + h / 2, 24.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        }
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        ScanHomebrew();
    }
}

void Menu::SetIndex(s64 index) {
    if (m_entries.empty()) {
        m_index = 0;
        SetTitleSubHeading("");
        SetSubHeading("0 / 0");
        SetStorageHighlight(0, 0);
        return;
    }

    index = std::clamp<s64>(index, 0, static_cast<s64>(m_entries.size()) - 1);
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    char title_id[33];
    std::snprintf(title_id, sizeof(title_id), "%016lX", m_entries[m_index].app_id);
    SetTitleSubHeading(title_id);
    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries.size()));

    auto& entry = m_entries[m_index];
    LoadGameSummary(entry);
    SetStorageHighlight(entry.nand_size, entry.sd_size);
}

void Menu::ScanHomebrew() {
    constexpr auto ENTRY_CHUNK_COUNT = 1000;
    const auto hide_forwarders = m_hide_forwarders.Get();
    TimeStamp ts;

    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    FreeEntries();
    m_entries.reserve(ENTRY_CHUNK_COUNT);

    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    s32 offset{};
    while (true) {
        s32 record_count{};
        if (R_FAILED(nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count))) {
            log_write("failed to list application records at offset: %d\n", offset);
        }

        // finished parsing all entries.
        if (!record_count) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            const auto& e = record_list[i];

            if (hide_forwarders && (e.application_id & 0x0500000000000000) == 0x0500000000000000) {
                continue;
            }

            m_entries.emplace_back(e.application_id, e.last_event, e.last_updated);
        }

        offset += record_count;
    }

    m_dirty = false;
    log_write("games found: %zu time_taken: %.2f seconds %zu ms %zu ns\n", m_entries.size(), ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
    this->Sort();
    SetIndex(0);
    ClearSelection();
}

void Menu::Sort() {
    const auto sort = m_sort.Get();
    const auto order = m_order.Get();

    if (sort == SortType_Alphabetical || sort == SortType_Publisher) {
        for (auto& e : m_entries) {
            LoadControlEntry(e);
        }
    }

    const auto name_cmp = [order](const Entry& lhs, const Entry& rhs) -> bool {
        auto r = strcasecmp(lhs.GetName(), rhs.GetName());
        if (!r) {
            r = strcasecmp(lhs.GetAuthor(), rhs.GetAuthor());
        }

        if (order == OrderType_Descending) {
            return r < 0;
        } else {
            return r > 0;
        }
    };

    const auto publisher_cmp = [order](const Entry& lhs, const Entry& rhs) -> bool {
        auto r = strcasecmp(lhs.GetAuthor(), rhs.GetAuthor());
        if (!r) {
            r = strcasecmp(lhs.GetName(), rhs.GetName());
        }

        if (order == OrderType_Descending) {
            return r < 0;
        } else {
            return r > 0;
        }
    };

    const auto sorter = [sort, order, &name_cmp, &publisher_cmp](const Entry& lhs, const Entry& rhs) -> bool {
        switch (sort) {
            case SortType_Updated: {
                if (lhs.last_updated == rhs.last_updated) {
                    if (lhs.last_event == rhs.last_event) {
                        return lhs.app_id < rhs.app_id;
                    } else if (order == OrderType_Descending) {
                        return lhs.last_event > rhs.last_event;
                    } else {
                        return lhs.last_event < rhs.last_event;
                    }
                } else if (order == OrderType_Descending) {
                    return lhs.last_updated > rhs.last_updated;
                } else {
                    return lhs.last_updated < rhs.last_updated;
                }
            } break;

            case SortType_Alphabetical: {
                return name_cmp(lhs, rhs);
            } break;

            case SortType_Publisher: {
                return publisher_cmp(lhs, rhs);
            } break;
        }

        std::unreachable();
    };

    std::sort(m_entries.begin(), m_entries.end(), sorter);
}

void Menu::SortAndFindLastFile(bool scan) {
    if (m_entries.empty()) {
        if (scan) {
            ScanHomebrew();
        } else {
            Sort();
            SetIndex(0);
        }
        return;
    }

    const auto app_id = m_entries[m_index].app_id;
    if (scan) {
        ScanHomebrew();
    } else {
        Sort();
    }
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries.size(); i++) {
        if (app_id == m_entries[i].app_id) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        const auto row = m_list->GetRow();
        const auto page = m_list->GetPage();
        // guesstimate where the position is
        if (index >= page) {
            m_list->SetYoff((((index - page) + row) / row) * m_list->GetMaxY());
        } else {
            m_list->SetYoff(0);
        }
        SetIndex(index);
    }
}

void Menu::FreeEntries() {
    auto vg = App::GetVg();

    for (auto&p : m_entries) {
        FreeEntry(vg, p);
    }

    m_entries.clear();
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
    SetIndex(0);
}

void Menu::ToggleCurrentSelection() {
    if (m_entries.empty()) {
        return;
    }

    auto& entry = m_entries[m_index];
    entry.selected ^= 1;
    m_selected_count += entry.selected ? 1 : -1;
}

void Menu::InvertSelection() {
    m_selected_count = 0;
    for (auto& entry : m_entries) {
        entry.selected ^= 1;
        if (entry.selected) {
            m_selected_count++;
        }
    }
}

void Menu::CreateContentsFolders() {
    const auto targets = GetSelectedEntries();
    size_t created{};
    for (const auto& target : targets) {
        const auto path = title::GetContentsPath(target.app_id);
        auto rc = fs::FsNativeSd().CreateDirectory(path);
        if (rc == FsError_PathAlreadyExists) {
            rc = 0;
        }
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Mods folder create failed!"_i18n);
            return;
        }

        const auto entry = std::ranges::find_if(m_entries, [&target](const auto& candidate){
            return candidate.app_id == target.app_id;
        });
        if (entry != m_entries.end()) {
            entry->layeredfs = true;
        }
        created++;
    }

    ClearSelection();
    App::Notify(std::to_string(created) + " " + "mods folder(s) ready"_i18n);
}

void Menu::DeleteGames() {
    App::Push<ProgressBox>(0, "Deleting"_i18n, "", [this](auto pbox) -> Result {
        auto targets = GetSelectedEntries();

        for (s64 i = 0; i < std::size(targets); i++) {
            auto& e = targets[i];

            LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, std::size(targets));
            R_TRY(nsDeleteApplicationCompletely(e.app_id));
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Delete failed!"_i18n);

        ClearSelection();
        m_dirty = true;

        if (R_SUCCEEDED(rc)) {
            App::Notify("Delete successfull!"_i18n);
        }
    });
}

void Menu::DumpGames(u32 flags) {
    DumpEntries(GetSelectedEntries(), flags, true);
}

void Menu::DumpEntries(std::vector<Entry> targets, u32 flags, bool clear_selection) {
    std::vector<NspEntry> nsp_entries;
    for (auto& e : targets) {
        if (const auto rc = BuildNspEntries(e, flags, nsp_entries); R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to prepare NSP dump"_i18n);
            return;
        }
    }

    if (nsp_entries.empty()) {
        App::Notify("No matching installed content to dump"_i18n);
        return;
    }

    std::vector<fs::FsPath> paths;
    for (auto& e : nsp_entries) {
        paths.emplace_back(fs::AppendPath("/dumps/NSP", e.path));
    }

    auto source = std::make_shared<NspSource>(nsp_entries);
    dump::Dump(source, paths, [this, clear_selection](Result rc){
        if (clear_selection) {
            ClearSelection();
        }
    });
}

void Menu::CreateSaves(AccountUid uid) {
    App::Push<ProgressBox>(0, "Creating"_i18n, "", [this, uid](auto pbox) -> Result {
        auto targets = GetSelectedEntries();

        for (s64 i = 0; i < std::size(targets); i++) {
            auto& e = targets[i];

            LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, std::size(targets));
            const auto rc = CreateSave(e.app_id, uid);

            // don't error if the save already exists.
            if (R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
                R_THROW(rc);
            }
        }

        R_SUCCEED();
    }, [this](Result rc){
        App::PushErrorBox(rc, "Save create failed!"_i18n);

        ClearSelection();
        save::SignalChange();

        if (R_SUCCEEDED(rc)) {
            App::Notify("Save create successfull!"_i18n);
        }
    });
}

} // namespace sphaira::ui::menu::game
