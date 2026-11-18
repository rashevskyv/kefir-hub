// Internet speed test: downloads from Cloudflare's speed endpoint and throws the bytes away.
// Four runs tell where the limit is: HTTP vs HTTPS (TLS costs CPU on the console) and one vs four connections
// (a limit per connection vs a limit of the whole link).

#include "speed_test.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::speedtest {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int SECONDS = 8;
// the server refuses a very big file, so a finished 25 MB file is simply requested again until the clock ends the run
constexpr const char* PATH = "speed.cloudflare.com/__down?bytes=25000000";

struct Run {
    const char* name;
    const char* scheme;
    int conns;
};

struct Counter {
    s64 bytes{};
    bool stop{};
};

struct Line {
    std::string line;
    bool failed{};
};

size_t OnWrite(char*, size_t size, size_t nmemb, void* user) {
    auto c = static_cast<Counter*>(user);
    c->bytes += static_cast<s64>(size * nmemb);
    return c->stop ? 0 : size * nmemb;
}

auto Ms(Clock::time_point a, Clock::time_point b) -> s64 {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}

auto Measure(ui::ProgressBox* pbox, const Run& run) -> Line {
    pbox->NewTransfer(run.name);
    const std::string url = std::string{run.scheme} + "://" + PATH;

    struct Conn {
        CURL* easy{};
        Counter count;
    };
    std::vector<Conn> conns(run.conns);
    auto multi = curl_multi_init();
    for (auto& c : conns) {
        c.easy = curl_easy_init();
        curl_easy_setopt(c.easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c.easy, CURLOPT_WRITEFUNCTION, OnWrite);
        curl_easy_setopt(c.easy, CURLOPT_WRITEDATA, &c.count);
        curl_easy_setopt(c.easy, CURLOPT_USERAGENT, APP_USER_AGENT);
        curl_easy_setopt(c.easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c.easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c.easy, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(c.easy, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(c.easy, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c.easy, CURLOPT_BUFFERSIZE, 1024L * 512L);
        curl_multi_add_handle(multi, c.easy);
    }

    const auto begin = Clock::now();
    Clock::time_point first_byte{};
    bool started = false;
    s64 window_bytes = 0, peak = 0; // bytes in the best one-second window
    auto window_start = begin;
    int running = 0;
    CURLcode err = CURLE_OK;
    for (;;) {
        curl_multi_perform(multi, &running);
        curl_multi_poll(multi, nullptr, 0, 50, nullptr);

        // a finished file starts again while time is left; a failed one stays out (its error is kept)
        int msgs = 0;
        while (auto m = curl_multi_info_read(multi, &msgs)) {
            if (m->msg != CURLMSG_DONE) {
                continue;
            }
            const auto easy = m->easy_handle;
            curl_multi_remove_handle(multi, easy);
            if (m->data.result == CURLE_OK) {
                if (Ms(begin, Clock::now()) < SECONDS * 1000) {
                    curl_multi_add_handle(multi, easy);
                    running++;
                }
            } else {
                err = m->data.result;
            }
        }

        s64 total = 0;
        for (auto& c : conns) {
            total += c.count.bytes;
        }
        const auto now = Clock::now();
        if (!started && total > 0) {
            started = true;
            first_byte = now;
            window_start = now;
            window_bytes = total;
        }
        if (started) {
            if (Ms(window_start, now) >= 1000) {
                peak = std::max(peak, total - window_bytes);
                window_bytes = total;
                window_start = now;
            }
            const auto ms = Ms(first_byte, now);
            pbox->SetTransfer(std::string{run.name} + ": " + FormatRate(total, ms));
        }
        if (running == 0 || Ms(begin, now) >= SECONDS * 1000 || pbox->ShouldExit()) {
            break;
        }
    }

    const auto end = Clock::now();
    s64 total = 0;
    for (auto& c : conns) {
        total += c.count.bytes;
    }
    double ttfb = 0;
    if (!conns.empty()) {
        curl_easy_getinfo(conns[0].easy, CURLINFO_STARTTRANSFER_TIME, &ttfb);
    }
    for (auto& c : conns) {
        c.count.stop = true;
    }
    for (auto& c : conns) {
        curl_multi_remove_handle(multi, c.easy); // fails harmlessly when it is already out
        curl_easy_cleanup(c.easy);
    }
    curl_multi_cleanup(multi);

    Line r;
    const auto ms = started ? Ms(first_byte, end) : 0;
    if (total == 0) {
        r.failed = true;
        r.line = std::string{run.name} + ": " + "no data"_i18n + (err != CURLE_OK ? std::string{" ("} + curl_easy_strerror(err) + ")" : "");
    } else {
        r.line = std::string{run.name} + ": " + FormatRate(total, ms) + " · " + "peak"_i18n + " " + FormatRate(peak, 1000) +
                 " · " + "first byte"_i18n + " " + std::to_string(static_cast<int>(ttfb * 1000)) + " ms";
    }
    log_write("[SPEED] %s\n", r.line.c_str());
    return r;
}

} // namespace

void Open() {
    App::Push<ui::OptionBox>(
        "Download from the internet for about 25 seconds to measure the real speed of your connection? A fast connection uses a few hundred MB of data."_i18n,
        "Back"_i18n, "Start"_i18n, 1, [](auto op) {
            if (!op || *op != 1) {
                return;
            }
            auto lines = std::make_shared<std::vector<std::string>>();
            App::Push<ui::ProgressBox>(0, "Internet speed test"_i18n, "Cloudflare", [lines](auto pbox) -> Result {
                // HTTP first, then HTTPS: a gap between them is the TLS cost. Four connections last: a gain over
                // one connection means the limit is per connection, no gain means the whole link is the limit.
                static const Run RUNS[] = {
                    {"HTTP, 1 connection", "http", 1},
                    {"HTTPS, 1 connection", "https", 1},
                    {"HTTP, 4 connections", "http", 4},
                };
                for (const auto& run : RUNS) {
                    if (pbox->ShouldExit()) {
                        return Result_TransferCancelled;
                    }
                    lines->push_back(Measure(pbox, run).line);
                }
                return 0;
            }, [lines](Result rc) {
                if (rc == Result_TransferCancelled) {
                    return;
                }
                std::string text;
                for (const auto& l : *lines) {
                    text += l + "\n";
                }
                App::Push<ui::OptionBox>(text, "OK"_i18n);
            });
        });
}

} // namespace sphaira::speedtest
