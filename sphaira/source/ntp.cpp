#include "ntp.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstdio>
#include <sys/time.h>
#include <unistd.h>

extern "C" void __libnx_init_time(void);

namespace sphaira::ntp {
namespace {

// pool.ntp.org hands out a different server per lookup, so a single name covers
// redundancy without us keeping a server list. The fallbacks are only used when
// the pool cannot be resolved at all (some dns setups block it).
constexpr const char* SERVERS[] = {
    "pool.ntp.org",
    "time.cloudflare.com",
    "time.google.com",
};

constexpr u16 NTP_PORT = 123;
// ntp counts seconds from 1900, posix from 1970.
constexpr u64 NTP_TO_POSIX_EPOCH = 2208988800ULL;
// sanity window for a reply: anything outside it is a malformed or spoofed
// packet rather than a genuinely wrong clock. 2020-01-01 .. 2100-01-01.
constexpr u64 PLAUSIBLE_MIN = 1577836800ULL;
constexpr u64 PLAUSIBLE_MAX = 4102444800ULL;
// don't touch the clock for sub-second jitter; the round trip alone is worth
// more than that.
constexpr s64 MIN_CORRECTION_SECONDS = 2;

constexpr int RECV_TIMEOUT_SECONDS = 5;
// how often the idle thread re-checks for a connection.
constexpr u64 POLL_INTERVAL_NS = 10ULL * 1000000000ULL;
// once synced, re-sync this often to catch rtc drift on long sessions.
constexpr u64 RESYNC_INTERVAL_NS = 6ULL * 3600ULL * 1000000000ULL;
// after a failure, wait longer than the idle poll before hammering dns again.
constexpr u64 RETRY_INTERVAL_NS = 120ULL * 1000000000ULL;

// SNTPv4 packet, RFC 4330. All fields are big endian on the wire.
struct NtpPacket {
    u8 li_vn_mode;
    u8 stratum;
    u8 poll;
    u8 precision;
    u32 root_delay;
    u32 root_dispersion;
    u32 reference_id;
    u32 reference_timestamp_sec;
    u32 reference_timestamp_frac;
    u32 origin_timestamp_sec;
    u32 origin_timestamp_frac;
    u32 receive_timestamp_sec;
    u32 receive_timestamp_frac;
    u32 transmit_timestamp_sec;
    u32 transmit_timestamp_frac;
};
static_assert(sizeof(NtpPacket) == 48);

Thread g_thread{};
std::atomic_bool g_thread_running{};
std::atomic_bool g_stop{};
UEvent g_wake_event{};

auto HasInternet() -> bool {
    // "do we have an ip" is what the rest of the app calls connected -- see the
    // status header in MenuBase. nifmGetInternetConnectionStatus is stricter:
    // it only reports Connected once the os has run its own reachability probe
    // against nintendo's servers, which never happens on a console that is kept
    // off them, so gating on it meant this never ran at all.
    u32 ip{};
    if (R_FAILED(nifmGetCurrentIpAddress(&ip))) {
        return false;
    }
    return ip != 0;
}

// asks one server for the time. out is a posix timestamp.
Result QueryServer(const char* host, u64* out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result{};
    if (getaddrinfo(host, "123", &hints, &result) || !result) {
        R_THROW(Result_NtpResolveFailed);
    }
    ON_SCOPE_EXIT(freeaddrinfo(result));

    const auto fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        R_THROW(Result_NtpSocketFailed);
    }
    ON_SCOPE_EXIT(close(fd));

    // bounded so a silently dropped packet cannot wedge the thread.
    timeval tv{};
    tv.tv_sec = RECV_TIMEOUT_SECONDS;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // li = 0, vn = 4, mode = 3 (client).
    NtpPacket packet{};
    packet.li_vn_mode = 0x23;

    if (sendto(fd, &packet, sizeof(packet), 0, result->ai_addr, result->ai_addrlen) != sizeof(packet)) {
        R_THROW(Result_NtpSendFailed);
    }

    NtpPacket reply{};
    if (recv(fd, &reply, sizeof(reply), 0) != sizeof(reply)) {
        R_THROW(Result_NtpRecvFailed);
    }

    // mode 4 is "server"; stratum 0 is a kiss-o'-death packet, not a time.
    const auto mode = reply.li_vn_mode & 0x7;
    R_UNLESS(mode == 4, Result_NtpBadReply);
    R_UNLESS(reply.stratum > 0 && reply.stratum < 16, Result_NtpBadReply);

    const u64 seconds_1900 = ntohl(reply.transmit_timestamp_sec);
    R_UNLESS(seconds_1900 > NTP_TO_POSIX_EPOCH, Result_NtpBadReply);

    const auto posix = seconds_1900 - NTP_TO_POSIX_EPOCH;
    R_UNLESS(posix >= PLAUSIBLE_MIN && posix <= PLAUSIBLE_MAX, Result_NtpBadReply);

    *out = posix;
    R_SUCCEED();
}

// ISystemClock is a subservice of time:s; libnx's own session is time:u, whose
// clocks are not writable, so the write path opens its own.
Result GetClockSession(Service* srv, u32 cmd_id, Service* out) {
    return serviceDispatch(srv, cmd_id,
        .out_num_objects = 1,
        .out_objects = out,
    );
}

Result SetClockTime(Service* clock, u64 timestamp) {
    return serviceDispatchIn(clock, 1, timestamp);
}

// The system setting "Synchronise Clock via Internet" is, under the hood,
// StandardUserSystemClockAutomaticCorrectionEnabled. While it is on, hos owns
// the user clock and keeps re-deriving it from the network clock, so a direct
// write to the user clock below is silently reverted -- which is exactly why
// setting the time from here did nothing until the setting was toggled by hand.
// The system-settings "set clock manually" flow turns this flag off first; we
// mirror that so our write actually sticks, regardless of the setting's state.
// This changes the live flag only (time:s cmd 101); it does not rewrite the
// persisted system setting, and hos re-reads it on the next boot where this
// runs again.
Result DisableAutomaticCorrection(Service* time_srv) {
    const u8 enabled = 0;
    return serviceDispatchIn(time_srv, 101, enabled);
}

Result SetSystemTime(u64 timestamp) {
    Service time_srv{};
    // time:s is the settings-side manager and is the one that hands out
    // writable clocks. time:su exists from 9.0.0 and works the same way.
    auto rc = smGetService(&time_srv, "time:s");
    if (R_FAILED(rc)) {
        rc = smGetService(&time_srv, "time:su");
    }
    if (R_FAILED(rc)) {
        // a dead end rather than a transient: record it where it can be read
        // even with logging switched off, otherwise the feature just looks
        // like it does nothing.
        log_write_error("ntp: cannot open time:s / time:su (0x%08X); the clock cannot be set from here", R_VALUE(rc));
        R_THROW(rc);
    }
    ON_SCOPE_EXIT(serviceClose(&time_srv));

    // must precede the clock writes: see DisableAutomaticCorrection. Best effort
    // -- on a console that already has the setting off this is a no-op, so a
    // failure here only matters as a permission signal, not a hard stop.
    if (const auto rc = DisableAutomaticCorrection(&time_srv); R_FAILED(rc)) {
        log_write("[NTP] could not disable automatic clock correction (0x%08X); the write may not stick\n", R_VALUE(rc));
    }

    // 0 = GetStandardUserSystemClock, 1 = GetStandardNetworkSystemClock. The
    // user clock is what the ui and posix time() read; the network clock is
    // what the os treats as authoritative, so both are set to keep them from
    // drifting apart and having hos "correct" ours back.
    Result user_rc = 0;
    Result last_rc = 0;
    bool user_set = false;
    for (const u32 cmd_id : {0u, 1u}) {
        Service clock{};
        if (const auto get_rc = GetClockSession(&time_srv, cmd_id, &clock); R_FAILED(get_rc)) {
            if (cmd_id == 0u) {
                user_rc = get_rc;
            }
            last_rc = get_rc;
            continue;
        }
        ON_SCOPE_EXIT(serviceClose(&clock));
        if (const auto set_rc = SetClockTime(&clock, timestamp); R_FAILED(set_rc)) {
            if (cmd_id == 0u) {
                user_rc = set_rc;
            }
            last_rc = set_rc;
            continue;
        }
        if (cmd_id == 0u) {
            user_set = true;
        }
    }

    if (!user_set) {
        log_write_error("ntp: time service refused the clock write (0x%08X)", R_VALUE(user_rc ? user_rc : last_rc));
    }
    R_UNLESS(user_set, user_rc ? user_rc : (last_rc ? last_rc : Result_NtpSetTimeFailed));
    R_SUCCEED();
}

Result RunSync() {
    R_UNLESS(HasInternet(), Result_NtpNoConnection);

    u64 network_time{};
    Result rc = Result_NtpResolveFailed;
    for (const auto server : SERVERS) {
        rc = QueryServer(server, &network_time);
        if (R_SUCCEEDED(rc)) {
            break;
        }
        log_write("[NTP] %s failed: 0x%08X\n", server, R_VALUE(rc));
    }
    R_TRY(rc);

    u64 current_time{};
    R_TRY(timeGetCurrentTime(TimeType_UserSystemClock, &current_time));

    const auto offset = static_cast<s64>(network_time) - static_cast<s64>(current_time);
    if (offset > -MIN_CORRECTION_SECONDS && offset < MIN_CORRECTION_SECONDS) {
        log_write("[NTP] clock already accurate (%+lld s)\n", (long long)offset);
        R_SUCCEED();
    }

    R_TRY(SetSystemTime(network_time));
    evman::push(evman::FunctionalEventData{[]() {
        __libnx_init_time();
        App::Notify("Clock synced"_i18n);
    }}, false);
    log_write("[NTP] clock corrected by %+lld seconds\n", (long long)offset);
    R_SUCCEED();
}

void ThreadFunc(void*) {
    // start the first synchronization attempt immediately upon launch.
    u64 wait_ns = 0;

    while (!g_stop) {
        // waitSingle on the wake event so Stop() interrupts the sleep instead
        // of leaving the app hanging for up to the poll interval on exit.
        s32 idx;
        const auto waiter = waiterForUEvent(&g_wake_event);
        waitObjects(&idx, &waiter, 1, wait_ns);
        if (g_stop) {
            break;
        }

        if (!App::GetNtpEnable()) {
            wait_ns = POLL_INTERVAL_NS;
            continue;
        }

        const auto rc = RunSync();
        // no connection yet is the normal case on boot, so keep the fast poll
        // for it and back off only for real failures.
        wait_ns = R_SUCCEEDED(rc) ? RESYNC_INTERVAL_NS
            : rc == Result_NtpNoConnection ? POLL_INTERVAL_NS : RETRY_INTERVAL_NS;
    }
}

} // namespace

void Start() {
    if (g_thread_running) {
        ueventSignal(&g_wake_event);
        return;
    }

    g_stop = false;
    ueventCreate(&g_wake_event, true);

    // lowest priority homebrew may create, no core affinity: this must never
    // compete with the ui or an install.
    if (R_FAILED(threadCreate(&g_thread, ThreadFunc, nullptr, nullptr, 1024 * 32, PRIO_PREEMPTIVE, -2))) {
        log_write("[NTP] failed to create sync thread\n");
        return;
    }
    if (R_FAILED(threadStart(&g_thread))) {
        threadClose(&g_thread);
        log_write("[NTP] failed to start sync thread\n");
        return;
    }
    g_thread_running = true;
}

void Stop() {
    if (!g_thread_running) {
        return;
    }
    g_stop = true;
    ueventSignal(&g_wake_event);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_thread_running = false;
}

} // namespace sphaira::ntp
