#include "title_internal.hpp"
#include "title_info.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/types.hpp"
#include "yati/nx/nca.hpp"

#include <cstring>
#include <cstdio>
#include <vector>
#include <zlib.h>

namespace sphaira::title {

// also sets the status to error.
void FakeNacpEntry(ThreadResultData* e) {
    e->status = NacpLoadStatus::Error;
    // fake the nacp entry
    std::strcpy(e->lang.name, "Corrupted"); // literal, bounded
    std::strcpy(e->lang.author, "Corrupted"); // literal, bounded
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

} // namespace sphaira::title
