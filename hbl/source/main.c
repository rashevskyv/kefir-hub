#include "env.h"
#include <string.h>

#define EXIT_DETECTION_STR "if this isn't replaced i will exit :)"

// this uses 16KiB less than nx-hbloader.
static char g_argv[2048] = {0};
// this can and will be modified by libnx if launched nro calls envSetNextLoad().
static char g_nextArgv[2048] = {0};
static char g_nextNroPath[FS_MAX_PATH] = {0};
// stores first launched nro argv + path.
static char g_defaultArgv[2048] = {0};
static char g_defaultNroPath[FS_MAX_PATH] = {0};

static const char g_noticeText[] = { "sphaira " VERSION };

static u64 g_nroSize = 0;
static NroHeader g_nroHeader = {0};

static u128 g_userIdStorage = {0};
static u8 g_savedTls[0x100] = {0};

// Used by trampoline.s
u64 g_nroAddr = 0;
Result g_lastRet = 0;

void NX_NORETURN nroEntrypointTrampoline(const ConfigEntry* entries, u64 handle, u64 entrypoint);

static void fix_nro_path(char* path) {
    // hbmenu prefixes paths with sdmc: which fsFsOpenFile won't like
    if (!strncmp(path, "sdmc:/", 6)) {
        // memmove(path, path + 5, strlen(path)-5);
        memmove(path, path + 5, FS_MAX_PATH-5);
    }
}

// Credit to behemoth
// SOURCE: https://github.com/HookedBehemoth/nx-hbloader/commit/7f8000a41bc5e8a6ad96a097ef56634cfd2fabcb
static void NX_NORETURN selfExit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc))
        goto fail0;

    Service applet, proxy, self;

    rc = smGetService(&applet, "appletOE");
    if (R_FAILED(rc))
        goto fail1;

    const u32 cmd_id = 0;
    const u64 reserved = 0;

    // GetSessionProxy
    rc = serviceDispatchIn(&applet, cmd_id, reserved,
        .in_send_pid = true,
        .in_num_handles = 1,
        .in_handles = { g_procHandle },
        .out_num_objects = 1,
        .out_objects = &proxy,
    );
    if (R_FAILED(rc))
        goto fail2;

    // GetSelfController
    rc = serviceDispatch(&proxy, 1,
        .out_num_objects = 1,
        .out_objects = &self,
    );
    if (R_FAILED(rc))
        goto fail3;

    // Exit
    rc = serviceDispatch(&self, 0);

    serviceClose(&self);

fail3:
    serviceClose(&proxy);

fail2:
    serviceClose(&applet);

fail1:
    smExit();

fail0:
    if (R_SUCCEEDED(rc)) {
        while(1) svcSleepThread(86400000000000ULL);
        svcExitProcess();
        __builtin_unreachable();
    } else {
        diagAbortWithResult(rc);
    }
}


void NX_NORETURN loadNro(void) {
    NroHeader* header = NULL;
    size_t rw_size = 0;
    size_t total_size = 0;
    Result rc = 0;

    memcpy((u8*)armGetTls() + 0x100, g_savedTls, 0x100);

    // check's if the homebrew replaced nro_path.
    // if so, load new nro, otherwise, exit.
    if (!strcmp(g_nextArgv, EXIT_DETECTION_STR)) {
        if (!strcmp(g_nextNroPath, g_defaultNroPath)) {
            selfExit();
        } else {
            // exited nro, now returning to default nro
            strcpy(g_nextNroPath, g_defaultNroPath);
            strcpy(g_nextArgv, g_defaultArgv);
        }
    }

    if (g_nroSize) {
        // checks if nro was previously mapped, if so, unmap
        header = &g_nroHeader;
        const u64 seg2_size = (u64)header->segments[2].size;
        const u64 bss_size = (u64)header->bss_size;
        rw_size = (size_t)((seg2_size + bss_size + 0xFFFULL) & ~0xFFFULL);

        if (R_FAILED(rc = svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PreUnloadDll, g_nroAddr, g_nroSize))) {
            diagAbortWithResult(rc);
        }

        // .text
        rc = svcUnmapProcessCodeMemory(
            g_procHandle, g_nroAddr + header->segments[0].file_off, ((u64) g_heapAddr) + header->segments[0].file_off, header->segments[0].size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 24));

        // .rodata
        rc = svcUnmapProcessCodeMemory(
            g_procHandle, g_nroAddr + header->segments[1].file_off, ((u64) g_heapAddr) + header->segments[1].file_off, header->segments[1].size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 25));

       // .data + .bss
        rc = svcUnmapProcessCodeMemory(
            g_procHandle, g_nroAddr + header->segments[2].file_off, ((u64) g_heapAddr) + header->segments[2].file_off, rw_size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 26));

        if (R_FAILED(rc = svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PostUnloadDll, g_nroAddr, g_nroSize))) {
            diagAbortWithResult(rc);
        }

        g_nroAddr = g_nroSize = 0;
    } else {
        // otherwise, this is the first time launching, read path / argv from romfs
        FsStorage s;
        romfs_header romfs_header;

        if (R_FAILED(rc = fsOpenDataStorageByCurrentProcess(&s))) {
            diagAbortWithResult(rc);
        }

        if (R_FAILED(rc = fsStorageRead(&s, 0, &romfs_header, sizeof(romfs_header)))) {
            diagAbortWithResult(rc);
        }

        u8 romfs_dirs[1024 * 2]; // should be 1 entry ("/")
        u8 romfs_files[1024 * 4]; // should be 2 entries (argv and nro)

        if (romfs_header.dirTableSize > sizeof(romfs_dirs) || romfs_header.fileTableSize > sizeof(romfs_files)) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, LibnxError_OutOfMemory));
        }

        if (R_FAILED(rc = fsStorageRead(&s, romfs_header.dirTableOff, romfs_dirs, romfs_header.dirTableSize))) {
            diagAbortWithResult(rc);
        }

        if (R_FAILED(rc = fsStorageRead(&s, romfs_header.fileTableOff, romfs_files, romfs_header.fileTableSize))) {
            diagAbortWithResult(rc);
        }

        const romfs_dir* dir = (const romfs_dir*)romfs_dirs;
        const romfs_file* next_argv_file = (const romfs_file*)(romfs_files + dir->childFile);
        const romfs_file* next_nro_file = (const romfs_file*)(romfs_files + next_argv_file->sibling);

        if (R_FAILED(rc = fsStorageRead(&s, romfs_header.fileDataOff + next_argv_file->dataOff, g_nextArgv, next_argv_file->dataSize))) {
            diagAbortWithResult(rc);
        }

        if (R_FAILED(rc = fsStorageRead(&s, romfs_header.fileDataOff + next_nro_file->dataOff, g_nextNroPath, next_nro_file->dataSize))) {
            diagAbortWithResult(rc);
        }

        fsStorageClose(&s);

        strcpy(g_defaultNroPath, g_nextNroPath);
        strcpy(g_defaultArgv, g_nextArgv);
    }

    {
        // fix paths
        char fixedNextNroPath[FS_MAX_PATH];
        strcpy(fixedNextNroPath, g_nextNroPath);
        fix_nro_path(fixedNextNroPath);

        memcpy(g_argv, g_nextArgv, sizeof(g_argv));
        if (R_FAILED(rc = svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PreLoadDll, (uintptr_t)g_argv, sizeof(g_argv)))) {
            diagAbortWithResult(rc);
        }

        uint8_t *nrobuf = (uint8_t*) g_heapAddr;
        NroStart*  start  = (NroStart*)  (nrobuf + 0);
        header = (NroHeader*) (nrobuf + sizeof(NroStart));
        uint8_t*   rest   = (uint8_t*)   (nrobuf + sizeof(NroStart) + sizeof(NroHeader));

        FsFileSystem fs;
        if (R_FAILED(rc = fsOpenSdCardFileSystem(&fs))) {
            diagAbortWithResult(rc);
        }

        // don't fatal if we don't find the nro, exit to menu
        FsFile f;
        if (R_FAILED(rc = fsFsOpenFile(&fs, fixedNextNroPath, FsOpenMode_Read, &f))) {
            diagAbortWithResult(rc);
        }

        u64 bytes_read = 0;
        s64 offset = 0;

        if (R_FAILED(rc = fsFileRead(&f, offset, start, sizeof(*start), FsReadOption_None, &bytes_read)) ||
            bytes_read != sizeof(*start)) {
            diagAbortWithResult(rc);
        }
        offset += sizeof(*start);

        if (R_FAILED(rc = fsFileRead(&f, offset, header, sizeof(*header), FsReadOption_None, &bytes_read)) ||
            bytes_read != sizeof(*header) || header->magic != NROHEADER_MAGIC) {
            diagAbortWithResult(rc);
        }
        offset += sizeof(*header);

        const u64 nro_size_u64 = (u64)header->size;
        const u64 bss_size_u64 = (u64)header->bss_size;

        if (nro_size_u64 < sizeof(NroStart) + sizeof(NroHeader) || nro_size_u64 > g_heapSize) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
        }

        for (int i = 0; i < 3; i++) {
            const u64 seg_off = (u64)header->segments[i].file_off;
            const u64 seg_size = (u64)header->segments[i].size;
            if (seg_off >= nro_size_u64 || seg_size > nro_size_u64 ||
                (seg_off + seg_size) < seg_off || (seg_off + seg_size) > nro_size_u64)
            {
                diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
            }
        }

        const u64 seg2_off = (u64)header->segments[2].file_off;
        const u64 seg2_size = (u64)header->segments[2].size;

        const u64 rw_size_raw = seg2_size + bss_size_u64;
        if (rw_size_raw < seg2_size) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
        }

        const u64 rw_size_aligned = (rw_size_raw + 0xFFFULL) & ~0xFFFULL;
        if (rw_size_aligned < rw_size_raw) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
        }

        if (seg2_off + rw_size_aligned < seg2_off || (seg2_off + rw_size_aligned) > g_heapSize) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
        }

        const u64 total_size_raw = nro_size_u64 + bss_size_u64;
        if (total_size_raw < nro_size_u64) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
        }

        const u64 total_size_aligned = (total_size_raw + 0xFFFULL) & ~0xFFFULL;
        if (total_size_aligned < total_size_raw || total_size_aligned > g_heapSize) {
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 6));
        }

        rw_size = (size_t)rw_size_aligned;
        total_size = (size_t)total_size_aligned;

        const size_t rest_size = header->size - (sizeof(NroStart) + sizeof(NroHeader));
        if (R_FAILED(rc = fsFileRead(&f, offset, rest, rest_size, FsReadOption_None, &bytes_read)) ||
            bytes_read != rest_size) {
            diagAbortWithResult(rc);
        }

        fsFileClose(&f);
        fsFsClose(&fs);
    }

    // Copy header to elsewhere because we're going to unmap it next.
    memcpy(&g_nroHeader, header, sizeof(g_nroHeader));
    header = &g_nroHeader;

    if (total_size > header->size) {
        memset((uint8_t*)g_heapAddr + header->size, 0, total_size - header->size);
    }

    // Map code memory to a new randomized address
    virtmemLock();
    void* map_addr = virtmemFindCodeMemory(total_size, 0);
    rc = svcMapProcessCodeMemory(g_procHandle, (u64)map_addr, (u64)g_heapAddr, total_size);
    virtmemUnlock();

    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 18));

    // .text
    rc = svcSetProcessMemoryPermission(
        g_procHandle, (u64)map_addr + header->segments[0].file_off, header->segments[0].size, Perm_R | Perm_X);

    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    // .rodata
    rc = svcSetProcessMemoryPermission(
        g_procHandle, (u64)map_addr + header->segments[1].file_off, header->segments[1].size, Perm_R);

    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    // .data + .bss
    rc = svcSetProcessMemoryPermission(
        g_procHandle, (u64)map_addr + header->segments[2].file_off, rw_size, Perm_Rw);

    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    const u64 nro_size = header->segments[2].file_off + rw_size;
    const u64 raw_heap_start = ((u64) g_heapAddr) + nro_size;
    const u64 raw_heap_size  = g_heapSize + (u64) g_heapAddr - raw_heap_start;

    u64 nro_heap_start = 0;
    u64 nro_heap_size = 0;
    findUsableHeapRange(raw_heap_start, raw_heap_size, &nro_heap_start, &nro_heap_size);

    #define M EntryFlag_IsMandatory

    static ConfigEntry entries[] = {
        { EntryType_MainThreadHandle,     0, {0, 0} },
        { EntryType_ProcessHandle,        0, {0, 0} },
        { EntryType_AppletType,           0, {AppletType_SystemApplication, EnvAppletFlags_ApplicationOverride} },
        { EntryType_OverrideHeap,         M, {0, 0} },
        { EntryType_Argv,                 0, {0, 0} },
        { EntryType_NextLoadPath,         0, {0, 0} },
        { EntryType_LastLoadResult,       0, {0, 0} },
        { EntryType_SyscallAvailableHint, 0, {UINT64_MAX, UINT64_MAX} },
        { EntryType_SyscallAvailableHint2, 0, {UINT64_MAX, 0} },
        { EntryType_RandomSeed,           0, {0, 0} },
        { EntryType_UserIdStorage,        0, {(u64)(uintptr_t)&g_userIdStorage, 0} },
        { EntryType_HosVersion,           0, {0, 0} },
        { EntryType_EndOfList,            0, {(u64)(uintptr_t)g_noticeText, sizeof(g_noticeText)} }
    };

    ConfigEntry *entry_AppletType = &entries[2];
    ConfigEntry *entry_Syscalls   = &entries[7];

    if (g_isApplication) {
        entry_AppletType->Value[0] = AppletType_SystemApplication;
        entry_AppletType->Value[1] = EnvAppletFlags_ApplicationOverride;
    } else {
        entry_AppletType->Value[0] = AppletType_LibraryApplet;
        entry_AppletType->Value[1] = 0;
    }

    if (!(g_codeMemoryCapability & BIT(0))) {
        // Revoke access to svcCreateCodeMemory if it's not available.
        entry_Syscalls->Value[0x4B/64] &= ~(1UL << (0x4B%64));
    }

    if (!(g_codeMemoryCapability & BIT(1))) {
        // Revoke access to svcControlCodeMemory if it's not available for same-process usage.
        entry_Syscalls->Value[0x4C/64] &= ~(1UL << (0x4C%64)); // svcControlCodeMemory
    }

    // MainThreadHandle
    entries[0].Value[0] = envGetMainThreadHandle();
    // ProcessHandle
    entries[1].Value[0] = g_procHandle;
    // OverrideHeap
    entries[3].Value[0] = nro_heap_start;
    entries[3].Value[1] = nro_heap_size;
    // Argv
    entries[4].Value[1] = (u64)(uintptr_t)&g_argv[0];
    // NextLoadPath
    entries[5].Value[0] = (u64)(uintptr_t)&g_nextNroPath[0];
    entries[5].Value[1] = (u64)(uintptr_t)&g_nextArgv[0];
    // LastLoadResult
    entries[6].Value[0] = g_lastRet;
    // RandomSeed
    entries[9].Value[0] = randomGet64();
    entries[9].Value[1] = randomGet64();
    // HosVersion
    entries[11].Value[0] = hosversionGet();
    entries[11].Value[1] = hosversionIsAtmosphere() ? 0x41544d4f53504852UL : 0; // 'ATMOSPHR'

    g_nroAddr = (u64)map_addr;
    g_nroSize = nro_size;

    svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PostLoadDll, g_nroAddr, nro_size);

    // write exit detection
    strcpy(g_nextArgv, EXIT_DETECTION_STR);

    if (R_FAILED(rc = restoreMainThreadAffinity()))
        diagAbortWithResult(rc);

    // jump to trampoline.s
    nroEntrypointTrampoline(&entries[0], -1, g_nroAddr);
}

int main(int argc, char **argv) {
    memcpy(g_savedTls, (const u8*)armGetTls() + 0x100, 0x100);
    getIsApplication();
    getIsAutomaticGameplayRecording();
    setupHbHeap();
    getOwnProcessHandle();
    getCodeMemoryCapability();
    loadNro();
}

// libnx stuff
u32 __nx_applet_type = AppletType_Application;
// Minimize fs resource usage
u32 __nx_fs_num_sessions = 1;
// these aren't needed, keeping them as someone will eventually
// copy this code, use fsdev, and not add back these vars.
u32 __nx_fsdev_direntry_cache_size = 1;
bool __nx_fsdev_support_cwd = false;
// enable to always exit to homemenu, dbi does this.
// u32 __nx_applet_exit_mode = 1;
// u32 __nx_applet_exit_mode = 0;

void __libnx_initheap(void) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = NULL;
    fake_heap_end   = NULL;
}

void __appInit(void) {
    Result rc;

    // Detect Atmosphère early on. This is required for hosversion logic.
    // In the future, this check will be replaced by detectMesosphere().
    Handle dummy;
    rc = svcConnectToNamedPort(&dummy, "ams");
    u32 ams_flag = (R_VALUE(rc) != KERNELRESULT(NotFound)) ? BIT(31) : 0;
    if (R_SUCCEEDED(rc))
        svcCloseHandle(dummy);

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, LibnxError_InitFail_SM));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(ams_flag | MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, LibnxError_InitFail_FS));

    smExit(); // Close SM as we don't need it anymore.
}

void __appExit(void) {

}

// exit() effectively never gets called, so let's stub it out.
void __wrap_exit(void) {
    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 39));
}

// stub alloc calls as they're not used (saves 4KiB).
void* __libnx_alloc(size_t size) {
    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 40));
}

void* __libnx_aligned_alloc(size_t alignment, size_t size) {
    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 41));
}

void __libnx_free(void* p) {
    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 43));
}
