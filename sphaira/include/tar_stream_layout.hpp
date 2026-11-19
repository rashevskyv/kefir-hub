#pragma once

// A tar made on the fly from files that stay where they are: the layout DBI Backend Qt sends
// (Python tarfile, pax format), so sd_bundle::Receive reads a folder on the console the way it
// reads a bundle from the PC. A member is a ustar header, the data, zero padding to 512 bytes;
// a name that is not plain ASCII or is longer than 100 bytes gets a pax "path" record in front.
// Pure logic, host-tested in tests/test_tar_stream_layout.cpp.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::tar_stream {

inline constexpr int64_t BLOCK = 512;
inline constexpr int64_t USTAR_MAX_SIZE = 077777777777LL; // 11 octal digits: 8 GiB - 1

struct Member {
    std::string name;  // path inside the tar
    std::string src;   // the file that holds the data; empty: `data` is the content
    std::string data;
    int64_t size{};
};

inline auto Ustar(std::string_view name, int64_t size, char type) -> std::string {
    std::string h(BLOCK, '\0');
    const auto put = [&](size_t off, size_t len, std::string_view v) {
        std::memcpy(h.data() + off, v.data(), std::min(len, v.size()));
    };
    char num[32];
    put(0, 100, name);
    put(100, 7, "0000644");
    put(108, 7, "0000000");
    put(116, 7, "0000000");
    std::snprintf(num, sizeof(num), "%011llo", static_cast<unsigned long long>(size));
    put(124, 11, num);
    put(136, 11, "00000000000");
    put(148, 8, "        "); // the checksum counts its own field as spaces
    h[156] = type;
    put(257, 5, "ustar");
    put(263, 2, "00");
    unsigned sum = 0;
    for (const unsigned char c : h) sum += c;
    std::snprintf(num, sizeof(num), "%06o", sum);
    put(148, 6, num);
    h[154] = '\0';
    return h;
}

// "<length> <key>=<value>\n", the length counts the whole record.
inline auto PaxRecord(std::string_view key, std::string_view value) -> std::string {
    const size_t body = 1 + key.size() + 1 + value.size() + 1;
    size_t len = body + 1;
    while (std::to_string(len).size() + body != len) len = std::to_string(len).size() + body;
    return std::to_string(len) + " " + std::string{key} + "=" + std::string{value} + "\n";
}

// everything in front of a member's data.
inline auto Header(std::string_view name, int64_t size) -> std::string {
    const bool ascii = std::all_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    const bool big = size > USTAR_MAX_SIZE;
    if (ascii && name.size() <= 100 && !big) {
        return Ustar(name, size, '0');
    }
    auto records = PaxRecord("path", name);
    if (big) records += PaxRecord("size", std::to_string(size));
    auto out = Ustar("././@PaxHeader", static_cast<int64_t>(records.size()), 'x') + records;
    out.resize((out.size() + BLOCK - 1) / BLOCK * BLOCK, '\0');
    // readers without pax see this name; pax readers take the record
    std::string plain{name.substr(0, 100)};
    for (auto& c : plain) {
        if (static_cast<unsigned char>(c) >= 0x80) c = '_';
    }
    return out + Ustar(plain, big ? 0 : size, '0');
}

struct Layout {
    std::vector<Member> members;
    std::vector<int64_t> starts; // where each member's header begins
    int64_t members_end{};
    int64_t size{};              // the whole tar, with the two empty end blocks

    Layout() = default;
    explicit Layout(std::vector<Member> list) : members{std::move(list)} {
        for (const auto& m : members) {
            starts.push_back(members_end);
            members_end += static_cast<int64_t>(Header(m.name, m.size).size()) + (m.size + BLOCK - 1) / BLOCK * BLOCK;
        }
        size = members_end + 2 * BLOCK;
    }

    // Fills `buf` with the `n` bytes at `off` (the caller keeps off + n <= size).
    // `read_file(member index, offset in the file, buf, bytes)` must fill every byte and return 0,
    // or return its error, which stops the read and is returned.
    template <typename F>
    auto Read(int64_t off, void* buf, int64_t n, F&& read_file) const -> uint32_t {
        auto out = static_cast<char*>(buf);
        while (n > 0) {
            int64_t take = n;
            if (off >= members_end) {
                std::memset(out, 0, take); // the end blocks
            } else {
                const auto i = static_cast<size_t>(std::upper_bound(starts.begin(), starts.end(), off) - starts.begin() - 1);
                const auto& m = members[i];
                const auto header = Header(m.name, m.size);
                const auto hlen = static_cast<int64_t>(header.size());
                const auto rel = off - starts[i];
                const auto next = i + 1 < starts.size() ? starts[i + 1] : members_end;
                if (rel < hlen) {
                    take = std::min(n, hlen - rel);
                    std::memcpy(out, header.data() + rel, take);
                } else if (rel < hlen + m.size) {
                    take = std::min(n, hlen + m.size - rel);
                    if (m.src.empty()) {
                        std::memcpy(out, m.data.data() + (rel - hlen), take);
                    } else if (const auto rc = read_file(i, rel - hlen, out, take)) {
                        return rc;
                    }
                } else {
                    take = std::min(n, next - off);
                    std::memset(out, 0, take); // padding to a whole block
                }
            }
            out += take;
            off += take;
            n -= take;
        }
        return 0;
    }
};

} // namespace sphaira::tar_stream
