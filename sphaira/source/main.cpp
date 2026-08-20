#include <switch.h>
#include <memory>
#include <cstdint>
#include <cstddef>
#include "app.hpp"
#include "log.hpp"
#include "net.hpp"
#include "web.hpp"

int main(int argc, char** argv) {
    if (!argc || !argv) {
        return 1;
    }

    auto app = std::make_unique<sphaira::App>(argv[0]);
    app->Loop();
    return 0;
}

extern "C" {

extern char* fake_heap_start;
extern char* fake_heap_end;

extern size_t __nx_heap_size;

void __libnx_initheap(void) {
    if (envHasHeapOverride()) {
        void* override_addr_ptr = envGetHeapOverrideAddr();
        size_t override_size = envGetHeapOverrideSize();
        uintptr_t override_addr = (uintptr_t)override_addr_ptr;

        if (override_size == 0 || override_addr == 0 || (override_addr + override_size) <= override_addr) {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
        }

        uintptr_t override_end = override_addr + override_size;
        uintptr_t cur_addr = override_addr;

        uintptr_t best_start = 0;
        size_t best_size = 0;
        uintptr_t current_usable_start = 0;
        size_t current_usable_size = 0;

        while (cur_addr < override_end) {
            MemoryInfo info{};
            u32 pageinfo = 0;
            Result rc = svcQueryMemory(&info, &pageinfo, cur_addr);
            if (R_FAILED(rc)) {
                diagAbortWithResult(rc);
            }

            if (info.size == 0 || (info.addr + info.size) <= info.addr || cur_addr < info.addr || cur_addr >= (info.addr + info.size)) {
                diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
            }

            uintptr_t mem_end = (uintptr_t)(info.addr + info.size);
            uintptr_t block_start = ((uintptr_t)info.addr < override_addr) ? override_addr : (uintptr_t)info.addr;
            uintptr_t block_end = (mem_end > override_end) ? override_end : mem_end;
            size_t block_size = block_end - block_start;

            bool is_usable = ((info.type & MemState_Type) == MemType_Heap) &&
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
                diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
            }
            cur_addr = mem_end;
        }

        if (current_usable_size > best_size) {
            best_size = current_usable_size;
            best_start = current_usable_start;
        }

        if (best_size == 0) {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
        }

        fake_heap_start = (char*)best_start;
        fake_heap_end = (char*)(best_start + best_size);
    } else {
        size_t size = __nx_heap_size;

        if (size == 0) {
            u64 total_mem = 0, used_mem = 0;
            svcGetInfo(&total_mem, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
            svcGetInfo(&used_mem, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

            size = 0x20000000;
            if (used_mem + 0x200000 < total_mem) {
                size = (total_mem - used_mem - 0x200000) & ~0x1FFFFFULL;
                if (size == 0) {
                    size = 0x20000000;
                }
            }
        }

        void* addr = nullptr;
        Result rc = svcSetHeapSize(&addr, size);
        if (R_FAILED(rc)) {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
        }

        fake_heap_start = (char*)addr;
        fake_heap_end = (char*)((uintptr_t)addr + size);
    }
}

void userAppInit(void) {
    sphaira::App::SetBoostMode(true);

    // https://github.com/mtheall/ftpd/blob/e27898f0c3101522311f330e82a324861e0e3f7e/source/switch/init.c#L31
    const SocketInitConfig socket_config_application = {
        .tcp_tx_buf_size = 1024 * 64,
        .tcp_rx_buf_size = 1024 * 64,
        .tcp_tx_buf_max_size = 1024 * 1024 * 4,
        .tcp_rx_buf_max_size = 1024 * 1024 * 4,
        .udp_tx_buf_size = 0x2400, // same as default
        .udp_rx_buf_size = 0xA500, // same as default
        .sb_efficiency = 8,
        .num_bsd_sessions = 3,
        .bsd_service_type = BsdServiceType_Auto,
    };

    const SocketInitConfig socket_config_applet = {
        .tcp_tx_buf_size = 1024 * 32,
        .tcp_rx_buf_size = 1024 * 64,
        .tcp_tx_buf_max_size = 1024 * 256,
        .tcp_rx_buf_max_size = 1024 * 256,
        .udp_tx_buf_size = 0x2400, // same as default
        .udp_rx_buf_size = 0xA500, // same as default
        .sb_efficiency = 4,
        .num_bsd_sessions = 3,
        .bsd_service_type = BsdServiceType_Auto,
    };

    const auto is_application = sphaira::App::IsApplication();

    const auto socket_config = is_application ? socket_config_application : socket_config_applet;

    Result rc;
    if (R_FAILED(rc = appletLockExit()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = socketInitialize(&socket_config)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = plInitialize(PlServiceType_User)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = psmInitialize()))
        diagAbortWithResult(rc);
    // nifm:a is what lets sphaira clear airplane mode when something that needs
    // the network is started offline (see net::TryConnect). it is not granted
    // everywhere sphaira runs, so nifm:u stays the fallback -- every other nifm
    // call we make is available on both.
    if (R_FAILED(nifmInitialize(NifmServiceType_Admin))) {
        if (R_FAILED(rc = nifmInitialize(NifmServiceType_User)))
            diagAbortWithResult(rc);
    }
    if (R_FAILED(rc = accountInitialize(is_application ? AccountServiceType_Application : AccountServiceType_System)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = setInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = hidsysInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = ncmInitialize()))
        diagAbortWithResult(rc);

    // it doesn't matter if this fails.
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
}

void userAppExit(void) {
    sphaira::WebShareStop();
    sphaira::net::Exit();

    ncmExit();
    hidsysExit();
    setExit();
    accountExit();
    nifmExit();
    psmExit();
    plExit();
    socketExit();
    // NOTE (DMC): prevents exfat / fat32 corruption on all SD cards.
    fsdevCommitDevice("sdmc");
    if (auto fs = fsdevGetDeviceFileSystem("sdmc")) {
        fsFsCommit(fs);
    }

    sphaira::App::SetBoostMode(false);
    appletUnlockExit();
}

} // extern "C"
