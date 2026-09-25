#include "env.h"
#include <string.h>

bool g_isApplication = false;
bool g_isAutomaticGameplayRecording = false;

CodeMemoryCapability g_codeMemoryCapability = CodeMemoryUnavailable;

void*  g_heapAddr = {0};
size_t g_heapSize = {0};

Handle g_procHandle = {0};

void getIsApplication(void) {
    Result rc;

    // Try asking the kernel directly (only works on [9.0.0+] or mesosphère)
    u64 flag = 0;
    rc = svcGetInfo(&flag, InfoType_IsApplication, CUR_PROCESS_HANDLE, 0);
    if (R_SUCCEEDED(rc)) {
        g_isApplication = flag != 0;
        return;
    }

    // Retrieve our process' PID
    u64 cur_pid = 0;
    rc = svcGetProcessId(&cur_pid, CUR_PROCESS_HANDLE);
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    // Try reading the current application PID through pm:shell
    rc = pmshellInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 app_pid = 0;
        rc = pmshellGetApplicationProcessIdForShell(&app_pid);
        pmshellExit();

        if (cur_pid == app_pid)
            g_isApplication = true;
    }
}

void getIsAutomaticGameplayRecording(void) {
    Result rc;

    // Do nothing if the HOS version predates [4.0.0], or we're not an application.
    if (hosversionBefore(4, 0, 0) || !g_isApplication)
        return;

    // Retrieve our process' Program ID
    u64 cur_progid = 0;
    rc = svcGetInfo(&cur_progid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    // Try reading our NACP
    rc = nsInitialize();
    if (R_SUCCEEDED(rc)) {
        NsApplicationControlData data;
        u64 size = 0;
        rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, cur_progid, &data, sizeof(data), &size);
        nsExit();

        if (R_SUCCEEDED(rc) && data.nacp.video_capture == 2)
            g_isAutomaticGameplayRecording = true;
    }
}

u64 calculateMaxHeapSize(void) {
    u64 size = 0;
    u64 mem_available = 0, mem_used = 0;

    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    if (mem_available > mem_used + 0x200000)
        size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
        size = 0x2000000 * 16;
    if (size > 0x6000000 && g_isAutomaticGameplayRecording)
        size -= 0x6000000;

    return size;
}

void setupHbHeap(void) {
    void* addr = NULL;
    u64 size = calculateMaxHeapSize();
    Result rc = svcSetHeapSize(&addr, size);

    if (R_FAILED(rc) || addr==NULL)
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 9));

    g_heapAddr = addr;
    g_heapSize = size;
}

static void procHandleReceiveThread(void* arg) {
    Handle session = (Handle)(uintptr_t)arg;
    Result rc;

    void* base = armGetTls();
    hipcMakeRequestInline(base);

    s32 idx = 0;
    rc = svcReplyAndReceive(&idx, &session, 1, INVALID_HANDLE, UINT64_MAX);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 15));

    HipcParsedRequest r = hipcParseRequest(base);
    if (r.meta.num_copy_handles != 1)
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 17));

    g_procHandle = r.data.copy_handles[0];
    svcCloseHandle(session);
}

void getOwnProcessHandle(void) {
    Result rc;

    Handle server_handle, client_handle;
    rc = svcCreateSession(&server_handle, &client_handle, 0, 0);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 12));

    Thread t;
    u8* stack = g_heapAddr;
    rc = threadCreate(&t, &procHandleReceiveThread, (void*)(uintptr_t)server_handle, stack, 0x1000, 0x20, 0);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 10));

    rc = threadStart(&t);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 13));

    hipcMakeRequestInline(armGetTls(),
        .num_copy_handles = 1,
    ).copy_handles[0] = CUR_PROCESS_HANDLE;

    svcSendSyncRequest(client_handle);
    svcCloseHandle(client_handle);

    threadWaitForExit(&t);
    threadClose(&t);
}

static bool isKernel5xOrLater(void) {
    u64 dummy = 0;
    Result rc = svcGetInfo(&dummy, InfoType_UserExceptionContextAddress, INVALID_HANDLE, 0);
    return R_VALUE(rc) != KERNELRESULT(InvalidEnumValue);
}

static bool isKernel4x(void) {
    u64 dummy = 0;
    Result rc = svcGetInfo(&dummy, InfoType_InitialProcessIdRange, INVALID_HANDLE, 0);
    return R_VALUE(rc) != KERNELRESULT(InvalidEnumValue);
}

void getCodeMemoryCapability(void) {
    if (detectMesosphere()) {
        // Mesosphère allows for same-process code memory usage.
        g_codeMemoryCapability = CodeMemorySameProcess;
    } else if (isKernel5xOrLater()) {
        // On [5.0.0+], the kernel does not allow the creator process of a CodeMemory object
        // to use svcControlCodeMemory on itself, thus returning InvalidMemoryState (0xD401).
        // However the kernel can be patched to support same-process usage of CodeMemory.
        // We can detect that by passing a bad operation and observe if we actually get InvalidEnumValue (0xF001).
        Handle code;
        Result rc = svcCreateCodeMemory(&code, g_heapAddr, 0x1000);
        if (R_SUCCEEDED(rc)) {
            rc = svcControlCodeMemory(code, (CodeMapOperation)-1, 0, 0x1000, 0);
            svcCloseHandle(code);

            if (R_VALUE(rc) == KERNELRESULT(InvalidEnumValue))
                g_codeMemoryCapability = CodeMemorySameProcess;
            else
                g_codeMemoryCapability = CodeMemoryForeignProcess;
        }
    } else if (isKernel4x()) {
        // On [4.0.0-4.1.0] there is no such restriction on same-process CodeMemory usage.
        g_codeMemoryCapability = CodeMemorySameProcess;
    } else {
        // This kernel is too old to support CodeMemory syscalls.
        g_codeMemoryCapability = CodeMemoryUnavailable;
    }
}

Result restoreMainThreadAffinity(void) {
    u64 core_mask = 0;
    Result rc = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc))
        return rc;

    return svcSetThreadCoreMask(CUR_THREAD_HANDLE, -1, core_mask);
}

void findUsableHeapRange(u64 override_addr, u64 override_size, u64* out_start, u64* out_size) {
    if (override_size == 0 || override_addr == 0 || (override_addr + override_size) <= override_addr) {
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 28));
    }

    const u64 override_end = override_addr + override_size;
    u64 cur_addr = override_addr;

    u64 best_start = 0;
    u64 best_size = 0;
    u64 current_usable_start = 0;
    u64 current_usable_size = 0;

    while (cur_addr < override_end) {
        MemoryInfo info = {0};
        u32 pageinfo = 0;
        Result rc = svcQueryMemory(&info, &pageinfo, cur_addr);
        if (R_FAILED(rc)) {
            diagAbortWithResult(rc);
        }

        if (info.size == 0 || (info.addr + info.size) <= info.addr || cur_addr < info.addr || cur_addr >= (info.addr + info.size)) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 28));
        }

        const u64 mem_end = info.addr + info.size;
        const u64 block_start = (info.addr < override_addr) ? override_addr : info.addr;
        const u64 block_end = (mem_end > override_end) ? override_end : mem_end;
        const u64 block_size = block_end - block_start;

        const bool is_usable = ((info.type & MemState_Type) == MemType_Heap) &&
                               (info.perm == Perm_Rw) &&
                               (info.attr == 0);

        if (is_usable && (block_start % 0x1000 == 0) && (block_size % 0x1000 == 0)) {
            if (current_usable_size > 0 && (current_usable_start + current_usable_size) == block_start) {
                current_usable_size += block_size;
            } else {
                if (current_usable_size > best_size) {
                    best_size = current_usable_size;
                    best_start = current_usable_start;
                }
                current_usable_start = block_start;
                current_usable_size = block_size;
            }
        } else {
            if (current_usable_size > best_size) {
                best_size = current_usable_size;
                best_start = current_usable_start;
            }
            current_usable_start = 0;
            current_usable_size = 0;
        }

        if (mem_end <= cur_addr) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 28));
        }
        cur_addr = mem_end;
    }

    if (current_usable_size > best_size) {
        best_size = current_usable_size;
        best_start = current_usable_start;
    }

    if (best_size == 0) {
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 28));
    }

    *out_start = best_start;
    *out_size = best_size;
}
