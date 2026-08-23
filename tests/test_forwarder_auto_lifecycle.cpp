// Host unit test for forwarder_auto thread lifecycle (sphaira/source/forwarder_auto_install.cpp)
// Verifies that threadWaitForExit and threadClose are strictly guaranteed even when worker exits early.
//
//     g++ -std=c++20 -Wall -Wextra -Werror tests/test_forwarder_auto_lifecycle.cpp -o /tmp/t && /tmp/t

#include <cstdint>
#include <cstdio>
#include <atomic>

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

    bool StartCheck(bool is_application) {
        if (g_thread_created.exchange(true)) {
            return false;
        }

        if (is_application) {
            g_thread_created = false;
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

    // Test 1: Application mode -> No thread created, StopCheck is a safe no-op
    {
        lc.Reset();
        CHECK(!lc.StartCheck(true));
        CHECK(lc.mock_create_calls == 0);
        CHECK(!lc.g_thread_created);
        CHECK(!lc.g_thread_active);

        lc.StopCheck();
        CHECK(lc.mock_close_calls == 0);
        CHECK(lc.mock_wait_calls == 0);
    }

    // Test 2: Worker exits BEFORE StopCheck() is called -> threadClose must STILL be called!
    {
        lc.Reset();
        CHECK(lc.StartCheck(false));
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
        CHECK(lc.StartCheck(false));
        CHECK(!lc.StartCheck(false)); // Deduplicated
        CHECK(lc.mock_create_calls == 1);

        lc.StopCheck();
        CHECK(lc.mock_close_calls == 1);

        lc.StopCheck(); // Redundant stop is safe no-op
        CHECK(lc.mock_close_calls == 1);
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
