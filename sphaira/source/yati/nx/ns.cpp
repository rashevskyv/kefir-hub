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

Result DeleteApplicationRecord(Service* srv, u64 tid) {
    return serviceDispatchIn(srv, 27, tid);
}

Result InvalidateApplicationControlCache(Service* srv, u64 tid) {
    return serviceDispatchIn(srv, 404, tid);
}

} // namespace sphaira::ns
