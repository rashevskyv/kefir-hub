#include "repack/romfs_image.hpp"
#include "repack/romfs_walk.hpp"
#include "romfs_pack.hpp"
#include "romfs_pack_layout.hpp"

#include "defines.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace sphaira::repack {
namespace {

// a romfs with bigger tables than this is broken, not big: real games stay far below.
constexpr u64 TABLE_LIMIT = 256 * 1024 * 1024;

enum class Kind : u8 { Game, Bin, Loose };

struct Source {
    Kind kind{};
    u64 offset{};  // Game / Bin: offset from the start of that romfs. Loose: index into loose paths.
    u64 size{};
    u64 data{};    // offset in the new image.
};

using ReadFn = std::function<Result(void*, u64, u64)>;

Result ListImage(const ReadFn& read, std::vector<RomfsFile>& out) {
    romfs_header h;
    R_TRY(read(&h, 0, sizeof(h)));
    R_UNLESS(h.headerSize == sizeof(h) && h.dirTableSize <= TABLE_LIMIT && h.fileTableSize <= TABLE_LIMIT, Result_RepackBadRomfs);

    std::vector<u8> dirs(h.dirTableSize), files(h.fileTableSize);
    R_TRY(read(dirs.data(), h.dirTableOff, dirs.size()));
    R_TRY(read(files.data(), h.fileTableOff, files.size()));
    R_UNLESS(WalkRomfs(dirs, files, h.fileDataOff, out), Result_RepackBadRomfs);
    R_SUCCEED();
}

Result ReadFull(fs::File& f, u64 off, void* buf, u64 size) {
    auto out = static_cast<u8*>(buf);
    while (size) {
        u64 n{};
        R_TRY(f.Read(off, out, size, FsReadOption_None, &n));
        // a short read means the file shrank since it was listed.
        R_UNLESS(n, Result_RomfsPackSourceChanged);
        out += n;
        off += n;
        size -= n;
    }
    R_SUCCEED();
}

struct Image final : yati::source::Base {
    ProgramRomfs game;
    fs::FsNativeSd sd;
    fs::File bin;
    std::vector<fs::FsPath> loose;
    romfs_pack::Layout layout;
    std::vector<Source> files; // sorted by data offset.
    fs::File cur;
    u64 cur_index = UINT64_MAX;
    std::mutex mutex;

    Result ReadSource(const Source& s, u64 rel, u8* out, u64 n) {
        switch (s.kind) {
            case Kind::Game:
                return game.storage->Read2(out, game.offset + s.offset + rel, n);
            case Kind::Bin:
                return ReadFull(bin, s.offset + rel, out, n);
            case Kind::Loose:
                if (cur_index != s.offset) {
                    cur.Close();
                    cur_index = UINT64_MAX;
                    R_TRY(sd.OpenFile(loose[s.offset], FsOpenMode_Read, &cur));
                    cur_index = s.offset;
                }
                return ReadFull(cur, rel, out, n);
        }
        R_THROW(Result_RepackBadRomfs);
    }

    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        std::scoped_lock lock{mutex};
        *bytes_read = 0;
        const auto total = static_cast<s64>(layout.total_size);
        size = std::min<s64>(size, off < total ? total - off : 0);

        auto out = static_cast<u8*>(buf);
        u64 pos = off;
        u64 left = size;
        while (left) {
            u64 n;
            if (pos < romfs_pack::DATA_OFFSET) {
                n = std::min<u64>(left, romfs_pack::DATA_OFFSET - pos);
                std::memcpy(out, layout.header.data() + pos, n);
            } else if (pos >= layout.tables_offset) {
                const auto rel = pos - layout.tables_offset;
                n = left;
                if (rel < layout.tables.size()) {
                    n = std::min<u64>(left, layout.tables.size() - rel);
                    std::memcpy(out, layout.tables.data() + rel, n);
                } else {
                    std::memset(out, 0, n);
                }
            } else {
                // the last file that starts at or before pos; past its end is alignment padding.
                auto it = std::upper_bound(files.begin(), files.end(), pos, [](u64 p, const Source& s) { return p < s.data; });
                const auto next = it == files.end() ? layout.tables_offset : it->data;
                if (it != files.begin() && pos < std::prev(it)->data + std::prev(it)->size) {
                    const auto& s = *std::prev(it);
                    n = std::min<u64>(left, s.data + s.size - pos);
                    R_TRY(ReadSource(s, pos - s.data, out, n));
                } else {
                    n = std::min<u64>(left, next - pos);
                    std::memset(out, 0, n);
                }
            }
            out += n;
            pos += n;
            left -= n;
        }
        *bytes_read = size;
        R_SUCCEED();
    }

    Result GetSize(s64* out) override {
        *out = layout.total_size;
        R_SUCCEED();
    }
};

} // namespace

Result OpenModdedRomfs(ui::ProgressBox* pbox, const ProgramRomfs& game, const fs::FsPath& contents_dir, std::shared_ptr<yati::source::Base>& out, ModCount* out_count) {
    auto img = std::make_shared<Image>();
    img->game = game;

    std::vector<std::string> paths;
    std::vector<Source> srcs;
    std::unordered_map<std::string, size_t> index;
    ModCount count{};
    const auto put = [&](std::string path, Source s) {
        const auto [it, added] = index.try_emplace(path, srcs.size());
        if (added) {
            paths.push_back(std::move(path));
            srcs.push_back(s);
            count.added += s.kind != Kind::Game;
        } else {
            count.replaced += srcs[it->second].kind == Kind::Game;
            srcs[it->second] = s;
        }
    };

    std::vector<RomfsFile> listed;
    R_TRY(ListImage([&](void* buf, u64 off, u64 size) { return game.storage->Read2(buf, game.offset + off, size); }, listed));
    for (auto& f : listed) {
        R_UNLESS(f.offset + f.size <= game.size, Result_RepackBadRomfs);
        put(std::move(f.path), {Kind::Game, f.offset, f.size});
    }

    const auto bin_path = fs::AppendPath(contents_dir, "romfs.bin");
    if (img->sd.FileExists(bin_path)) {
        R_TRY(img->sd.OpenFile(bin_path, FsOpenMode_Read, &img->bin));
        R_TRY(ListImage([&](void* buf, u64 off, u64 size) { return ReadFull(img->bin, off, buf, size); }, listed));
        for (auto& f : listed) {
            put(std::move(f.path), {Kind::Bin, f.offset, f.size});
        }
    }

    const auto romfs_dir = fs::AppendPath(contents_dir, "romfs");
    if (img->sd.DirExists(romfs_dir)) {
        std::vector<romfs_pack::Entry> loose;
        R_TRY(romfs_pack::WalkDir(pbox, img->sd, romfs_dir, loose));
        for (auto& e : loose) {
            if (!e.is_dir) {
                img->loose.push_back(fs::AppendPath(romfs_dir, e.path));
                put(std::move(e.path), {Kind::Loose, img->loose.size() - 1, e.size});
            }
        }
    }

    std::vector<romfs_pack::Entry> entries;
    entries.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); i++) {
        entries.push_back({paths[i], srcs[i].size, false});
    }
    img->layout = romfs_pack::BuildLayout(entries);
    for (size_t i = 0; i < srcs.size(); i++) {
        srcs[i].data = img->layout.data_offsets[i];
    }
    std::sort(srcs.begin(), srcs.end(), [](const auto& a, const auto& b) { return a.data < b.data; });
    img->files = std::move(srcs);

    log_write("[repack] romfs: %zu files, %u replaced, %u added, %lu bytes\n", img->files.size(), count.replaced, count.added, img->layout.total_size);
    if (out_count) {
        *out_count = count;
    }
    out = std::move(img);
    R_SUCCEED();
}

} // namespace sphaira::repack
