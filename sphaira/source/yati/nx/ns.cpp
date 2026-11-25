#include "yati/nx/ns.hpp"
#include "log.hpp"

namespace sphaira::ns {
namespace {

} // namespace

Result PushApplicationRecord(Service* srv, u64 tid, const ncm::ContentStorageRecord* records, u32 count) {
    const struct {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = { ApplicationRecordType_Installed, {0}, tid };

    return serviceDispatchIn(srv, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { records, sizeof(*records) * count } });
}

Result ListApplicationRecordContentMeta(Service* srv, u64 offset, u64 tid, ncm::ContentStorageRecord* out, u32 count, s32* out_count) {
    const struct {
        u64 offset;
        u64 tid;
    } in = { offset, tid };

    return serviceDispatchInOut(srv, 17, in, *out_count,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { out, sizeof(*out) * count } });
}

AppManager::AppManager() {
    if (hosversionAtLeast(3,0,0)) {
        if (R_SUCCEEDED(nsGetApplicationManagerInterface(&m_owned))) {
            m_srv = &m_owned;
            m_own = true;
        }
    } else {
        m_srv = nsGetServiceSession_ApplicationManagerInterface();
    }
}

AppManager::~AppManager() {
    if (m_own) {
        serviceClose(&m_owned);
    }
}

Result DeleteApplicationRecord(Service* srv, u64 tid) {
    return serviceDispatchIn(srv, 27, tid);
}

Result InvalidateApplicationControlCache(Service* srv, u64 tid) {
    const auto rc = serviceDispatchIn(srv, 404, tid);
    // 19.0.0+ HOME reads a second cache ("of the stage") that 404 leaves and a reboot keeps:
    // without it an edited name or icon stays old in HOME. Its own result is only logged.
    if (hosversionAtLeast(19,0,0)) {
        const auto stage_rc = serviceDispatchIn(srv, 418, tid);
        log_write("[NS] %016lX control cache 0x%X, stage cache 0x%X\n", tid, rc, stage_rc);
    }
    return rc;
}

} // namespace sphaira::ns
