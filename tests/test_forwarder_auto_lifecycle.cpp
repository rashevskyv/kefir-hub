// Host unit test for forwarder_auto thread lifecycle (sphaira/source/forwarder_auto_install.cpp)
// Verifies that threadWaitForExit and threadClose are strictly guaranteed even when worker exits early.
//
//     g++ -std=c++20 -Wall -Wextra -Werror tests/test_forwarder_auto_lifecycle.cpp -o /tmp/t && /tmp/t

#include "forwarder_auto_plan.hpp"

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <string>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

namespace {

struct MockForwarderAutoLifecycle {
    std::atomic_bool g_stop_requested{false};
    std::atomic_bool g_thread_active{false};
    std::atomic_bool g_thread_created{false};

    int mock_wait_calls{0};
    int mock_close_calls{0};
    int mock_create_calls{0};

    void Reset() {
        g_stop_requested = false;
        g_thread_active = false;
        g_thread_created = false;
        mock_wait_calls = 0;
        mock_close_calls = 0;
        mock_create_calls = 0;
    }

    void WorkerFunc(bool is_application) {
        g_thread_active = true;
        if (is_application) {
            // Worker exits almost immediately in Application mode
            g_thread_active = false;
            return;
        }
        // Simulate normal work
        g_thread_active = false;
    }

    bool StartCheck() {
        if (g_thread_created.exchange(true)) {
            return false;
        }

        g_stop_requested = false;
        g_thread_active = true;
        mock_create_calls++;
        return true;
    }

    void StopCheck() {
        if (g_thread_created.exchange(false)) {
            g_stop_requested = true;
            mock_wait_calls++;
            mock_close_calls++;
            g_thread_active = false;
        }
    }
};

int RunTests() {
    MockForwarderAutoLifecycle lc;

    // Test 1: Application mode still creates the thread (cleanup/install may be needed)
    {
        lc.Reset();
        CHECK(lc.StartCheck());
        CHECK(lc.mock_create_calls == 1);
        CHECK(lc.g_thread_created);

        lc.StopCheck();
        CHECK(lc.mock_close_calls == 1);
        CHECK(lc.mock_wait_calls == 1);
    }

    // Test 2: Worker exits BEFORE StopCheck() is called -> threadClose must STILL be called!
    {
        lc.Reset();
        CHECK(lc.StartCheck());
        CHECK(lc.mock_create_calls == 1);
        CHECK(lc.g_thread_created);

        // Worker executes and finishes before user exits
        lc.WorkerFunc(false);
        CHECK(!lc.g_thread_active);
        CHECK(lc.g_thread_created); // Flag guarding cleanup remains true!

        // Now user launches NRO / exits app: StopCheck must close thread!
        lc.StopCheck();
        CHECK(lc.mock_wait_calls == 1);
        CHECK(lc.mock_close_calls == 1);
        CHECK(!lc.g_thread_created);
    }

    // Test 3: Multiple StartCheck calls are deduplicated
    {
        lc.Reset();
        CHECK(lc.StartCheck());
        CHECK(!lc.StartCheck()); // Deduplicated
        CHECK(lc.mock_create_calls == 1);

        lc.StopCheck();
        CHECK(lc.mock_close_calls == 1);

        lc.StopCheck(); // Redundant stop is safe no-op
        CHECK(lc.mock_close_calls == 1);
    }

    // Test 4: IsOldHomebrewTitle / IsStaleOwnForwarder / ClassifyLaunch
    {
        using sphaira::forwarder_auto::IsOldHomebrewTitle;
        using sphaira::forwarder_auto::IsStaleOwnForwarder;
        using sphaira::forwarder_auto::ClassifyLaunch;
        using sphaira::forwarder_auto::LaunchSource;

        const uint64_t kefirhub_tid = 0x0500000000123000ULL;
        const uint64_t stale_tid = 0x0500000000ABCDEFULL;

        CHECK(IsOldHomebrewTitle("Anything", 0x03DB1280BD84000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Anything", 0x03DB12780BD84000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Custom Name", 0x050000000000100DULL, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("Custom Name", 0x010000000000100DULL, kefirhub_tid));

        CHECK(IsOldHomebrewTitle("Homebrew Menu", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("homebrew menu v2.0", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Homebrew Launcher", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("HBM", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("HBL", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("hbmenu", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("hblauncher", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("nx-hbmenu", 0x0100111222333000ULL, kefirhub_tid));

        CHECK(!IsOldHomebrewTitle("Kefir Hub", kefirhub_tid, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("KefirHub", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("Sphaira", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("Super Mario Odyssey", 0x0100000000010000ULL, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("RetroArch", 0x0100000000020000ULL, kefirhub_tid));

        CHECK(IsStaleOwnForwarder("Kefir Hub", stale_tid, kefirhub_tid));
        CHECK(IsStaleOwnForwarder("Sphaira", stale_tid, kefirhub_tid));
        CHECK(!IsStaleOwnForwarder("Kefir Hub", kefirhub_tid, kefirhub_tid));
        CHECK(!IsStaleOwnForwarder("Kefir Hub", 0x0100000000010000ULL, kefirhub_tid));
        CHECK(!IsStaleOwnForwarder("Homebrew Menu", stale_tid, kefirhub_tid));

        CHECK(ClassifyLaunch(false, 0, kefirhub_tid, "") == LaunchSource::Album);
        CHECK(ClassifyLaunch(true, kefirhub_tid, kefirhub_tid, "Kefir Hub") == LaunchSource::NewForwarder);
        CHECK(ClassifyLaunch(true, stale_tid, kefirhub_tid, "Kefir Hub") == LaunchSource::StaleOwn);
        CHECK(ClassifyLaunch(true, 0x03DB1280BD84000ULL, kefirhub_tid, "Homebrew Menu") == LaunchSource::OldForwarder);
        CHECK(ClassifyLaunch(true, 0x0100AABBCCDDE000ULL, kefirhub_tid, "Some App") == LaunchSource::Album);
    }

    // Test 5: launch-source plan — never delete the forwarder we launched from
    {
        using sphaira::forwarder_auto::Decide;
        using sphaira::forwarder_auto::LaunchSource;
        using sphaira::forwarder_auto::Notice;

        auto from_new = Decide(LaunchSource::NewForwarder, true, true);
        CHECK(from_new.delete_old);
        CHECK(!from_new.install_new);
        CHECK(from_new.notice == Notice::OldWillBeRemoved);

        auto from_new_clean = Decide(LaunchSource::NewForwarder, true, false);
        CHECK(!from_new_clean.delete_old);
        CHECK(!from_new_clean.install_new);
        CHECK(from_new_clean.notice == Notice::None);

        auto from_old = Decide(LaunchSource::OldForwarder, false, true);
        CHECK(from_old.install_new);
        CHECK(!from_old.delete_old);
        CHECK(from_old.notice == Notice::UseNewNextTime);

        auto from_old_new_ready = Decide(LaunchSource::OldForwarder, true, true);
        CHECK(!from_old_new_ready.install_new);
        CHECK(!from_old_new_ready.delete_old);
        CHECK(from_old_new_ready.notice == Notice::UseNewNextTime);

        auto from_album = Decide(LaunchSource::Album, false, true);
        CHECK(from_album.install_new);
        CHECK(from_album.delete_old);
        CHECK(from_album.notice == Notice::PreferHomeIcon);

        auto from_album_clean = Decide(LaunchSource::Album, true, false);
        CHECK(!from_album_clean.install_new);
        CHECK(!from_album_clean.delete_old);
        CHECK(from_album_clean.notice == Notice::None);

        auto from_stale = Decide(LaunchSource::StaleOwn, false, false);
        CHECK(from_stale.install_new);
        CHECK(!from_stale.delete_old);
        CHECK(from_stale.notice == Notice::None);

        auto from_stale_ready = Decide(LaunchSource::StaleOwn, true, false);
        CHECK(!from_stale_ready.install_new);
        CHECK(!from_stale_ready.delete_old);
        CHECK(from_stale_ready.notice == Notice::None);
    }

    return 0;
}

} // namespace

int main() {
    if (RunTests() != 0) {
        return 1;
    }
    std::printf("ok  forwarder_auto_lifecycle: %d checks passed\n", g_checks);
    return 0;
}
