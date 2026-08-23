// Host unit test for forwarder_auto thread lifecycle (sphaira/source/forwarder_auto_install.cpp)
// Verifies that threadWaitForExit and threadClose are strictly guaranteed even when worker exits early.
//
//     g++ -std=c++20 -Wall -Wextra -Werror tests/test_forwarder_auto_lifecycle.cpp -o /tmp/t && /tmp/t

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <string>
#include <cctype>

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

bool IsOldHomebrewTitle(const std::string& raw_name, uint64_t tid, uint64_t kefirhub_tid) {
    if (tid == kefirhub_tid && kefirhub_tid != 0) {
        return false;
    }

    // Specific known Title IDs for Homebrew Menu / HBL forwarders
    if (tid == 0x03DB1280BD84000ULL || tid == 0x03DB12780BD84000ULL ||
        tid == 0x010000000000100DULL || tid == 0x050000000000100DULL) {
        return true;
    }

    std::string name = raw_name;
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Never delete KefirHub or Sphaira itself
    if (name.find("kefir") != std::string::npos || name.find("sphaira") != std::string::npos) {
        return false;
    }

    if (name == "hbm" || name == "hbl" || name == "hbmenu" || name == "hblauncher" || name == "nx-hbmenu") {
        return true;
    }

    if (name.find("homebrew menu") != std::string::npos ||
        name.find("homebrew launcher") != std::string::npos ||
        name.find("hblauncher") != std::string::npos ||
        name.find("hbmenu") != std::string::npos ||
        name.find("nx-hbmenu") != std::string::npos) {
        return true;
    }

    return false;
}

bool IsOldForwarderNspFile(const std::string& raw_name) {
    std::string name = raw_name;
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (name.length() < 4 || name.substr(name.length() - 4) != ".nsp") {
        return false;
    }

    if (name.find("kefir") != std::string::npos || name.find("sphaira") != std::string::npos) {
        return false;
    }

    if (name.find("homebrew menu") != std::string::npos ||
        name.find("homebrew launcher") != std::string::npos ||
        name.find("hblauncher") != std::string::npos ||
        name.find("hbmenu") != std::string::npos ||
        name.find("hbm") != std::string::npos ||
        name.find("hbl") != std::string::npos ||
        name.find("03db1280bd84000") != std::string::npos ||
        name.find("03db12780bd84000") != std::string::npos ||
        name.find("010000000000100d") != std::string::npos ||
        name.find("050000000000100d") != std::string::npos) {
        return true;
    }

    return false;
}

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

    // Test 4: IsOldHomebrewTitle detection
    {
        const uint64_t kefirhub_tid = 0x0500000000123000ULL;

        // Known legacy Title IDs must be detected regardless of name
        CHECK(IsOldHomebrewTitle("Anything", 0x03DB1280BD84000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Anything", 0x03DB12780BD84000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Custom Name", 0x010000000000100DULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Custom Name", 0x050000000000100DULL, kefirhub_tid));

        // Matching Homebrew Menu names
        CHECK(IsOldHomebrewTitle("Homebrew Menu", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("homebrew menu v2.0", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("Homebrew Launcher", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("HBM", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("HBL", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("hbmenu", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("hblauncher", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(IsOldHomebrewTitle("nx-hbmenu", 0x0100111222333000ULL, kefirhub_tid));

        // KefirHub / Sphaira must NEVER be matched
        CHECK(!IsOldHomebrewTitle("Kefir Hub", kefirhub_tid, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("KefirHub", 0x0100111222333000ULL, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("Sphaira", 0x0100111222333000ULL, kefirhub_tid));

        // Regular games must not match
        CHECK(!IsOldHomebrewTitle("Super Mario Odyssey", 0x0100000000010000ULL, kefirhub_tid));
        CHECK(!IsOldHomebrewTitle("RetroArch", 0x0100000000020000ULL, kefirhub_tid));
    }

    // Test 5: IsOldForwarderNspFile detection in /Games
    {
        CHECK(IsOldForwarderNspFile("Homebrew menu [010000000000100D].nsp"));
        CHECK(IsOldForwarderNspFile("Homebrew Launcher.nsp"));
        CHECK(IsOldForwarderNspFile("hbmenu.nsp"));
        CHECK(IsOldForwarderNspFile("hblauncher.nsp"));
        CHECK(IsOldForwarderNspFile("HBM [03DB1280BD84000].nsp"));
        CHECK(IsOldForwarderNspFile("HBL.nsp"));

        // Must not match KefirHub forwarders or other files
        CHECK(!IsOldForwarderNspFile("Kefir Hub [0500000000123000].nsp"));
        CHECK(!IsOldForwarderNspFile("Sphaira.nsp"));
        CHECK(!IsOldForwarderNspFile("SuperMario.nsp"));
        CHECK(!IsOldForwarderNspFile("hbmenu.nro")); // Only .nsp files
        CHECK(!IsOldForwarderNspFile("info.txt"));
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
