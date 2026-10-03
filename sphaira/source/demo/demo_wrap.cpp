// DOCS_DEMO builds only: libnx calls wrapped with -Wl,--wrap=<fn> (sphaira/CMakeLists.txt).
// Each wrapper returns the real result plus the demo entries, so Eden's own content stays.

#include "demo/demo_data.hpp"
#include "defines.hpp"

#include <algorithm>

extern "C" {

Result __real_nsListApplicationRecord(NsApplicationRecord* records, s32 count, s32 entry_offset, s32* out_entrycount);

// real records first, demo ids after them: offsets past the real total index the demo list.
Result __wrap_nsListApplicationRecord(NsApplicationRecord* records, s32 count, s32 entry_offset, s32* out_entrycount) {
    s32 real{};
    R_TRY(__real_nsListApplicationRecord(records, count, entry_offset, &real));
    *out_entrycount = real;
    if (real == count) {
        R_SUCCEED();
    }

    // a short read means the real list ends here; with nothing read, count it.
    s32 real_total = entry_offset + real;
    if (!real && entry_offset) {
        NsApplicationRecord tmp[64];
        real_total = 0;
        for (s32 n; R_SUCCEEDED(__real_nsListApplicationRecord(tmp, 64, real_total, &n)) && n > 0; ) {
            real_total += n;
        }
    }

    const auto ids = sphaira::demo::TitleIds();
    const s32 first = std::max(0, entry_offset - real_total);
    for (s32 i = first; i < (s32)ids.size() && *out_entrycount < count; i++) {
        records[(*out_entrycount)++] = NsApplicationRecord{.application_id = ids[i]};
    }

    R_SUCCEED();
}

} // extern "C"
