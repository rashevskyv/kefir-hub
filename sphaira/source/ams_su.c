/* Atmosphere/Daybreak ams:su client. GPL-2.0-or-later. */
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include "ams_su.h"
#include "yati/nx/service_guard.h"

static Service g_amssu_srv;
static TransferMemory g_tmem;
// The update buffer is handed to ams:su as transfer memory with no access for us
// (Perm_None). It must never go back to malloc: ams:su may still hold it after our
// handle is closed, and a later malloc that carved a block out of it faulted in
// _malloc_r (Data Abort, crash reports 01791704128 / 01791707336). One buffer,
// allocated once, kept for the life of the process.
static void* g_update_buffer;
NX_GENERATE_SERVICE_GUARD(amssu);

Result _amssuInitialize(void) { return smGetService(&g_amssu_srv, "ams:su"); }
void _amssuCleanup(void) { serviceClose(&g_amssu_srv); tmemCloseHandle(&g_tmem); }

static void copy_path(char out[FS_MAX_PATH], const char* path) {
    strncpy(out, path, FS_MAX_PATH - 1);
    out[FS_MAX_PATH - 1] = 0;
}

Result amssuGetUpdateInformation(AmsSuUpdateInformation* out, const char* path) {
    char send_path[FS_MAX_PATH] = {0}; copy_path(send_path, path);
    return serviceDispatchOut(&g_amssu_srv, 0, *out,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcPointer | SfBufferAttr_FixedSize },
        .buffers = { { send_path, FS_MAX_PATH } });
}

Result amssuValidateUpdate(AmsSuUpdateValidationInfo* out, const char* path) {
    char send_path[FS_MAX_PATH] = {0}; copy_path(send_path, path);
    return serviceDispatchOut(&g_amssu_srv, 1, *out,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcPointer | SfBufferAttr_FixedSize },
        .buffers = { { send_path, FS_MAX_PATH } });
}

Result amssuSetupUpdate(void* buffer, size_t size, const char* path, bool exfat) {
    if (!buffer) {
        if (!g_update_buffer) {
            g_update_buffer = aligned_alloc(0x1000, size);
            if (!g_update_buffer) return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
        }
        buffer = g_update_buffer;
    }
    Result rc = tmemCreateFromMemory(&g_tmem, buffer, size, Perm_None);
    if (R_FAILED(rc)) return rc;
    char send_path[FS_MAX_PATH] = {0}; copy_path(send_path, path);
    const struct { u8 exfat; u64 size; } in = { exfat, g_tmem.size };
    rc = serviceDispatchIn(&g_amssu_srv, 2, in,
        .in_num_handles = 1, .in_handles = { g_tmem.handle },
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcPointer | SfBufferAttr_FixedSize },
        .buffers = { { send_path, FS_MAX_PATH } });
    if (R_FAILED(rc)) tmemCloseHandle(&g_tmem);
    return rc;
}

Result amssuRequestPrepareUpdate(AsyncResult* out) {
    memset(out, 0, sizeof(*out));
    Handle event = INVALID_HANDLE;
    Result rc = serviceDispatch(&g_amssu_srv, 4, .out_num_objects = 1,
        .out_objects = &out->s, .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = &event);
    if (R_SUCCEEDED(rc)) eventLoadRemote(&out->event, event, false);
    return rc;
}

Result amssuGetPrepareUpdateProgress(NsSystemUpdateProgress* out) {
    return serviceDispatchOut(&g_amssu_srv, 5, *out);
}
Result amssuHasPreparedUpdate(bool* out) {
    u8 value = 0; Result rc = serviceDispatchOut(&g_amssu_srv, 6, value);
    if (R_SUCCEEDED(rc) && out) *out = value & 1;
    return rc;
}
Result amssuApplyPreparedUpdate(void) { return serviceDispatch(&g_amssu_srv, 7); }
