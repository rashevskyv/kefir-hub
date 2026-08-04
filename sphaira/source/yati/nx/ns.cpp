#include "yati/nx/ns.hpp"

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
    return serviceDispatchIn(srv, 404, tid);
}

} // namespace sphaira::ns
