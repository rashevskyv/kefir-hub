#pragma once

// The firmware store (github.com/kefircfw/nx-firmware): every firmware NCA stored once, a manifest per version.
// A console that runs one firmware already holds most NCAs of the next one in its system storage: the plan says which
// of a version's NCAs to copy from the console and which to download. Pure; host test tests/test_fw_store.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::fwstore {

struct Nca {
    std::string name;    // "<32 hex>.nca" or "<32 hex>.cnmt.nca": the file name in /firmware
    std::int64_t size{};
    std::string sha256;  // 64 hex; the first 32 digits are the content id
    std::string release; // the GitHub release that holds the file ("fw-22.0.0")
};

// 16 bytes of the content id from the file name; nullopt when the name does not start with 32 hex digits.
inline auto ContentId(std::string_view name) -> std::optional<std::array<std::uint8_t, 16>> {
    if (name.size() < 32) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 16> id{};
    for (size_t i = 0; i < 16; i++) {
        int v[2]{};
        for (size_t k = 0; k < 2; k++) {
            const char c = name[i * 2 + k];
            if (c >= '0' && c <= '9') v[k] = c - '0';
            else if (c >= 'a' && c <= 'f') v[k] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v[k] = c - 'A' + 10;
            else return std::nullopt;
        }
        id[i] = static_cast<std::uint8_t>(v[0] * 16 + v[1]);
    }
    return id;
}

// "https://github.com/kefircfw/nx-firmware/releases/download/{release}/{name}" with the fields filled in
inline auto AssetUrl(std::string tmpl, std::string_view release, std::string_view name) -> std::string {
    const auto put = [&](std::string_view key, std::string_view value) {
        if (const auto pos = tmpl.find(key); pos != std::string::npos) {
            tmpl.replace(pos, key.size(), value);
        }
    };
    put("{release}", release);
    put("{name}", name);
    return tmpl;
}

struct Plan {
    std::vector<size_t> copy;      // indexes: on the console already, same size
    std::vector<size_t> download;  // indexes: missing, or a different size
    std::int64_t total_bytes{};    // the whole version: what /firmware ends up holding
    std::int64_t download_bytes{}; // what has to cross the network
};

// console_size(name): the size of that content in the console's system storage, nullopt when it is not there.
inline auto MakePlan(const std::vector<Nca>& ncas, const std::function<std::optional<std::int64_t>(const Nca&)>& console_size) -> Plan {
    Plan plan;
    for (size_t i = 0; i < ncas.size(); i++) {
        plan.total_bytes += ncas[i].size;
        const auto have = console_size(ncas[i]);
        if (have && *have == ncas[i].size) {
            plan.copy.push_back(i);
        } else {
            plan.download.push_back(i);
            plan.download_bytes += ncas[i].size;
        }
    }
    return plan;
}

// free space the SD needs: /firmware ends up holding every NCA, plus a margin for the filesystem
inline auto NeededSdBytes(const Plan& plan) -> std::int64_t {
    return plan.total_bytes + plan.total_bytes / 10 + (32LL << 20);
}

} // namespace sphaira::fwstore
