// Host test of the RAR backend: sphaira/source/rar_reader.cpp linked against the real
// UnRAR sources (built by tests/test_rar_host.sh), reading tests/contract_fixtures/rar.
// Usage: rar_reader_host <fixture dir> <scratch dir>
#include "archive_volumes.hpp"
#include "rar_reader.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace sphaira;
using rar::Error;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::string fixtures, scratch;

// the bytes tests/contract_fixtures/rar/make.sh packed.
static auto Expected() -> std::map<std::string, std::string> {
    std::string readme;
    char line[64];
    for (int i = 0; i < 400; i++) {
        std::snprintf(line, sizeof(line), "Kefir Hub RAR fixture line %04d\n", i);
        readme += line;
    }
    std::string data;
    std::uint32_t x = 1;
    for (int i = 0; i < 12000; i++) {
        x = x * 1103515245u + 12345u;
        data += static_cast<char>((x >> 16) & 0xFF);
    }
    return {
        {"readme.txt", readme},
        {"dir/data.bin", data},
        {"dir/sub/empty.txt", ""},
        {"dir/\xD1\x82\xD0\xB5\xD1\x81\xD1\x82.txt", "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82\n"}, // тест.txt: привіт
    };
}

static auto Bytes(const std::string& path) -> std::string {
    std::ifstream f{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{f}, {}};
}

static void Put(const std::string& path, const std::string& bytes) {
    std::ofstream{path, std::ios::binary}.write(bytes.data(), bytes.size());
}

// lists `path` and checks every expected file shows up with its size.
static void CheckList(const std::string& path, bool solid) {
    const auto want = Expected();
    rar::Reader r;
    CHECK(r.Open(path, false) == Error::None);
    CHECK(r.solid == solid);
    std::map<std::string, std::int64_t> files;
    bool saw_dir = false;
    for (int guard = 0; guard < 100; guard++) {
        rar::Entry e;
        bool end{};
        CHECK(r.Next(&e, &end) == Error::None);
        if (end) {
            break;
        }
        if (e.is_file) {
            files[e.name] = e.size;
            CHECK(e.mtime > 1600000000); // packed in 2026: a real unix time
        }
        saw_dir |= e.is_dir && e.name == "dir/sub";
        CHECK(r.Skip({}) == Error::None);
    }
    CHECK(saw_dir);
    CHECK(files.size() == want.size());
    for (const auto& [name, bytes] : want) {
        CHECK(files.count(name) && files[name] == static_cast<std::int64_t>(bytes.size()));
    }
}

// decompresses every file of `path` in one pass and compares the bytes.
static void CheckExtractAll(const std::string& path) {
    const auto want = Expected();
    rar::Reader r;
    CHECK(r.Open(path, true) == Error::None);
    size_t matched = 0;
    for (int guard = 0; guard < 100; guard++) {
        rar::Entry e;
        bool end{};
        CHECK(r.Next(&e, &end) == Error::None);
        if (end) {
            break;
        }
        if (!e.is_file) {
            CHECK(r.Skip({}) == Error::None);
            continue;
        }
        std::string got;
        CHECK(r.Read([&](const void* d, size_t n) { got.append(static_cast<const char*>(d), n); return true; }) == Error::None);
        const auto it = want.find(e.name);
        CHECK(it != want.end() && it->second == got);
        matched += it != want.end() && it->second == got;
    }
    CHECK(matched == want.size());
}

// reads one entry through the worker-thread stream, in odd-sized pieces.
static auto StreamOne(const std::string& path, const std::string& name, Error* err) -> std::string {
    rar::EntryStream s{path, [&](const rar::Entry& e) { return e.name == name; }};
    std::string out;
    char buf[777];
    while (const auto got = s.Read(buf, sizeof(buf))) {
        out.append(buf, got);
        if (got < sizeof(buf)) {
            break;
        }
    }
    *err = s.GetError();
    return out;
}

static void CheckStreamAll(const std::string& path) {
    for (const auto& [name, bytes] : Expected()) {
        Error e{};
        CHECK(StreamOne(path, name, &e) == bytes);
        CHECK(e == Error::None);
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: %s <fixtures> <scratch>\n", argv[0]);
        return 2;
    }
    fixtures = std::string{argv[1]} + "/";
    scratch = std::string{argv[2]} + "/";

    // sets, by name: any volume leads to the first one.
    CHECK(archive::VolumePaths(fixtures + "multi5.part2.rar").front() == fixtures + "multi5.part1.rar");
    CHECK(archive::VolumePaths(fixtures + "multi5.part3.rar").size() == 3);
    CHECK(archive::VolumePaths(fixtures + "multi4.r01").front() == fixtures + "multi4.rar");
    CHECK(archive::VolumePaths(fixtures + "multi4.rar").size() == 3);
    CHECK(rar::IsRarName(archive::VolumePaths(fixtures + "multi4.r00").front()));

    // solid RAR4 (what libarchive refuses), solid / plain RAR5, both split styles.
    CheckList(fixtures + "solid4.rar", true);
    CheckList(fixtures + "solid5.rar", true);
    CheckList(fixtures + "plain5.rar", false);
    CheckList(fixtures + "multi5.part1.rar", false);
    CheckList(fixtures + "multi4.rar", false);
    for (const auto* a : {"solid4.rar", "solid5.rar", "plain5.rar", "multi5.part1.rar", "multi4.rar"}) {
        CheckExtractAll(fixtures + a);
        CheckStreamAll(fixtures + a);
    }

    // a stream closed early (reader stops after one piece) must not hang or leak the worker.
    for (int i = 0; i < 20; i++) {
        rar::EntryStream s{fixtures + "solid4.rar", [](const rar::Entry& e) { return e.name == "dir/data.bin"; }};
        char b[100];
        CHECK(s.Read(b, sizeof(b)) == sizeof(b));
    }
    // closed before the first read, while the worker is still skipping solid entries.
    for (int i = 0; i < 20; i++) {
        rar::EntryStream s{fixtures + "solid5.rar", [](const rar::Entry& e) { return e.name == "readme.txt"; }};
    }

    Error e{};
    StreamOne(fixtures + "solid4.rar", "no/such.file", &e);
    CHECK(e == Error::NotFound);

    // a sink that stops: Stopped, not Damaged.
    {
        rar::Reader r;
        CHECK(r.Open(fixtures + "plain5.rar", true) == Error::None);
        rar::Entry en;
        bool end{};
        while (r.Next(&en, &end) == Error::None && !end && en.name != "dir/data.bin") {
            r.Skip({});
        }
        CHECK(en.name == "dir/data.bin");
        CHECK(r.Read([](const void*, size_t) { return false; }) == Error::Stopped);
    }

    // passwords are not supported: a clear error, data-only and header encryption.
    {
        rar::Reader r;
        CHECK(r.Open(fixtures + "enc_head.rar", false) == Error::Password);
    }
    StreamOne(fixtures + "enc_data.rar", "readme.txt", &e);
    CHECK(e == Error::Password);

    // not a rar, missing file.
    {
        rar::Reader r;
        CHECK(r.Open(fixtures + "make.sh", false) == Error::Unsupported);
        rar::Reader m;
        CHECK(m.Open(fixtures + "missing.rar", false) == Error::SourceRead);
    }

    // a split set with its last volume gone.
    std::filesystem::create_directories(scratch);
    Put(scratch + "cut.part1.rar", Bytes(fixtures + "multi5.part1.rar"));
    Put(scratch + "cut.part2.rar", Bytes(fixtures + "multi5.part2.rar"));
    StreamOne(scratch + "cut.part1.rar", "dir/data.bin", &e);
    std::printf("missing volume -> %d\n", static_cast<int>(e));
    CHECK(e == Error::SourceRead);

    // truncated and corrupted copies: an error, never a crash or wrong bytes passed as good.
    const auto solid4 = Bytes(fixtures + "solid4.rar");
    const auto solid5 = Bytes(fixtures + "solid5.rar");
    for (const auto& [name, src] : {std::pair{"trunc4.rar", solid4}, std::pair{"trunc5.rar", solid5}}) {
        Put(scratch + name, src.substr(0, src.size() / 2));
        const auto want = Expected();
        bool any_error = false;
        for (const auto& [entry, bytes] : want) {
            const auto got = StreamOne(scratch + name, entry, &e);
            any_error |= e != Error::None;
            CHECK(e != Error::None || got == bytes);
        }
        std::printf("%s -> error seen: %d\n", name, any_error);
        CHECK(any_error);
    }
    for (const auto& [name, src] : {std::pair{"bad4.rar", solid4}, std::pair{"bad5.rar", solid5}}) {
        auto bad = src;
        for (size_t i = bad.size() / 3; i < bad.size() / 3 + 64; i++) {
            bad[i] = static_cast<char>(bad[i] ^ 0x5A);
        }
        Put(scratch + name, bad);
        StreamOne(scratch + name, "readme.txt", &e);
        std::printf("%s -> %d\n", name, static_cast<int>(e));
        CHECK(e == Error::Damaged);
    }

    std::printf(failures ? "rar_reader_host: %d FAILED\n" : "rar_reader_host: ok\n", failures);
    return failures ? 1 : 0;
}
