// The installer callbacks every MTP install path shares (Games drive install folders,
// the microSD root drop). The drives themselves live in haze_game_proxy*.cpp / haze_fs_proxy.cpp.
#include "haze/haze_internal.hpp"

namespace sphaira::haze {

#if ENABLE_NETWORK_INSTALL
void InitInstallMode(OnInstallStart on_start, OnInstallWrite on_write, OnInstallClose on_close) {
    SCOPED_MUTEX(&g_shared_data.mutex);
    g_shared_data.on_start = on_start;
    g_shared_data.on_write = on_write;
    g_shared_data.on_close = on_close;
    g_shared_data.enabled = true;
}

void DisableInstallMode() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    g_shared_data.enabled = false;
}
#endif

} // namespace sphaira::haze
