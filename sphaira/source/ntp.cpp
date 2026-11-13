#include "ntp.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "fs.hpp"
#include "hekate_clock.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "nextendo_apply.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <minIni.h>
#include <optional>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstdio>
#include <string>
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
std::atomic<s64> g_display_offset{0};

void ReportSyncStage(std::string stage) {
    log_write("[NTP] %s\n", stage.c_str());
}

void ReportSyncFailure(std::string stage, Result rc) {
    char result[16];
    std::snprintf(result, sizeof(result), "0x%08X", R_VALUE(rc));
    ReportSyncStage(std::move(stage) + " failed " + result);
}

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
    ReportSyncStage(std::string("DNS ") + host);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result{};
    if (getaddrinfo(host, "123", &hints, &result) || !result) {
        ReportSyncStage(std::string("DNS ") + host + " failed");
        R_THROW(Result_NtpResolveFailed);
    }
    ON_SCOPE_EXIT(freeaddrinfo(result));

    ReportSyncStage(std::string("opening UDP ") + host);
    const auto fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        ReportSyncStage(std::string("opening UDP ") + host + " failed");
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

    ReportSyncStage(std::string("sending request to ") + host);
    if (sendto(fd, &packet, sizeof(packet), 0, result->ai_addr, result->ai_addrlen) != sizeof(packet)) {
        ReportSyncStage(std::string("sending request to ") + host + " failed");
        R_THROW(Result_NtpSendFailed);
    }

    ReportSyncStage(std::string("waiting for ") + host);
    NtpPacket reply{};
    if (recv(fd, &reply, sizeof(reply), 0) != sizeof(reply)) {
        ReportSyncStage(std::string("waiting for ") + host + " failed");
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
    ReportSyncStage(std::string("valid reply from ") + host);
    R_SUCCEED();
}

// ISystemClock is a subservice of the privileged time services; libnx's own
// session is time:u, whose clocks are not writable, so the write path opens its own.
Result GetClockSession(Service* srv, u32 cmd_id, Service* out) {
    return serviceDispatch(srv, cmd_id,
        .out_num_objects = 1,
        .out_objects = out,
    );
}

Result SetClockTime(Service* clock, u64 timestamp) {
    return serviceDispatchIn(clock, 1, timestamp);
}

Result SetSystemTimeWithService(const char* service_name, u64 timestamp) {
    const std::string service{service_name};
    Service time_srv{};
    ReportSyncStage("opening " + service);
    const auto open_rc = smGetService(&time_srv, service_name);
    if (R_FAILED(open_rc)) {
        ReportSyncFailure("opening " + service, open_rc);
        R_THROW(open_rc);
    }
    ON_SCOPE_EXIT(serviceClose(&time_srv));

    // 0 = GetStandardUserSystemClock, 1 = GetStandardNetworkSystemClock. The
    // user clock is what the ui and posix time() read; the network clock is
    // what the os treats as authoritative, so both are set to keep them from
    // drifting apart and having hos "correct" ours back.
    Result user_rc = 0;
    Result last_rc = 0;
    bool user_set = false;
    for (const u32 cmd_id : {0u, 1u}) {
        const char* clock_name = cmd_id == 0u ? "User Clock" : "Network Clock";
        Service clock{};
        ReportSyncStage(service + " opening " + clock_name);
        const auto get_rc = GetClockSession(&time_srv, cmd_id, &clock);
        if (R_FAILED(get_rc)) {
            ReportSyncFailure(service + " opening " + clock_name, get_rc);
            if (cmd_id == 0u) {
                user_rc = get_rc;
            }
            last_rc = get_rc;
            continue;
        }
        ON_SCOPE_EXIT(serviceClose(&clock));
        ReportSyncStage(service + " setting " + clock_name);
        const auto set_rc = SetClockTime(&clock, timestamp);
        if (R_FAILED(set_rc)) {
            ReportSyncFailure(service + " setting " + clock_name, set_rc);
            if (cmd_id == 0u) {
                user_rc = set_rc;
            }
            last_rc = set_rc;
            continue;
        }
        if (cmd_id == 0u) {
            user_set = true;
        }
        ReportSyncStage(service + " updated " + clock_name);
    }

    R_UNLESS(user_set, user_rc ? user_rc : (last_rc ? last_rc : Result_NtpSetTimeFailed));
    R_SUCCEED();
}

Result SetSystemTime(u64 timestamp, bool& out_used_fallback) {
    out_used_fallback = false;

    // time:su is the supported writable service for user-mode system tools on
    // 9.0.0+. time:s remains a fallback for older or custom HOS setups.
    ReportSyncStage("trying time:su");
    const auto system_user_rc = SetSystemTimeWithService("time:su", timestamp);
    if (R_SUCCEEDED(system_user_rc)) {
        R_SUCCEED();
    }

    ReportSyncFailure("time:su user-clock update", system_user_rc);
    ReportSyncStage("trying time:s fallback");
    const auto system_rc = SetSystemTimeWithService("time:s", timestamp);
    if (R_SUCCEEDED(system_rc)) {
        R_SUCCEED();
    }

    ReportSyncFailure("time:s user-clock update", system_rc);

    u64 user_clock_after = 0;
    ReportSyncStage("re-reading User Clock");
    const auto re_read_rc = timeGetCurrentTime(TimeType_UserSystemClock, &user_clock_after);
    if (R_SUCCEEDED(re_read_rc)) {
        const s64 diff = static_cast<s64>(timestamp) - static_cast<s64>(user_clock_after);
        if (diff > -MIN_CORRECTION_SECONDS && diff < MIN_CORRECTION_SECONDS) {
            ReportSyncStage("User Clock updated live via automatic correction");
            R_SUCCEED();
        }
    } else {
        ReportSyncFailure("re-reading User Clock", re_read_rc);
    }

    ReportSyncStage("trying set:sys fallback");
    ReportSyncStage("opening set:sys");
    const auto setsys_init_rc = setsysInitialize();
    if (R_FAILED(setsys_init_rc)) {
        log_write_error("ntp: user clock write failed via time:su (0x%08X); time:s (0x%08X); set:sys (0x%08X)",
            R_VALUE(system_user_rc), R_VALUE(system_rc), R_VALUE(setsys_init_rc));
        ReportSyncFailure("opening set:sys", setsys_init_rc);
        R_THROW(setsys_init_rc);
    }
    ON_SCOPE_EXIT(setsysExit());

    TimeSteadyClockTimePoint steady{};
    ReportSyncStage("set:sys reading steady clock");
    const auto steady_rc = timeGetStandardSteadyClockTimePoint(&steady);
    if (R_FAILED(steady_rc)) {
        log_write_error("ntp: user clock write failed via time:su (0x%08X); time:s (0x%08X); steady clock (0x%08X)",
            R_VALUE(system_user_rc), R_VALUE(system_rc), R_VALUE(steady_rc));
        ReportSyncFailure("set:sys reading steady clock", steady_rc);
        R_THROW(steady_rc);
    }

    TimeSystemClockContext context{};
    context.timestamp = steady;
    context.offset = static_cast<s64>(timestamp) - steady.time_point;

    ReportSyncStage("set:sys setting NetworkSystemClockContext");
    const auto net_ctx_rc = setsysSetNetworkSystemClockContext(&context);
    if (R_FAILED(net_ctx_rc)) {
        log_write_error("ntp: user clock write failed via time:su (0x%08X); time:s (0x%08X); set:sys net context (0x%08X)",
            R_VALUE(system_user_rc), R_VALUE(system_rc), R_VALUE(net_ctx_rc));
        ReportSyncFailure("set:sys setting NetworkSystemClockContext", net_ctx_rc);
        R_THROW(net_ctx_rc);
    }
    ReportSyncStage("set:sys NetworkSystemClockContext updated");

    ReportSyncStage("set:sys enabling automatic correction");
    const auto auto_corr_rc = setsysSetUserSystemClockAutomaticCorrectionEnabled(true);
    if (R_FAILED(auto_corr_rc)) {
        ReportSyncFailure("set:sys enabling automatic correction", auto_corr_rc);
        R_THROW(auto_corr_rc);
    }
    ReportSyncStage("set:sys automatic correction enabled");

    out_used_fallback = true;
    R_SUCCEED();
}

// The raw max77620 rtc, read the way hekate reads it (bdk/rtc/max77620-rtc.c:34).
// hekate reads the chip directly; the offsets HOS keeps on top of it are invisible to it.
std::optional<hekate_clock::RtcDate> ReadRawRtc() {
    if (R_FAILED(i2cInitialize())) {
        return std::nullopt;
    }
    ON_SCOPE_EXIT(i2cExit());
    I2cSession s{};
    if (R_FAILED(i2cOpenSession(&s, I2cDevice_Max77620Rtc))) {
        return std::nullopt;
    }
    ON_SCOPE_EXIT(i2csessionClose(&s));

    // UPDATE0 bit 4 copies the running counters into the read registers.
    // Bit 0 would write the registers back into the clock: never set it.
    const u8 latch[2] = {0x04, 0x10};
    if (R_FAILED(i2csessionSendAuto(&s, latch, sizeof(latch), I2cTransactionOption_All))) {
        return std::nullopt;
    }
    svcSleepThread(16'000'000);

    u8 control{};
    u8 regs[7]{};
    const u8 cmd_control[] = {0x40, 0x01, 0x03, 0xC1, 0x01};
    const u8 cmd_time[] = {0x40, 0x01, 0x07, 0xC1, sizeof(regs)};
    if (R_FAILED(i2csessionExecuteCommandList(&s, &control, sizeof(control), cmd_control, sizeof(cmd_control))) ||
        R_FAILED(i2csessionExecuteCommandList(&s, regs, sizeof(regs), cmd_time, sizeof(cmd_time)))) {
        return std::nullopt;
    }
    return hekate_clock::DecodeRtc(regs, control);
}

Result RunSync() {
    ReportSyncStage("checking network connection");
    if (!HasInternet()) {
        ReportSyncStage("no network connection");
        R_THROW(Result_NtpNoConnection);
    }
    ReportSyncStage("network connection ready");

    u64 network_time{};
    Result rc = Result_NtpResolveFailed;
    for (const auto server : SERVERS) {
        rc = QueryServer(server, &network_time);
        if (R_SUCCEEDED(rc)) {
            break;
        }
        ReportSyncFailure(std::string("querying ") + server, rc);
    }
    R_TRY(rc);
    const u64 reply_tick = armGetSystemTick();
    const auto utc_now = [&] {
        return network_time + armTicksToNs(armGetSystemTick() - reply_tick) / 1'000'000'000ULL;
    };

    u64 current_time{};
    ReportSyncStage("reading User Clock");
    const auto current_time_rc = timeGetCurrentTime(TimeType_UserSystemClock, &current_time);
    if (R_FAILED(current_time_rc)) {
        ReportSyncFailure("reading User Clock", current_time_rc);
        R_THROW(current_time_rc);
    }

    const s64 displayed_time = static_cast<s64>(current_time) + g_display_offset.load(std::memory_order_relaxed);
    const auto offset = static_cast<s64>(network_time) - displayed_time;
    ReportSyncStage("clock offset " + std::to_string(offset) + " seconds");
    if (offset > -MIN_CORRECTION_SECONDS && offset < MIN_CORRECTION_SECONDS) {
        ReportSyncStage("clock already synchronized");
        // hekate keeps its own offset, which may still be wrong; costs no sd write when it is right.
        SyncHekateClock(utc_now());
        R_SUCCEED();
    }

    bool used_fallback = false;
    ReportSyncStage("writing corrected clock");
    const auto set_time_rc = SetSystemTime(network_time, used_fallback);
    if (R_FAILED(set_time_rc)) {
        ReportSyncFailure("synchronization", set_time_rc);
        R_THROW(set_time_rc);
    }
    SyncHekateClock(utc_now());

    if (used_fallback) {
        const s64 new_offset = static_cast<s64>(network_time) - static_cast<s64>(current_time);
        g_display_offset.store(new_offset, std::memory_order_relaxed);
        ReportSyncStage("automatic correction enabled; reboot required to update HOS User Clock");
    } else {
        g_display_offset.store(0, std::memory_order_relaxed);
        ReportSyncStage("clock updated; refreshing UI");
        evman::push(evman::FunctionalEventData{[]() {
            __libnx_init_time();
            App::Notify("Clock synced"_i18n);
        }}, false);
    }

    R_SUCCEED();
}

void ThreadFunc(void*) {
    // DNS-MITM reads the hosts file on the first DNS query; System Settings links a profile
    // to Nextendo only after that (Prelude-Nextendo does the same query at boot).
    if (::sphaira::nextendo::Active()) {
        const auto* he = gethostbyname("accounts.nintendo.com");
        log_write("[Nextendo] dns warmup: %s\n", he ? "ok" : "failed");
    }

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

        ReportSyncStage("background synchronization started");
        const auto rc = RunSync();
        if (R_SUCCEEDED(rc)) {
            ReportSyncStage("background synchronization complete");
        }
        // no connection yet is the normal case on boot, so keep the fast poll
        // for it and back off only for real failures.
        wait_ns = R_SUCCEEDED(rc) ? RESYNC_INTERVAL_NS
            : rc == Result_NtpNoConnection ? POLL_INTERVAL_NS : RETRY_INTERVAL_NS;
    }
}

} // namespace

void SyncHekateClock(u64 utc) {
    using namespace hekate_clock;
    if (!fs::DirExists("/bootloader")) {
        return; // no hekate on this card
    }
    const auto rtc = ReadRawRtc();
    if (!rtc) {
        log_write("[NTP] hekate clock: rtc read failed\n");
        return;
    }
    TimeCalendarTime cal{};
    TimeCalendarAdditionalInfo info{};
    const auto tz_rc = timeToCalendarTimeWithMyRule(utc, &cal, &info);
    if (R_FAILED(tz_rc)) {
        log_write("[NTP] hekate clock: timezone failed 0x%08X\n", R_VALUE(tz_rc));
        return;
    }

    // Nyx shows local wall time; info.offset is this zone's utc offset now, dst included.
    const bool auto_dst = ini_getl("config", "timedst", 1, NYX_INI) != 0;
    const auto wanted = Offset(static_cast<s64>(utc) + info.offset, *rtc, auto_dst);
    char stored[16]{};
    ini_gets("config", "timeoffset", "0", stored, sizeof(stored), NYX_INI);
    if (!NeedsWrite(static_cast<u32>(std::strtoul(stored, nullptr, 16)), wanted)) {
        log_write("[NTP] hekate clock: timeoffset=%s unchanged\n", stored);
        return;
    }
    const auto hex = Hex(wanted);
    if (ini_puts("config", "timeoffset", hex.c_str(), NYX_INI)) {
        log_write("[NTP] hekate clock: timeoffset=%s (was %s, timedst=%d)\n", hex.c_str(), stored, auto_dst);
    } else {
        log_write("[NTP] hekate clock: writing %s failed\n", NYX_INI);
    }
}

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

s64 GetDisplayOffset() {
    return g_display_offset.load(std::memory_order_relaxed);
}

} // namespace sphaira::ntp
