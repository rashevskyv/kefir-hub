#include "web_mdns.hpp"
#include "web.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "utils/thread.hpp"

#include <cerrno>
#include <cstring>
#include <cctype>
#include <string>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

namespace sphaira {
namespace {

using Socket = int;

// mDNS responder for kefir.local discovery
Thread g_mdns_thread{};
std::atomic_bool g_mdns_thread_created{false};
std::atomic_bool g_mdns_running{false};
std::atomic_bool g_mdns_active{false};
std::atomic<Socket> g_mdns_socket{-1};
u32 g_mdns_ip{0};

// Safely parse a DNS name with strict bounds, hops limit, and compression pointer support.
bool ParseDnsName(const u8* buf, size_t buf_len, size_t& offset, std::string& out_name) {
    out_name.clear();
    size_t curr = offset;
    bool jumped = false;
    size_t next_offset = 0;
    int hops = 0;

    while (true) {
        if (++hops > 16 || curr >= buf_len) {
            return false;
        }

        const u8 len = buf[curr];
        if (len == 0) {
            if (!jumped) {
                next_offset = curr + 1;
            }
            break;
        }

        if ((len & 0xC0) == 0xC0) {
            if (curr + 1 >= buf_len) {
                return false;
            }
            const size_t ptr = (static_cast<size_t>(len & 0x3F) << 8) | buf[curr + 1];
            if (ptr >= buf_len) {
                return false;
            }
            if (!jumped) {
                next_offset = curr + 2;
                jumped = true;
            }
            curr = ptr;
            continue;
        }

        if ((len & 0xC0) != 0) {
            return false;
        }

        curr++;
        if (curr + len > buf_len) {
            return false;
        }

        if (!out_name.empty()) {
            out_name.push_back('.');
        }
        if (out_name.size() + len > 255) {
            return false;
        }

        for (size_t i = 0; i < len; i++) {
            out_name.push_back(static_cast<char>(std::tolower(buf[curr + i])));
        }
        curr += len;
    }

    offset = next_offset;
    return true;
}

void MdnsThreadFunc(void*) {
    u8 buf[1500];

    while (WebShareIsRunning() && g_mdns_running) {
        const auto sock = g_mdns_socket.load();
        if (sock < 0) {
            break;
        }

        pollfd pfd{};
        pfd.fd = sock;
        pfd.events = POLLIN;

        const int poll_ret = poll(&pfd, 1, 250);
        if (poll_ret <= 0) {
            if (poll_ret < 0 && errno != EINTR && errno != EAGAIN) {
                break;
            }
            continue;
        }

        if (!(pfd.revents & POLLIN)) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                break;
            }
            continue;
        }

        sockaddr_in remote{};
        socklen_t remote_len = sizeof(remote);
        const auto n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&remote), &remote_len);
        if (n < 12) {
            continue;
        }
        if (remote.sin_family != AF_INET) {
            continue;
        }

        const u16 sender_port = ntohs(remote.sin_port);
        if (sender_port == 0) {
            continue;
        }

        const u16 id = (static_cast<u16>(buf[0]) << 8) | buf[1];
        const u16 flags = (static_cast<u16>(buf[2]) << 8) | buf[3];
        const u16 qdcount = (static_cast<u16>(buf[4]) << 8) | buf[5];

        // Only answer standard queries (QR=0, Opcode=0) with at least 1 question.
        if ((flags & 0x8000) != 0 || ((flags >> 11) & 0x0F) != 0 || qdcount == 0) {
            continue;
        }

        size_t offset = 12;
        bool matched = false;
        bool unicast_requested = false;

        for (u16 q = 0; q < qdcount; q++) {
            std::string qname;
            if (!ParseDnsName(buf, static_cast<size_t>(n), offset, qname)) {
                break;
            }
            if (offset + 4 > static_cast<size_t>(n)) {
                break;
            }

            const u16 qtype = (static_cast<u16>(buf[offset]) << 8) | buf[offset + 1];
            const u16 qclass_raw = (static_cast<u16>(buf[offset + 2]) << 8) | buf[offset + 3];
            offset += 4;

            const u16 qclass = qclass_raw & 0x7FFF;
            if (qname == "kefir.local" && (qtype == 1 || qtype == 255) && (qclass == 1 || qclass == 255)) {
                matched = true;
                if (qclass_raw & 0x8000) {
                    unicast_requested = true;
                }
                break;
            }
        }

        if (!matched) {
            continue;
        }

        u8 resp[64]{};
        size_t rlen = 0;

        // ponytail: no mDNS probe/conflict defense (RFC 6762 sec 8/9). If another host on the
        // LAN claims kefir.local, this responder does not probe or rename to kefir-2.local.
        const u16 resp_id = (unicast_requested || sender_port != 5353) ? id : 0;
        resp[rlen++] = static_cast<u8>(resp_id >> 8);
        resp[rlen++] = static_cast<u8>(resp_id & 0xFF);

        // Flags: 0x8400 (QR=1, AA=1)
        resp[rlen++] = 0x84;
        resp[rlen++] = 0x00;

        // QDCOUNT: 0
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;

        // ANCOUNT: 1
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x01;

        // NSCOUNT: 0
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;

        // ARCOUNT: 0
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;

        // Answer Name: "kefir.local"
        resp[rlen++] = 5;
        std::memcpy(&resp[rlen], "kefir", 5);
        rlen += 5;
        resp[rlen++] = 5;
        std::memcpy(&resp[rlen], "local", 5);
        rlen += 5;
        resp[rlen++] = 0;

        // TYPE: A (1)
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x01;

        // CLASS: IN (1), with cache-flush bit (0x8000) when answering mDNS
        const u16 resp_class = (sender_port == 5353) ? 0x8001 : 0x0001;
        resp[rlen++] = static_cast<u8>(resp_class >> 8);
        resp[rlen++] = static_cast<u8>(resp_class & 0xFF);

        // TTL: 120s
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x78;

        // RDLENGTH: 4
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x04;

        // RDATA: local IPv4 address
        std::memcpy(&resp[rlen], &g_mdns_ip, 4);
        rlen += 4;

        sockaddr_in dest{};
        if (unicast_requested || sender_port != 5353) {
            dest = remote;
        } else {
            dest.sin_family = AF_INET;
            dest.sin_addr.s_addr = htonl(0xE00000FB);
            dest.sin_port = htons(5353);
        }

        sendto(sock, resp, rlen, 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    }
}

} // namespace

void StopMdnsResponder() {
    g_mdns_running = false;
    g_mdns_active = false;

    const auto sock = g_mdns_socket.exchange(-1);
    if (sock >= 0) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }

    if (g_mdns_thread_created.exchange(false)) {
        threadWaitForExit(&g_mdns_thread);
        threadClose(&g_mdns_thread);
    }
}

auto StartMdnsResponder(u32 ip) -> bool {
    StopMdnsResponder();

    g_mdns_ip = ip;
    if (!g_mdns_ip) {
        log_write("[MDNS] no local IP available\n");
        return false;
    }

    const auto sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_write("[MDNS] socket() failed: %d %s\n", errno, std::strerror(errno));
        return false;
    }

    const int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(5353);

    if (bind(sock, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        log_write("[MDNS] bind() failed on port 5353: %d %s\n", errno, std::strerror(errno));
        close(sock);
        return false;
    }

    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = htonl(0xE00000FB);
    mreq.imr_interface.s_addr = g_mdns_ip;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            log_write("[MDNS] IP_ADD_MEMBERSHIP failed: %d %s\n", errno, std::strerror(errno));
            close(sock);
            return false;
        }
    }

    struct in_addr if_addr{};
    if_addr.s_addr = g_mdns_ip;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &if_addr, sizeof(if_addr));

    const u8 ttl = 255;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);

    g_mdns_socket = sock;
    g_mdns_running = true;

    Result rc = utils::CreateThread(&g_mdns_thread, MdnsThreadFunc, nullptr, 1024 * 64, PRIO_PREEMPTIVE);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_mdns_thread);
        if (R_FAILED(rc)) {
            threadClose(&g_mdns_thread);
        }
    }

    if (R_FAILED(rc)) {
        log_write("[MDNS] failed to start thread: 0x%X\n", rc);
        g_mdns_running = false;
        close(sock);
        g_mdns_socket = -1;
        return false;
    }

    g_mdns_thread_created = true;
    g_mdns_active = true;
    log_write("[MDNS] responder started for kefir.local -> %u.%u.%u.%u:5353\n",
        g_mdns_ip & 0xFF, (g_mdns_ip >> 8) & 0xFF, (g_mdns_ip >> 16) & 0xFF, (g_mdns_ip >> 24) & 0xFF);
    return true;
}

auto IsMdnsActive() -> bool {
    return g_mdns_active.load();
}

} // namespace sphaira
