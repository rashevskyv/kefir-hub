#include "romfs_pack.hpp"
#include "romfs_pack_layout.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace sphaira::romfs_pack {
namespace {

// left free for the system (saves, logs), as zero_fill does.
constexpr s64 RESERVE = 64LL * 1024 * 1024;
// one write per MiB: tens of thousands of small mod files are not one write each.
constexpr u64 CHUNK_SIZE = 1024 * 1024;
constexpr char BIN[] = "romfs.bin";
// fs.mitm only looks for the exact name romfs.bin: a half-written file is never used.
constexpr char PART[] = "romfs.bin.part";

struct Title {
    fs::FsPath dir;
    s64 size{}; // of romfs.bin; 0 = nothing to pack
    std::string skip{}; // why it is not packed
};

struct Report {
    std::vector<std::string> lines{};
    s64 need{}; // set when the forecast says the card is too small: nothing was packed
    s64 free{};
};

auto Size(s64 bytes) -> std::string {
    return ui::menu::grid::FormatBytes(static_cast<u64>(std::max<s64>(0, bytes)));
}

auto Name(const fs::FsPath& dir) -> std::string {
    const auto s = dir.toString();
    return s.substr(s.rfind('/') + 1);
}

// every file and folder under `root`, paths relative to it.
auto Walk(ui::ProgressBox* pbox, fs::Fs& fs, const fs::FsPath& root, std::vector<Entry>& out) -> Result {
    std::vector<std::string> stack{""};
    std::vector<FsDirectoryEntry> buf(64);
    while (!stack.empty()) {
        R_TRY(pbox->ShouldExitResult());
        const auto rel = std::move(stack.back());
        stack.pop_back();

        fs::Dir dir;
        R_TRY(fs.OpenDirectory(rel.empty() ? root : fs::AppendPath(root, rel), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir));
        for (;;) {
            s64 count{};
            R_TRY(dir.Read(&count, buf.size(), buf.data()));
            if (count <= 0) {
                break;
            }
            for (s64 i = 0; i < count; i++) {
                const auto& e = buf[i];
                auto path = rel.empty() ? std::string{e.name} : rel + "/" + e.name;
                if (e.type == FsDirEntryType_Dir) {
                    stack.push_back(path);
                    out.push_back({std::move(path), 0, true});
                } else {
                    out.push_back({std::move(path), static_cast<u64>(e.file_size), false});
                }
            }
        }
    }
    R_SUCCEED();
}

auto Scan(ui::ProgressBox* pbox, fs::Fs& fs, const fs::FsPath& dir, Title& out) -> Result {
    out = Title{dir};
    const auto romfs = fs::AppendPath(dir, "romfs");
    if (!fs.DirExists(romfs)) {
        out.skip = "no romfs folder"_i18n;
        R_SUCCEED();
    }
    // the old image may hold files the folder has not: overwriting it would lose them.
    if (fs.FileExists(fs::AppendPath(dir, BIN))) {
        out.skip = "romfs.bin already exists"_i18n;
        R_SUCCEED();
    }

    std::vector<Entry> entries;
    R_TRY(Walk(pbox, fs, romfs, entries));
    if (std::none_of(entries.begin(), entries.end(), [](auto& e) { return !e.is_dir; })) {
        out.skip = "the romfs folder has no files"_i18n;
        R_SUCCEED();
    }
    out.size = static_cast<s64>(BuildLayout(entries).total_size);
    R_SUCCEED();
}

auto WriteImage(ui::ProgressBox* pbox, fs::File& f, const fs::FsPath& romfs, fs::Fs& fs, const std::vector<Entry>& entries, const Layout& layout) -> Result {
    R_TRY(f.Write(0, layout.header.data(), layout.header.size(), FsWriteOption_None));

    // file data in image order, gathered into one buffer: the card sees large sequential writes.
    std::vector<size_t> order;
    s64 data_total{};
    for (size_t i = 0; i < entries.size(); i++) {
        if (!entries[i].is_dir) {
            order.push_back(i);
            data_total += entries[i].size;
        }
    }
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return layout.data_offsets[a] < layout.data_offsets[b]; });

    std::vector<u8> buf(CHUNK_SIZE);
    u64 base = DATA_OFFSET; // image offset of buf[0]
    u64 used = 0;
    s64 done = 0;
    const auto flush = [&]() -> Result {
        R_TRY(f.Write(base, buf.data(), used, FsWriteOption_None));
        base += used;
        used = 0;
        R_SUCCEED();
    };

    pbox->UpdateTransfer(0, data_total);
    for (const auto i : order) {
        R_TRY(pbox->ShouldExitResult());
        const auto& e = entries[i];

        // alignment padding before the file, under 0x10 bytes.
        while (base + used < layout.data_offsets[i]) {
            if (used == CHUNK_SIZE) {
                R_TRY(flush());
            }
            buf[used++] = 0;
        }

        fs::File src;
        R_TRY(fs.OpenFile(fs::AppendPath(romfs, e.path), FsOpenMode_Read, &src));
        s64 now_size{};
        R_TRY(src.GetSize(&now_size));
        R_UNLESS(static_cast<u64>(now_size) == e.size, Result_RomfsPackSourceChanged);

        for (u64 off = 0; off < e.size; ) {
            if (used == CHUNK_SIZE) {
                R_TRY(flush());
            }
            const auto want = std::min<u64>(e.size - off, CHUNK_SIZE - used);
            u64 got{};
            R_TRY(src.Read(off, buf.data() + used, want, FsReadOption_None, &got));
            R_UNLESS(got > 0, Result_RomfsPackSourceChanged);
            used += got;
            off += got;
            done += got;
            pbox->UpdateTransfer(done, data_total);
        }
    }
    if (used) {
        R_TRY(flush());
    }

    R_TRY(f.Write(layout.tables_offset, layout.tables.data(), layout.tables.size(), FsWriteOption_None));
    R_SUCCEED();
}

auto PackOne(ui::ProgressBox* pbox, fs::Fs& fs, const Title& t, bool delete_source) -> Result {
    const auto romfs = fs::AppendPath(t.dir, "romfs");
    const auto part = fs::AppendPath(t.dir, PART);

    pbox->NewTransfer("Counting files"_i18n);
    std::vector<Entry> entries;
    R_TRY(Walk(pbox, fs, romfs, entries));
    const auto layout = BuildLayout(entries);

    // a leftover of a run cut off by a power loss.
    fs.DeleteFile(part);
    // full size up front: the space is taken before the first byte is copied.
    R_TRY(fs.CreateFile(part, layout.total_size));
    bool renamed{};
    ON_SCOPE_EXIT(if (!renamed) { fs.DeleteFile(part); });

    {
        fs::File f;
        R_TRY(fs.OpenFile(part, FsOpenMode_Write, &f));
        pbox->NewTransfer("Packing"_i18n);
        R_TRY(WriteImage(pbox, f, romfs, fs, entries, layout));
    } // closed before the rename

    R_TRY(fs.RenameFile(part, fs::AppendPath(t.dir, BIN)));
    renamed = true;
    log_write("[ROMFS] packed %s: %zu entries, %llu bytes\n", t.dir.toString().c_str(), entries.size(), (unsigned long long)layout.total_size);

    // only now: if this is cut off, the loose files left over hold the same data as romfs.bin.
    if (delete_source) {
        pbox->NewTransfer("Deleting "_i18n + romfs.toString());
        R_TRY(fs.DeleteDirectoryRecursively(romfs));
    }
    R_SUCCEED();
}

auto Run(ui::ProgressBox* pbox, const std::vector<fs::FsPath>& dirs, bool delete_source, Report& report) -> Result {
    fs::FsNativeSd fs;
    pbox->SetHideSpeed(true);

    std::vector<Title> titles(dirs.size());
    for (size_t i = 0; i < dirs.size(); i++) {
        if (dirs.size() > 1) {
            pbox->SetOverall(i, dirs.size());
        }
        pbox->SetTitle(Name(dirs[i]));
        pbox->NewTransfer("Counting files"_i18n);
        R_TRY(Scan(pbox, fs, dirs[i], titles[i]));
    }

    // forecast before anything is written. With deletion, packing the largest title first and
    // then removing its folder frees at least what each smaller title needs, so the largest
    // image alone decides. Without deletion every image stays on the card.
    s64 need{};
    for (const auto& t : titles) {
        need = delete_source ? std::max(need, t.size) : need + t.size;
    }
    s64 free{};
    R_TRY(fs.GetFreeSpace("/", &free));
    log_write("[ROMFS] forecast need %lld free %lld delete %d\n", (long long)need, (long long)free, delete_source);
    if (need && need + RESERVE > free) {
        report.need = need + RESERVE;
        report.free = free;
        R_SUCCEED();
    }

    std::stable_sort(titles.begin(), titles.end(), [](auto& a, auto& b) { return a.size > b.size; });
    pbox->SetHideSpeed(false);
    for (size_t i = 0; i < titles.size(); i++) {
        const auto& t = titles[i];
        const auto name = Name(t.dir);
        if (!t.size) {
            report.lines.push_back(name + ": " + t.skip);
            continue;
        }
        if (titles.size() > 1) {
            pbox->SetOverall(i, titles.size());
        }
        pbox->SetTitle(name);

        // checked again per title: the forecast assumes the card is not written meanwhile.
        R_TRY(fs.GetFreeSpace("/", &free));
        if (t.size + RESERVE > free) {
            report.lines.push_back(name + ": " + "not enough free space"_i18n + " (" + Size(t.size) + ")");
            continue;
        }

        const auto rc = PackOne(pbox, fs, t, delete_source);
        if (rc == Result_TransferCancelled) {
            report.lines.push_back(name + ": " + "cancelled"_i18n);
            return rc;
        }
        if (R_FAILED(rc)) {
            char code[16];
            std::snprintf(code, sizeof(code), "0x%X", rc);
            report.lines.push_back(name + ": " + "failed"_i18n + " " + code);
            continue;
        }
        report.lines.push_back(name + ": " + "packed"_i18n + " (" + Size(t.size) + ")");
    }
    R_SUCCEED();
}

void ShowReport(Result rc, const Report& report) {
    if (report.need) {
        App::Push<ui::OptionBox>("Not enough free space on the memory card. Nothing was packed."_i18n + "\n" +
            "Needed: "_i18n + Size(report.need) + "\n" + "Free: "_i18n + Size(report.free), "OK"_i18n);
        return;
    }
    if (report.lines.empty()) {
        if (rc == Result_TransferCancelled) {
            return;
        }
        App::PushErrorBox(rc, "Packing failed!"_i18n);
        return;
    }
    // "select all" in atmosphere/contents can be a hundred titles: a list scrolls, a box does not.
    if (report.lines.size() > 8) {
        App::Push<ui::PopupList>("Packing"_i18n, report.lines, [](auto){});
        return;
    }
    std::string text;
    for (const auto& line : report.lines) {
        text += line + "\n";
    }
    text.pop_back();
    App::Push<ui::OptionBox>(text, "OK"_i18n);
}

} // namespace

auto CanPack(const fs::FsPath& title_dir) -> bool {
    fs::FsNativeSd fs;
    return fs.DirExists(fs::AppendPath(title_dir, "romfs")) && !fs.FileExists(fs::AppendPath(title_dir, BIN));
}

void PackTitles(std::vector<fs::FsPath> title_dirs, std::function<void()> on_done) {
    App::Push<ui::OptionBox>(
        "Pack the romfs folders into romfs.bin?\nAtmosphere then reads one file instead of thousands. Files left in the romfs folder take priority over romfs.bin."_i18n,
        "Back"_i18n, "Keep folders"_i18n, "Delete folders"_i18n, 2, [title_dirs, on_done](auto op_index){
            if (!op_index || !*op_index) {
                return;
            }
            const bool delete_source = *op_index == 2;
            auto report = std::make_shared<Report>();
            App::Push<ui::ProgressBox>(0, "Packing"_i18n, "", [title_dirs, delete_source, report](auto pbox) -> Result {
                return Run(pbox, title_dirs, delete_source, *report);
            }, [report, on_done](Result rc){
                ShowReport(rc, *report);
                if (on_done) {
                    on_done();
                }
            });
        });
}

} // namespace sphaira::romfs_pack
