// Host test for sphaira/include/nextendo_patches.hpp, run against the real generated list (nextendo_snapshot.hpp)

#include "nextendo_patches.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace sphaira::nextendo;

int main() {
    // every shipped patch folder carries the prefix, so the Hub and Semi-stock find them with nextendo_*
    const auto exefs = ShippedFolders("atmosphere/exefs_patches/");
    const auto nro = ShippedFolders("atmosphere/nro_patches/");
    assert(!exefs.empty() && !nro.empty());
    std::size_t in_roots = 0;
    for (const auto& f : kPatchFiles) {
        const std::string path = f.path;
        assert(f.size > 0);
        if (path.rfind("atmosphere/exefs_patches/", 0) == 0 || path.rfind("atmosphere/nro_patches/", 0) == 0) {
            in_roots++;
            const auto folder = path.substr(path.find('/', 11) + 1);
            assert(folder.rfind("nextendo_", 0) == 0);
        } else {
            assert(path.rfind("atmosphere/contents/0100000000000803/", 0) == 0); // browser certs, the only other kind
        }
    }
    assert(in_roots > 0);
    for (std::size_t i = 1; i < exefs.size(); i++) {
        assert(exefs[i - 1] < exefs[i]); // one entry per folder
    }

    // build IDs: 40 or 64 hex digits, zero padded like Atmosphere does; anything else is refused
    auto id = ParseBuildId("0A6CD7F6CF67BEB4DC9FFFC999EB37A80A788793");
    auto padded = ParseBuildId("0a6cd7f6cf67beb4dc9fffc999eb37a80a788793000000000000000000000000");
    assert(id && padded && *id == *padded && (*id)[0] == 0x0A && (*id)[19] == 0x93 && (*id)[20] == 0);
    assert(!ParseBuildId("") && !ParseBuildId("ABC") && !ParseBuildId("ZZ") && !ParseBuildId(std::string(66, 'A')));
    assert(BuildIdHex(*id) == "0A6CD7F6CF67BEB4DC9FFFC999EB37A80A788793");

    // the ssl check: a module whose ID has a file in the ssl folder passes, any other module does not
    std::string ssl_file;
    for (const auto& f : kPatchFiles) {
        if (std::string{f.path}.rfind(std::string{kSslFolder}, 0) == 0) {
            ssl_file = f.path;
            break;
        }
    }
    assert(!ssl_file.empty());
    const auto hex = ssl_file.substr(kSslFolder.size(), ssl_file.size() - kSslFolder.size() - 4);
    const auto ssl = ParseBuildId(hex);
    assert(ssl);
    BuildId other{};
    other[0] = 0x42;
    assert(SslPatched({*ssl}));
    assert(SslPatched({other, *ssl})); // any module of the process
    assert(!SslPatched({other}) && !SslPatched({}));

    std::puts("test_nextendo_patches: ok");
    return 0;
}
