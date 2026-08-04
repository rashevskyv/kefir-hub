#pragma once

#include <switch.h>
#include "ncm.hpp"

namespace sphaira::ns {

enum ApplicationRecordType {
    ApplicationRecordType_Running         = 0x0,
    ApplicationRecordType_Installed       = 0x3,
    ApplicationRecordType_Downloading     = 0x4,
    // application is gamecard, but gamecard isn't insterted
    ApplicationRecordType_GamecardMissing = 0x5,
    ApplicationRecordType_Downloaded      = 0x6,
    ApplicationRecordType_Updated         = 0xA,
    ApplicationRecordType_Archived        = 0xB,
};

Result PushApplicationRecord(Service* srv, u64 tid, const ncm::ContentStorageRecord* records, u32 count);
Result ListApplicationRecordContentMeta(Service* srv, u64 offset, u64 tid, ncm::ContentStorageRecord* out, u32 count, s32* out_count);
Result DeleteApplicationRecord(Service* srv, u64 tid);
Result InvalidateApplicationControlCache(Service* srv, u64 tid);

// opens the application manager interface, falling back to the session libnx
// already holds on <3.0.0 (where the getter cmd doesn't exist).
struct AppManager {
    AppManager();
    ~AppManager();
    Service* get() { return m_srv; }
    explicit operator bool() const { return m_srv != nullptr; }

private:
    Service m_owned{};
    Service* m_srv{};
    bool m_own{};
};

} // namespace sphaira::ns
