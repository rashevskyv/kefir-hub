#include "utils/nfs_url.hpp"
#include <iostream>
#include <cassert>
#include <string>

namespace {

int g_checks = 0;

#define CHECK(expr) do { \
    g_checks++; \
    if (!(expr)) { \
        std::cerr << "FAIL: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void test_valid_urls() {
    {
        auto res = sphaira::nfs::ParseUrl("nfs://192.168.1.20/export");
        CHECK(res.valid);
        CHECK(res.server == "192.168.1.20");
        CHECK(res.export_path == "/export");
        CHECK(res.port == 0);
    }
    {
        auto res = sphaira::nfs::ParseUrl("nfs://192.168.1.20:2049/export");
        CHECK(res.valid);
        CHECK(res.server == "192.168.1.20");
        CHECK(res.export_path == "/export");
        CHECK(res.port == 2049);
    }
    // e00ac7c regression case: full export path must be preserved
    {
        auto res = sphaira::nfs::ParseUrl("nfs://nas.example.local/export/media");
        CHECK(res.valid);
        CHECK(res.server == "nas.example.local");
        CHECK(res.export_path == "/export/media");
        CHECK(res.port == 0);
    }
    {
        auto res = sphaira::nfs::ParseUrl("nfs://nas.example.local:111/export/media/nested/folder");
        CHECK(res.valid);
        CHECK(res.server == "nas.example.local");
        CHECK(res.export_path == "/export/media/nested/folder");
        CHECK(res.port == 111);
    }
    {
        auto res = sphaira::nfs::ParseUrl("nfs://my-server_01/share");
        CHECK(res.valid);
        CHECK(res.server == "my-server_01");
        CHECK(res.export_path == "/share");
        CHECK(res.port == 0);
    }
    {
        CHECK(sphaira::nfs::ValidateUrl("nfs://10.0.0.5/volume1"));
        CHECK(sphaira::nfs::ValidateUrl("nfs://storage.local:2049/exports/games"));
    }

    // Maximum accepted URL length check: exactly kMaxNfsUrlLength (768 characters)
    {
        std::string base = "nfs://192.168.1.1/export/";
        std::string fill(sphaira::nfs::kMaxNfsUrlLength - base.length(), 'a');
        std::string max_url = base + fill;
        CHECK(max_url.length() == sphaira::nfs::kMaxNfsUrlLength);
        CHECK(max_url.length() == 768);
        auto res = sphaira::nfs::ParseUrl(max_url);
        CHECK(res.valid);
        CHECK(res.server == "192.168.1.1");
        CHECK(res.export_path == std::string("/export/") + fill);
    }
}

void test_invalid_urls() {
    const char* invalids[] = {
        "",
        // Scheme checks: only lowercase nfs:// is accepted
        "NFS://192.168.1.1/export",
        "Nfs://192.168.1.1/export",
        "nFs://192.168.1.1/export",
        "smb://192.168.1.1/export",
        "http://192.168.1.1/export",
        "ftp://192.168.1.1/export",
        "192.168.1.1/export",
        "nfs://",
        "nfs:///",
        "nfs:///export",
        "nfs://host",
        "nfs://host/",
        "nfs://user@host/export",
        "nfs://user:pass@host/export",
        "nfs://[::1]/export",
        "nfs://[fe80::1]/export",
        "nfs://host:0/export",
        "nfs://host:65536/export",
        "nfs://host:999999/export",
        "nfs://host:abc/export",
        "nfs://host:2049:extra/export",
        "nfs://host/export?query=1",
        "nfs://host/export#fragment",
        // Dot / traversal segments
        "nfs://host/export/../other",
        "nfs://host/../export",
        "nfs://host/export/.",
        "nfs://host/./export",
        "nfs://host/export//sub",
        // Percent-encoded dot and traversal cases
        "nfs://host/export/%2e",
        "nfs://host/export/%2E",
        "nfs://host/export/%2e%2e",
        "nfs://host/export/%2E%2E",
        "nfs://host/export/%2e%2e/",
        "nfs://host/export/%2E%2E/",
        "nfs://host/export/%2e/media",
        "nfs://host/export/%2E/media",
        "nfs://host/%2e%2e/export",
        "nfs://host/%2E%2E/export",
        // Encoded separators and backslashes
        "nfs://host/export%2fmedia",
        "nfs://host/export%2Fmedia",
        "nfs://host/export%5cmedia",
        "nfs://host/export%5Cmedia",
        "nfs://host/export\\media",
        // Whitespace and control chars
        "nfs://ho st/export",
        "nfs://host/exp ort",
        "nfs://host/export\n",
        "nfs://host/export\t",
        "nfs://host/export\r",
        // Invalid hostname formatting
        "nfs://.invalid/export",
        "nfs://invalid./export",
        "nfs://in..valid/export",
        "nfs://-host/export",
        "nfs://host-/export",
    };

    for (const auto* u : invalids) {
        std::string err;
        CHECK(!sphaira::nfs::ValidateUrl(u, &err));
        CHECK(!err.empty());
        auto res = sphaira::nfs::ParseUrl(u);
        CHECK(!res.valid);
    }

    // One-character-over-limit rejection check: 769 characters (kMaxNfsUrlLength + 1)
    {
        std::string base = "nfs://192.168.1.1/export/";
        std::string fill(sphaira::nfs::kMaxNfsUrlLength + 1 - base.length(), 'a');
        std::string over_url = base + fill;
        CHECK(over_url.length() == sphaira::nfs::kMaxNfsUrlLength + 1);
        CHECK(over_url.length() == 769);
        std::string err;
        CHECK(!sphaira::nfs::ValidateUrl(over_url, &err));
        CHECK(!err.empty());
        auto res = sphaira::nfs::ParseUrl(over_url);
        CHECK(!res.valid);
    }
}

} // namespace

int main() {
    test_valid_urls();
    test_invalid_urls();
    std::cout << "ok  nfs_url: " << g_checks << " checks passed" << std::endl;
    return 0;
}
