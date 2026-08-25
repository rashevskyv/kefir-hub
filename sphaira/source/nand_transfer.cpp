#include "nand_transfer.hpp"

#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <switch/services/pm.h>

namespace sphaira::nand_transfer {
namespace {

constexpr u64 TID_BCAT = 0x010000000000000CULL;
constexpr u64 TID_ACCOUNT = 0x010000000000001EULL;
constexpr u64 TID_PDM = 0x010000000000002DULL;
constexpr u64 TID_OLSC = 0x010000000000003EULL;
constexpr u64 TID_NS = 0x0100000000000005ULL;

struct SaveSpec {
    u64 id;
    const char* hex;
    bool kill_account;
    bool kill_pdm;
    bool required;
};

constexpr SaveSpec kSaves[] = {
    {0x8000000000000010ULL, "8000000000000010", true,  false, true},
    {0x8000000000000011ULL, "8000000000000011", true,  false, false},
    {0x80000000000000F0ULL, "80000000000000F0", false, true,  true},
    {0x8000000000000041ULL, "8000000000000041", false, true,  false},
};

auto Join(const std::string& dir, const char* name) -> std::string {
    if (dir.empty() || dir == "/") {
        return std::string("/") + name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

void Kill(bool account, bool pdm, bool ns) {
    if (R_FAILED(pmshellInitialize())) {
        return;
    }
    if (account) {
        pmshellTerminateProgram(TID_BCAT);
        pmshellTerminateProgram(TID_ACCOUNT);
        pmshellTerminateProgram(TID_OLSC);
    }
    if (pdm) {
        pmshellTerminateProgram(TID_PDM);
    }
    if (ns) {
        pmshellTerminateProgram(TID_NS);
    }
    pmshellExit();
    svcSleepThread(200000000);
}

auto TryOpen(u64 id) -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = id;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, true);
}

auto OpenForDump(const SaveSpec& spec) -> fs::FsNativeSave {
    auto save = TryOpen(spec.id);
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
        return save;
    }
    if (spec.kill_account) {
        Kill(true, false, false);
        save = TryOpen(spec.id);
        if (R_SUCCEEDED(save.GetFsOpenResult())) {
            return save;
        }
    }
    if (spec.kill_pdm) {
        Kill(false, true, false);
        save = TryOpen(spec.id);
        if (R_SUCCEEDED(save.GetFsOpenResult())) {
            return save;
        }
        Kill(false, true, true);
        save = TryOpen(spec.id);
    }
    return save;
}

auto CopyTree(ui::ProgressBox* pbox, fs::Fs& from, const std::string& src, fs::Fs& to, const std::string& dst, u32& files) -> Result {
    if (pbox) {
        R_TRY(pbox->ShouldExitResult());
    }
    R_TRY(to.CreateDirectoryRecursively(dst.c_str()));

    fs::Dir d;
    auto rc = from.OpenDirectory(src.c_str(), FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d);
    if (R_FAILED(rc) && (src.empty() || src == "/")) {
        rc = from.OpenDirectory("", FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d);
    }
    R_TRY(rc);

    std::vector<FsDirectoryEntry> ents;
    R_TRY(d.ReadAll(ents));
    for (const auto& e : ents) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        const auto child_src = Join(src, e.name);
        const auto child_dst = dst + "/" + e.name;
        if (e.type == FsDirEntryType_Dir) {
            R_TRY(CopyTree(pbox, from, child_src, to, child_dst, files));
            continue;
        }
        if (pbox) {
            pbox->NewTransfer(e.name);
            R_TRY(pbox->CopyFile(&from, &to, child_src.c_str(), child_dst.c_str()));
        } else {
            std::vector<u8> data;
            R_TRY(from.read_entire_file(child_src.c_str(), data));
            R_TRY(to.write_entire_file(child_dst.c_str(), data));
        }
        files++;
    }
    R_SUCCEED();
}

void WriteScript(fs::Fs& sd, const std::string& pack_dir) {
    std::vector<u8> te;
    if (R_FAILED(fs::read_entire_file("romfs:/tegra/nand_transfer_restore.te", te)) || te.empty()) {
        log_write("[NAND] restore.te missing from romfs\n");
        return;
    }
    sd.write_entire_file((pack_dir + "/restore.te").c_str(), te);
    sd.CreateDirectoryRecursively("/config/kefir/nand_transfer");
    sd.write_entire_file("/config/kefir/nand_transfer/restore.te", te);
}

void WriteReadme(fs::Fs& sd, const std::string& pack_dir) {
    const char* text =
        "Kefir Hub NAND transfer pack\n"
        "\n"
        "These folders are DECRYPTED inner files of system saves, not the\n"
        "encrypted SYSTEM:/save blobs.\n"
        "\n"
        "Restore on the DESTINATION console:\n"
        "1. Copy this whole folder onto the destination SD.\n"
        "2. Inject TegraExplorer (keys must already be dumped on that SD).\n"
        "3. Open this folder and run restore.te\n"
        "4. Pick emuMMC SYSTEM or sysMMC SYSTEM.\n"
        "5. The script writes the files into the destination's existing saves\n"
        "   and commit()s them (encrypts/signs with destination keys).\n"
        "6. Reboot CFW.\n"
        "\n"
        "Do NOT copy 80000000000000F0 as a raw NAND file. That bricks emuMMC.\n"
        "Destination play hours (and profiles, if 0010 is present) are replaced.\n"
        "Back up the destination SYSTEM partition first.\n"
        "Do not restore 0120/0121/0124.\n";
    const std::vector<u8> body(text, text + std::strlen(text));
    sd.write_entire_file((pack_dir + "/README.txt").c_str(), body);
}

void WriteManifest(fs::Fs& sd, const std::string& pack_dir, const Report& report) {
    char json[512]{};
    std::snprintf(json, sizeof(json),
        "{\"kind\":\"kefir-nand-transfer\",\"version\":1,"
        "\"save_0010\":%s,\"save_0011\":%s,\"save_00F0\":%s,\"save_0041\":%s}\n",
        report.save_0010 ? "true" : "false",
        report.save_0011 ? "true" : "false",
        report.save_00F0 ? "true" : "false",
        report.save_0041 ? "true" : "false");
    const std::string s = json;
    sd.write_entire_file((pack_dir + "/manifest.json").c_str(),
        std::vector<u8>(s.begin(), s.end()));
}

void SetFlag(Report& report, const char* hex, bool ok) {
    if (!std::strcmp(hex, "8000000000000010")) {
        report.save_0010 = ok;
    } else if (!std::strcmp(hex, "8000000000000011")) {
        report.save_0011 = ok;
    } else if (!std::strcmp(hex, "80000000000000F0")) {
        report.save_00F0 = ok;
    } else if (!std::strcmp(hex, "8000000000000041")) {
        report.save_0041 = ok;
    }
}

} // namespace

auto Export(ui::ProgressBox* pbox, Report& out) -> Result {
    out = {};
    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    out.dir = paths::DATA_ROOT + "/nand_transfer/" + stamp;
    R_TRY(sd.CreateDirectoryRecursively(out.dir.c_str()));

    u32 dumped{};
    for (const auto& spec : kSaves) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
            pbox->NewTransfer(spec.hex);
        }
        auto save = OpenForDump(spec);
        if (R_FAILED(save.GetFsOpenResult())) {
            log_write("[NAND] open %s failed 0x%X\n", spec.hex, save.GetFsOpenResult());
            SetFlag(out, spec.hex, false);
            continue;
        }
        u32 files{};
        const auto dst = out.dir + "/" + spec.hex;
        const auto rc = CopyTree(pbox, save, "/", sd, dst, files);
        if (R_FAILED(rc) || !files) {
            log_write("[NAND] dump %s failed rc=0x%X files=%u\n", spec.hex, rc, files);
            SetFlag(out, spec.hex, false);
            continue;
        }
        SetFlag(out, spec.hex, true);
        dumped++;
        log_write("[NAND] dump %s files=%u\n", spec.hex, files);
    }

    WriteScript(sd, out.dir);
    WriteReadme(sd, out.dir);
    WriteManifest(sd, out.dir, out);

    R_UNLESS(dumped > 0, Result_FsEmpty);
    R_UNLESS(out.save_0010 || out.save_00F0, Result_FsEmpty);
    log_write("[NAND] pack %s 0010=%d 0011=%d F0=%d 41=%d\n",
        out.dir.c_str(), out.save_0010, out.save_0011, out.save_00F0, out.save_0041);
    R_SUCCEED();
}

} // namespace sphaira::nand_transfer
