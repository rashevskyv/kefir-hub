#include "title_info.hpp"
#include "defines.hpp"
#include "ui/types.hpp"
#include "i18n.hpp"
#include "app.hpp"
#include "log.hpp"

#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"

#include <cstring>
#include <cstdio>
#include <atomic>
#include <ranges>
#include <algorithm>

#include <nxtc.h>
#include <minIni.h>
#include <zlib.h>

namespace sphaira::title {
namespace {

constexpr int THREAD_PRIO = PRIO_PREEMPTIVE;
constexpr int THREAD_CORE = 1;

struct ThreadData {
    ThreadData(bool title_cache);

    void Run();
    void Close();
    void Clear();

    void PushAsync(u64 id);
    auto GetAsync(u64 app_id) -> ThreadResultData*;
    auto Get(u64 app_id, bool* cached = nullptr) -> ThreadResultData*;

    auto IsRunning() const -> bool {
        return m_running;
    }

    auto IsTitleCacheEnabled() const {
        return m_title_cache;
    }

private:
    fs::FsNativeSd m_fs{};
    UEvent m_uevent{};
    Mutex m_mutex_id{};
    Mutex m_mutex_result{};
    bool m_title_cache{};

    // app_ids pushed to the queue, signal uevent when pushed.
    std::vector<u64> m_ids{};
    // control data pushed to the queue.
    std::vector<std::unique_ptr<ThreadResultData>> m_result{};

    std::atomic_bool m_running{};
};

Mutex g_mutex{};
Thread g_thread{};
u32 g_ref_count{};
std::unique_ptr<ThreadData> g_thread_data{};

struct NcmEntry {
    const NcmStorageId storage_id;
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};

    void Open() {
        if (R_FAILED(ncmOpenContentMetaDatabase(std::addressof(db), storage_id))) {
            log_write("\tncmOpenContentMetaDatabase() failed. storage_id: %u\n", storage_id);
        } else {
            log_write("\tncmOpenContentMetaDatabase() success. storage_id: %u\n", storage_id);
        }

        if (R_FAILED(ncmOpenContentStorage(std::addressof(cs), storage_id))) {
            log_write("\tncmOpenContentStorage() failed. storage_id: %u\n", storage_id);
        } else {
            log_write("\tncmOpenContentStorage() success. storage_id: %u\n", storage_id);
        }
    }

    void Close() {
        ncmContentMetaDatabaseClose(std::addressof(db));
        ncmContentStorageClose(std::addressof(cs));

        db = {};
        cs = {};
    }
};

constinit NcmEntry ncm_entries[] = {
    // on memory, will become invalid on the gamecard being inserted / removed.
    { NcmStorageId_GameCard },
    // normal (save), will remain valid.
    { NcmStorageId_BuiltInUser },
    { NcmStorageId_SdCard },
};

auto& GetNcmEntry(u8 storage_id) {
    auto it = std::ranges::find_if(ncm_entries, [storage_id](auto& e){
        return storage_id == e.storage_id;
    });

    if (it == std::end(ncm_entries)) {
        log_write("unable to find valid ncm entry: %u\n", storage_id);
        return ncm_entries[0];
    }

    return *it;
}

// also sets the status to error.
void FakeNacpEntry(ThreadResultData* e) {
    e->status = NacpLoadStatus::Error;
    // fake the nacp entry
    std::strcpy(e->lang.name, "Corrupted");
    std::strcpy(e->lang.author, "Corrupted");
}

// NACP format v2 (Nintendo FW 20.0+): titles section is zlib-deflated at offset 0x3215.
// Returns true and fills out_name/out_author if the new format is detected and decodes successfully.
bool TryParseNacpV2(const u8* raw_nacp, size_t raw_size, char* out_name, char* out_author) {
    constexpr size_t kOffset     = 0x3215;
    constexpr size_t kDecompSize = 0x6000;     // must be exact multiple of sizeof(NacpLanguageEntry)
    constexpr size_t kEntrySize  = sizeof(NacpLanguageEntry);
    constexpr size_t kEntryCount = kDecompSize / kEntrySize;
    static_assert(kEntryCount * kEntrySize == kDecompSize);

    if (raw_size <= kOffset) {
        return false;
    }

    // Decompress raw deflate stream (wbits = -15 means raw deflate, no zlib header)
    std::vector<u8> decompressed(kDecompSize);

    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(raw_nacp + kOffset);
    zs.avail_in  = static_cast<uInt>(raw_size - kOffset);
    zs.next_out  = decompressed.data();
    zs.avail_out = static_cast<uInt>(kDecompSize);

    if (inflateInit2(&zs, -15) != Z_OK) {
        return false;
    }
    const int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);

    if (ret != Z_STREAM_END && ret != Z_OK) {
        return false;
    }

    // Map SetLanguage -> language entry index (mirrors Goldleaf commit 54ba947)
    SetLanguage sys_lang = SetLanguage_ENUS;
    u64 languageCode = 0;
    if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
        setMakeLanguage(languageCode, &sys_lang);
    }

    auto GetIndex = [](SetLanguage lang) -> int {
        switch (lang) {
            case SetLanguage_ENUS:   return 0;
            case SetLanguage_ENGB:   return 1;
            case SetLanguage_JA:     return 2;
            case SetLanguage_FR:     return 3;
            case SetLanguage_DE:     return 4;
            case SetLanguage_IT:     return 5;
            case SetLanguage_ES:     return 6;
            case SetLanguage_ZHCN:   return 7;
            case SetLanguage_KO:     return 8;
            case SetLanguage_NL:     return 9;
            case SetLanguage_PT:     return 10;
            case SetLanguage_RU:     return 11;
            case SetLanguage_ZHTW:   return 12;
            case SetLanguage_FRCA:   return 13;
            case SetLanguage_ES419:  return 14;
            case SetLanguage_ZHHANS: return 15;
            default:                 return 0;
        }
    };

    const auto* entries = reinterpret_cast<const NacpLanguageEntry*>(decompressed.data());
    int idx = GetIndex(sys_lang);

    // If preferred language entry is empty, fall back to English
    if (idx >= (int)kEntryCount || entries[idx].name[0] == '\0') {
        idx = 0; // SetLanguage_ENUS
    }

    if (idx >= (int)kEntryCount || entries[idx].name[0] == '\0') {
        return false;
    }

    std::strncpy(out_name,   entries[idx].name,   sizeof(NacpLanguageEntry::name)   - 1);
    std::strncpy(out_author, entries[idx].author, sizeof(NacpLanguageEntry::author) - 1);
    out_name[sizeof(NacpLanguageEntry::name) - 1]   = '\0';
    out_author[sizeof(NacpLanguageEntry::author) - 1] = '\0';

    return out_name[0] != '\0';
}


Result LoadControlManual(u64 id, NacpStruct& nacp, ThreadResultData* data) {
    TimeStamp ts;

    MetaEntries entries;
    R_TRY(GetMetaEntries(id, entries, ContentFlag_Nacp));
    R_UNLESS(!entries.empty(), Result_GameEmptyMetaEntries);

    u64 program_id;
    fs::FsPath path;
    R_TRY(GetControlPathFromStatus(entries.back(), &program_id, &path));
    R_TRY(nca::ParseControl(path, program_id, &nacp, sizeof(nacp), &data->icon));

    log_write("\t\t[manual control] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    R_SUCCEED();
}

ThreadData::ThreadData(bool title_cache) : m_title_cache{title_cache} {
    ueventCreate(&m_uevent, true);
    mutexInit(&m_mutex_id);
    mutexInit(&m_mutex_result);
    m_running = true;
}

void ThreadData::Run() {
    TimeStamp ts{};
    bool cached{true};
    const auto waiter = waiterForUEvent(&m_uevent);

    while (IsRunning()) {
        const auto rc = waitSingle(waiter, 3e+9);

        // if we timed out, flush the cache and poll again.
        if (R_FAILED(rc)) {
            nxtcFlushCacheFile();
            continue;
        }

        if (!IsRunning()) {
            return;
        }

        // a transfer has the card saturated. loading control data now would hold
        // g_mutex across those slow reads, and the menu drawing underneath the
        // progress box waits on the same mutex in GetAsync() - which is what
        // makes the ui judder for the length of a move. leave the ids queued and
        // come back once the transfer is done.
        if (App::GetProgressActive()) {
            ueventSignal(&m_uevent);
            svcSleepThread(1e+8);
            continue;
        }

        std::vector<u64> ids;
        {
            SCOPED_MUTEX(&m_mutex_id);
            std::swap(ids, m_ids);
        }

        for (u64 i = 0; i < std::size(ids); i++) {
            if (!IsRunning()) {
                return;
            }

            // sleep after every other entry loaded.
            const auto elapsed = (s64)2e+6 - (s64)ts.GetNs();
            if (!cached && elapsed > 0) {
                svcSleepThread(elapsed);
            }

            // loads new entry into cache.
            std::ignore = Get(ids[i], &cached);
            ts.Update();
        }
    }
}

void ThreadData::Close() {
    m_running = false;
    ueventSignal(&m_uevent);
}

void ThreadData::Clear() {
    SCOPED_MUTEX(&m_mutex_id);
    SCOPED_MUTEX(&m_mutex_result);
    m_result.clear();
    nxtcWipeCache();
}

void ThreadData::PushAsync(u64 id) {
    SCOPED_MUTEX(&m_mutex_id);
    SCOPED_MUTEX(&m_mutex_result);

    const auto it_id = std::ranges::find(m_ids, id);
    const auto it_result = std::ranges::find_if(m_result, [id](auto& e){
        return id == e->id;
    });

    if (it_id == m_ids.end() && it_result == m_result.end()) {
        m_ids.emplace_back(id);
        ueventSignal(&m_uevent);
    }
}

auto ThreadData::GetAsync(u64 app_id) -> ThreadResultData* {
    SCOPED_MUTEX(&m_mutex_result);

    for (s64 i = 0; i < std::size(m_result); i++) {
        if (app_id == m_result[i]->id) {
            return m_result[i].get();
        }
    }

    return {};
}

auto ThreadData::Get(u64 app_id, bool* cached) -> ThreadResultData* {
    // try and fetch from results first, before manually loading.
    if (auto data = GetAsync(app_id)) {
        if (cached) {
            *cached = true;
        }
        return data;
    }

    TimeStamp ts;
    auto result = std::make_unique<ThreadResultData>(app_id);
    result->status = NacpLoadStatus::Error;

    if (auto data = nxtcGetApplicationMetadataEntryById(app_id)) {
        log_write("[NXTC] loaded from cache time taken: %.2fs %zums %zuns\n", ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
        ON_SCOPE_EXIT(nxtcFreeApplicationMetadata(&data));

        if (cached) {
            *cached = true;
        }

        result->status = NacpLoadStatus::Loaded;
        std::strcpy(result->lang.name, data->name);
        std::strcpy(result->lang.author, data->publisher);
        result->icon.resize(data->icon_size);
        std::memcpy(result->icon.data(), data->icon_data, result->icon.size());
    } else {
        if (cached) {
            *cached = false;
        }

        bool manual_load = true;
        u64 actual_size{};
        auto control = std::make_unique<NsApplicationControlData>();

        if (hosversionBefore(20,0,0)) {
            TimeStamp ts;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, app_id, control.get(), sizeof(NsApplicationControlData), &actual_size))) {
                manual_load = false;
                log_write("\t\t[ns control cache] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            }
        }

        if (manual_load) {
            manual_load = R_SUCCEEDED(LoadControlManual(app_id, control->nacp, result.get()));
        }

        Result rc{};
        if (!manual_load) {
            TimeStamp ts;
            if (R_SUCCEEDED(rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control.get(), sizeof(NsApplicationControlData), &actual_size))) {
                log_write("\t\t[ns control storage] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            }
        }

        if (R_FAILED(rc)) {
            FakeNacpEntry(result.get());
        } else {
            bool valid = true;
            NacpLanguageEntry* lang;
            if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&control->nacp, &lang))) {
                result->lang = *lang;

                // NACP v2 fallback: if name is empty, the game uses the new compressed format (FW 20.0+)
                if (result->lang.name[0] == '\0') {
                    const auto* raw = reinterpret_cast<const u8*>(&control->nacp);
                    if (TryParseNacpV2(raw, sizeof(NacpStruct), result->lang.name, result->lang.author)) {
                        log_write("\t\t[nacp v2] decoded name: %s\n", result->lang.name);
                    } else {
                        log_write("\t\t[nacp v2] fallback failed, trying full raw nacp block\n");
                        // Try on full control block (name section may be in the trailing bytes after nacp)
                        const auto* raw_full = reinterpret_cast<const u8*>(control.get());
                        TryParseNacpV2(raw_full, actual_size, result->lang.name, result->lang.author);
                    }
                }
            } else {
                FakeNacpEntry(result.get());
                valid = false;
            }

            if (!manual_load) {
                const auto jpeg_size = actual_size - sizeof(NacpStruct);
                result->icon.resize(jpeg_size);
                std::memcpy(result->icon.data(), control->icon, result->icon.size());
            }

            // add new entry to cache, if valid.
            if (valid) {
                nxtcAddEntry(app_id, &control->nacp, result->icon.size(), result->icon.data(), true);
            }

            result->status = NacpLoadStatus::Loaded;
        }
    }

    // load override from sys-tweak.
    if (result->status == NacpLoadStatus::Loaded) {
        const auto tweak_path = GetContentsPath(app_id);
        if (m_fs.DirExists(tweak_path)) {
            log_write("[TITLE] found contents path: %s\n", tweak_path.s);

            std::vector<u8> icon;
            m_fs.read_entire_file(fs::AppendPath(tweak_path, "icon.jpg"), icon);

            struct Overrides {
                std::string name;
                std::string author;
            } overrides;

            static const auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
                auto e = static_cast<Overrides*>(UserData);

                if (!std::strcmp(Section, "override_nacp")) {
                    if (!std::strcmp(Key, "name")) {
                        e->name = Value;
                    } else if (!std::strcmp(Key, "author")) {
                        e->author = Value;
                    }
                }

                return 1;
            };

            ini_browse(cb, &overrides, fs::AppendPath(tweak_path, "config.ini"));

            if (!icon.empty() && icon.size() < sizeof(NsApplicationControlData::icon)) {
                log_write("[TITLE] overriding icon: %zu -> %zu\n", result->icon.size(), icon.size());
                result->icon = icon;
            }

            if (!overrides.name.empty() && overrides.name.length() < sizeof(result->lang.name)) {
                log_write("[TITLE] overriding name: %s -> %s\n", result->lang.name, overrides.name.c_str());
                std::strcpy(result->lang.name, overrides.name.c_str());
            }

            if (!overrides.author.empty() && overrides.author.length() < sizeof(result->lang.author)) {
                log_write("[TITLE] overriding author: %s -> %s\n", result->lang.author, overrides.author.c_str());
                std::strcpy(result->lang.author, overrides.author.c_str());
            }
        }
    }

    SCOPED_MUTEX(&m_mutex_result);
    return m_result.emplace_back(std::move(result)).get();
}

void ThreadFunc(void* user) {
    auto data = static_cast<ThreadData*>(user);

    if (data->IsTitleCacheEnabled() && !nxtcInitialize()) {
        log_write("[NXTC] failed to init cache\n");
    }
    ON_SCOPE_EXIT(nxtcExit());

    while (data->IsRunning()) {
        data->Run();
    }
}

} // namespace

// starts background thread.
Result Init() {
    SCOPED_MUTEX(&g_mutex);

    if (!g_ref_count) {
        R_TRY(nsInitialize());
        R_TRY(ncmInitialize());

        for (auto& e : ncm_entries) {
            e.Open();
        }

        g_thread_data = std::make_unique<ThreadData>(true);
        R_TRY(threadCreate(&g_thread, ThreadFunc, g_thread_data.get(), nullptr, 1024*32, THREAD_PRIO, THREAD_CORE));
        svcSetThreadCoreMask(g_thread.handle, THREAD_CORE, THREAD_AFFINITY_DEFAULT(THREAD_CORE));
        R_TRY(threadStart(&g_thread));
    }

    g_ref_count++;
    R_SUCCEED();
}

void Exit() {
    SCOPED_MUTEX(&g_mutex);

    if (!g_ref_count) {
        return;
    }

    g_ref_count--;
    if (!g_ref_count) {
        g_thread_data->Close();

        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_thread_data.reset();

        for (auto& e : ncm_entries) {
            e.Close();
        }

        nsExit();
        ncmExit();
    }
}

void Clear() {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        g_thread_data->Clear();
    }
}

void PushAsync(u64 app_id) {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        g_thread_data->PushAsync(app_id);
    }
}

auto GetAsync(u64 app_id) -> ThreadResultData* {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        return g_thread_data->GetAsync(app_id);
    }
    return {};
}

auto Get(u64 app_id, bool* cached) -> ThreadResultData* {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        return g_thread_data->Get(app_id, cached);
    }
    return {};
}

auto GetNcmCs(u8 storage_id) -> NcmContentStorage& {
    return GetNcmEntry(storage_id).cs;
}

auto GetNcmDb(u8 storage_id) -> NcmContentMetaDatabase& {
    return GetNcmEntry(storage_id).db;
}

Result GetMetaEntries(u64 id, MetaEntries& out, u32 flags) {
    for (s32 i = 0; ; i++) {
        s32 count;
        NsApplicationContentMetaStatus status;
        R_TRY(nsListApplicationContentMetaStatus(id, i, &status, 1, &count));

        if (!count) {
            break;
        }

        if (flags & ContentMetaTypeToContentFlag(status.meta_type)) {
            out.emplace_back(status);
        }
    }

    R_SUCCEED();
}

Result GetControlPathFromStatus(const NsApplicationContentMetaStatus& status, u64* out_program_id, fs::FsPath* out_path) {
    const auto& ee = status;
    if (ee.storageID != NcmStorageId_SdCard && ee.storageID != NcmStorageId_BuiltInUser && ee.storageID != NcmStorageId_GameCard) {
        return 0x1;
    }

    auto& db = GetNcmDb(ee.storageID);
    auto& cs = GetNcmCs(ee.storageID);

    NcmContentMetaKey key;
    R_TRY(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, ee.application_id));

    NcmContentId content_id;
    R_TRY(ncmContentMetaDatabaseGetContentIdByType(&db, &content_id, &key, NcmContentType_Control));

    R_TRY(ncmContentStorageGetProgramId(&cs, out_program_id, &content_id, FsContentAttributes_All));

    R_TRY(ncmContentStorageGetPath(&cs, out_path->s, sizeof(*out_path), &content_id));
    R_SUCCEED();
}

// taken from nxdumptool.
void utilsReplaceIllegalCharacters(char *str, bool ascii_only)
{
    static const char g_illegalFileSystemChars[] = "\\/:*?\"<>|";

    size_t str_size = 0, cur_pos = 0;

    if (!str || !(str_size = strlen(str))) return;

    u8 *ptr1 = (u8*)str, *ptr2 = ptr1;
    ssize_t units = 0;
    u32 code = 0;
    bool repl = false;

    while(cur_pos < str_size)
    {
        units = decode_utf8(&code, ptr1);
        if (units < 0) break;

        if (code < 0x20 || (!ascii_only && code == 0x7F) || (ascii_only && code >= 0x7F) || \
            (units == 1 && memchr(g_illegalFileSystemChars, (int)code, std::size(g_illegalFileSystemChars))))
        {
            if (!repl)
            {
                *ptr2++ = '_';
                repl = true;
            }
        } else {
            if (ptr2 != ptr1) memmove(ptr2, ptr1, (size_t)units);
            ptr2 += units;
            repl = false;
        }

        ptr1 += units;
        cur_pos += (size_t)units;
    }

    *ptr2 = '\0';
}

auto GetContentsPath(u64 app_id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "/atmosphere/contents/%016lX", app_id);
    return path;
}

namespace {

constexpr bool IsMovableStorage(u8 storage_id) {
    return storage_id == NcmStorageId_SdCard || storage_id == NcmStorageId_BuiltInUser;
}

// a move runs on a worker thread while the menu underneath keeps issuing ncm
// requests from its Draw() (icons, per-title sizes). the kernel serialises
// requests per session, so sharing the global handles would park every frame
// behind a multi-MiB transfer and freeze the ui - the move gets its own.
struct NcmSession {
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};

    Result Open(u8 storage_id) {
        R_TRY(ncmOpenContentStorage(std::addressof(cs), (NcmStorageId)storage_id));
        R_TRY(ncmOpenContentMetaDatabase(std::addressof(db), (NcmStorageId)storage_id));
        R_SUCCEED();
    }

    ~NcmSession() {
        ncmContentStorageClose(std::addressof(cs));
        ncmContentMetaDatabaseClose(std::addressof(db));
    }
};

// resolves the ncm meta key of a component on its own storage, plus the ncas it
// is made of.
Result GetComponentContents(NcmContentMetaDatabase& db, const NsApplicationContentMetaStatus& status, NcmContentMetaKey& key, std::vector<NcmContentInfo>& infos) {
    const auto app_id = ncm::GetAppId(status.meta_type, status.application_id);
    auto id_min = status.application_id;
    auto id_max = status.application_id;
    if (status.storageID == NcmStorageId_None || status.storageID == NcmStorageId_GameCard) {
        id_min -= 1;
        id_max += 1;
    }

    s32 meta_total{};
    s32 meta_written{};
    key = {};
    R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(meta_total), std::addressof(meta_written), std::addressof(key), 1, (NcmContentMetaType)status.meta_type, app_id, id_min, id_max, NcmContentInstallType_Full));
    R_UNLESS(meta_written == 1, Result_GameEmptyMetaEntries);

    return ncm::GetContentInfos(std::addressof(db), std::addressof(key), infos);
}

auto SumContentSize(const std::vector<NcmContentInfo>& infos) -> u64 {
    u64 total{};
    for (const auto& info : infos) {
        u64 size{};
        ncmContentInfoSizeToU64(std::addressof(info), std::addressof(size));
        total += size;
    }
    return total;
}

// undoes everything a component copy placed on the target storage. armed until
// Commit(), which is only reached once the application record points at the new
// storage - up to that moment the source copy is still whole and still the one
// the system launches from.
struct MoveRollback {
    NcmContentStorage* cs{};
    NcmContentMetaDatabase* db{};
    std::vector<NcmPlaceHolderId> placeholders{};
    std::vector<NcmContentId> registered{};
    NcmContentMetaKey meta_key{};
    bool has_meta{};
    bool committed{};

    void Commit() {
        committed = true;
    }

    ~MoveRollback() {
        if (committed) {
            return;
        }

        log_write("[MOVE] rolling back %zu placeholders, %zu ncas, meta: %u\n", placeholders.size(), registered.size(), has_meta);

        if (has_meta) {
            ncmContentMetaDatabaseRemove(db, std::addressof(meta_key));
            ncmContentMetaDatabaseCommit(db);
        }
        for (const auto& id : registered) {
            ncmContentStorageDelete(cs, std::addressof(id));
        }
        for (const auto& id : placeholders) {
            ncmContentStorageDeletePlaceHolder(cs, std::addressof(id));
        }
    }
};

// rewrites the application record so the moved key resolves to its new storage.
// ns keeps its own copy of "which storage holds which meta key" and it is that
// copy - not the ncm databases - which decides whether the title launches, so
// this is the single commit point of a move.
Result RepointApplicationRecord(u64 app_id, const NcmContentMetaKey& key, NcmStorageId target_storage) {
    ns::AppManager am;
    R_UNLESS(am, Result_GameMoveNoAppManager);

    // the whole list is re-pushed below, so a truncated read would silently drop
    // records (and with them, installed dlc): page until ns runs out.
    std::vector<ncm::ContentStorageRecord> records;
    constexpr s32 PAGE = 32;
    for (;;) {
        const auto offset = records.size();
        records.resize(offset + PAGE);

        s32 count{};
        R_TRY(ns::ListApplicationRecordContentMeta(am.get(), offset, app_id, records.data() + offset, PAGE, std::addressof(count)));
        records.resize(offset + count);

        if (count < PAGE) {
            break;
        }
    }

    bool found{};
    for (auto& record : records) {
        if (record.key.id == key.id && record.key.type == key.type) {
            record.storage_id = target_storage;
            found = true;
        }
    }

    if (!found) {
        auto& record = records.emplace_back();
        record.key = key;
        record.storage_id = target_storage;
    }

    // ns has no "update one record" cmd - the list is replaced wholesale.
    R_TRY(ns::DeleteApplicationRecord(am.get(), app_id));
    R_TRY(ns::PushApplicationRecord(am.get(), app_id, records.data(), records.size()));
    R_SUCCEED();
}

// copy -> register -> repoint the record -> only then delete the source.
// done/total drive one continuous progress bar across every component of a title.
Result MoveComponentImpl(const NsApplicationContentMetaStatus& status, NcmStorageId target_storage, ui::ProgressBox* pbox, u64& done, u64 total) {
    const auto src_storage = static_cast<NcmStorageId>(status.storageID);
    if (src_storage == target_storage) {
        R_SUCCEED();
    }
    R_UNLESS(IsMovableStorage(src_storage), Result_GameEmptyMetaEntries);
    R_UNLESS(IsMovableStorage(target_storage), Result_GameEmptyMetaEntries);

    NcmSession src{}, dst{};
    R_TRY(src.Open(src_storage));
    R_TRY(dst.Open(target_storage));

    auto& src_db = src.db;
    auto& src_cs = src.cs;
    auto& dst_db = dst.db;
    auto& dst_cs = dst.cs;

    NcmContentMetaKey key{};
    std::vector<NcmContentInfo> infos;
    R_TRY(GetComponentContents(src_db, status, key, infos));

    u64 meta_size{};
    R_TRY(ncmContentMetaDatabaseGetSize(std::addressof(src_db), std::addressof(meta_size), std::addressof(key)));
    std::vector<u8> meta_buf(meta_size);
    u64 out_meta_size{};
    R_TRY(ncmContentMetaDatabaseGet(std::addressof(src_db), std::addressof(key), std::addressof(out_meta_size), meta_buf.data(), meta_buf.size()));

    const auto meta_label = i18n::get(ncm::GetReadableMetaTypeStr(status.meta_type));

    auto file_label = [&](const NcmContentInfo& info, size_t i, u64 nca_size) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s · %s  (%zu/%zu)  %s",
            meta_label.c_str(), ncm::GetContentTypeStr(info.content_type),
            i + 1, infos.size(), utils::formatSizeStorage(nca_size).c_str());
        return std::string{buf};
    };

    auto show_step = [&](const std::string& step, const std::string& file, s64 offset, s64 size) {
        if (!pbox) {
            return;
        }
        pbox->SetTransfer(file.empty() ? step : (step + " · " + file));
        if (size > 0) {
            pbox->UpdateTransfer(offset, size);
        }
        // ncm CreatePlaceHolder can sit on a multi-GB allocate with no byte
        // callbacks; sleep one frame so the label actually paints first.
        pbox->Yield();
        svcSleepThread(16'000'000);
    };

    MoveRollback rollback{std::addressof(dst_cs), std::addressof(dst_db)};

    // every WritePlaceHolder is its own transaction on a journalled bis
    // partition, so small chunks cost throughput rather than saving latency -
    // 512 KiB made a move roughly four times slower, which is what a bar that
    // "never moves" actually was. same sizing as the installer.
    const u64 CHUNK_SIZE = App::IsFileBaseEmummc() ? 512ULL * 1024ULL : 4ULL * 1024ULL * 1024ULL;
    std::vector<u8> chunk_buf(CHUNK_SIZE);

    for (size_t i = 0; i < infos.size(); i++) {
        const auto& info = infos[i];
        u64 nca_size{};
        ncmContentInfoSizeToU64(std::addressof(info), std::addressof(nca_size));
        const auto file = file_label(info, i, nca_size);

        bool has{};
        ncmContentStorageHas(std::addressof(dst_cs), std::addressof(has), std::addressof(info.content_id));
        if (has) {
            // already present on the target (shared nca), nothing to copy.
            done += nca_size;
            if (pbox) {
                pbox->UpdateTransfer(nca_size, nca_size ? nca_size : 1);
            }
            continue;
        }

        show_step("Allocating"_i18n, file, 0, nca_size ? static_cast<s64>(nca_size) : 1);

        NcmPlaceHolderId placeholder_id{};
        R_TRY(ncmContentStorageGeneratePlaceHolderId(std::addressof(dst_cs), std::addressof(placeholder_id)));
        R_TRY(ncmContentStorageCreatePlaceHolder(std::addressof(dst_cs), std::addressof(info.content_id), std::addressof(placeholder_id), nca_size));
        rollback.placeholders.emplace_back(placeholder_id);

        show_step("Copying"_i18n, file, 0, nca_size ? static_cast<s64>(nca_size) : 1);

        u64 nca_offset{};
        TimeStamp log_ts;
        u64 log_done = done;
        while (nca_offset < nca_size) {
            R_TRY(pbox ? pbox->ShouldExitResult() : 0);

            const u64 to_read = std::min<u64>(CHUNK_SIZE, nca_size - nca_offset);
            R_TRY(ncmContentStorageReadContentIdFile(std::addressof(src_cs), chunk_buf.data(), to_read, std::addressof(info.content_id), nca_offset));
            R_TRY(ncmContentStorageWritePlaceHolder(std::addressof(dst_cs), std::addressof(placeholder_id), nca_offset, chunk_buf.data(), to_read));

            nca_offset += to_read;
            done += to_read;
            if (pbox) {
                pbox->UpdateTransfer(static_cast<s64>(nca_offset), static_cast<s64>(nca_size));
            }

            // one line a second, so a "the bar never moves" report can be
            // answered with the actual throughput instead of a guess.
            if (log_ts.GetSeconds() >= 1) {
                const auto secs = log_ts.GetSecondsD();
                log_write("[MOVE] %llu/%llu MiB  %.2f MiB/s\n",
                    (unsigned long long)(done >> 20), (unsigned long long)(total >> 20),
                    secs > 0 ? ((double)(done - log_done) / (1024.0 * 1024.0)) / secs : 0.0);
                log_ts.Update();
                log_done = done;
            }
        }

        R_TRY(ncmContentStorageRegister(std::addressof(dst_cs), std::addressof(info.content_id), std::addressof(placeholder_id)));
        rollback.placeholders.pop_back();
        rollback.registered.emplace_back(info.content_id);
    }

    // every nca is on the target now; publish the meta entry there.
    show_step("Updating ncm database"_i18n, {}, 1, 1);
    R_TRY(ncmContentMetaDatabaseSet(std::addressof(dst_db), std::addressof(key), meta_buf.data(), meta_buf.size()));
    R_TRY(ncmContentMetaDatabaseCommit(std::addressof(dst_db)));
    rollback.meta_key = key;
    rollback.has_meta = true;

    // point of no return: from here the title launches from the target copy.
    // cancelling is refused rather than half-applied.
    show_step("Pushing application record"_i18n, {}, 1, 1);
    R_TRY(RepointApplicationRecord(ncm::GetAppId(key), key, target_storage));
    rollback.Commit();

    show_step("Removing old copy"_i18n, {}, 1, 1);
    R_TRY(ncm::DeleteKey(std::addressof(src_cs), std::addressof(src_db), std::addressof(key)));

    if (ns::AppManager am; am) {
        ns::InvalidateApplicationControlCache(am.get(), status.application_id);
    }

    R_SUCCEED();
}

} // namespace

Result GetMovePlan(u64 app_id, NcmStorageId target_storage, MovePlan& out) {
    out = {};

    MetaEntries entries;
    R_TRY(GetMetaEntries(app_id, entries));

    for (const auto& status : entries) {
        NcmContentMetaKey key{};
        std::vector<NcmContentInfo> infos;
        // plans are built on the ui thread before any transfer starts, so the
        // shared sessions are fine here.
        if (R_FAILED(GetComponentContents(GetNcmDb(status.storageID), status, key, infos))) {
            continue;
        }

        const MoveEntry entry{status, SumContentSize(infos)};
        if (status.storageID == NcmStorageId_SdCard) {
            out.sd_size += entry.size;
        } else if (status.storageID == NcmStorageId_BuiltInUser) {
            out.nand_size += entry.size;
        }

        if (IsMovableStorage(status.storageID) && status.storageID != target_storage) {
            out.move_size += entry.size;
            out.move.emplace_back(entry);
        } else {
            out.stay.emplace_back(entry);
        }
    }

    R_SUCCEED();
}

Result MoveComponent(const NsApplicationContentMetaStatus& status, NcmStorageId target_storage, ui::ProgressBox* pbox) {
    NcmContentMetaKey key{};
    std::vector<NcmContentInfo> infos;
    R_TRY(GetComponentContents(GetNcmDb(status.storageID), status, key, infos));

    u64 done{};
    const auto total = SumContentSize(infos);
    if (pbox) {
        pbox->NewTransfer(i18n::get(ncm::GetReadableMetaTypeStr(status.meta_type)));
        pbox->UpdateTransfer(0, total);
    }

    return MoveComponentImpl(status, target_storage, pbox, done, total);
}

Result MoveApplication(u64 app_id, NcmStorageId target_storage, ui::ProgressBox* pbox) {
    if (pbox) {
        pbox->NewTransfer("Preparing move"_i18n);
        pbox->UpdateTransfer(0, 1);
    }

    MovePlan plan;
    R_TRY(GetMovePlan(app_id, target_storage, plan));
    if (plan.move.empty()) {
        R_SUCCEED();
    }

    // refuse up front rather than filling the target and failing halfway: a
    // partial move leaves ncas stranded on both storages.
    s64 nand_free{}, sd_free{};
    fs::GetStorageSpaces(&nand_free, nullptr, &sd_free, nullptr);
    const auto free_space = target_storage == NcmStorageId_SdCard ? sd_free : nand_free;
    R_UNLESS(free_space <= 0 || (u64)free_space > plan.move_size, Result_GameMoveNotEnoughSpace);

    u64 done{};

    for (const auto& entry : plan.move) {
        R_TRY(pbox ? pbox->ShouldExitResult() : 0);
        R_TRY(MoveComponentImpl(entry.status, target_storage, pbox, done, plan.move_size));
    }

    R_SUCCEED();
}

} // namespace sphaira::title
