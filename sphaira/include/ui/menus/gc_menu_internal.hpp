#pragma once

#include "ui/menus/gc_menu.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"

#include "yati/yati.hpp"
#include "yati/nx/nca.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "download.hpp"
#include "dumper.hpp"
#include "image.hpp"
#include "title_info.hpp"
#include "storage_ratio.hpp"
#include "utils/utils.hpp"

#include <cstring>
#include <algorithm>
#include <span>
#include <vector>

namespace sphaira::ui::menu::gc {

constexpr u32 XCI_MAGIC = std::byteswap(0x48454144);
constexpr u32 REMOUNT_ATTEMPT_MAX = 8; // same as nxdumptool.

enum DumpFileType {
    DumpFileType_XCI,
    DumpFileType_TrimmedXCI,
    DumpFileType_Set,
    DumpFileType_UID,
    DumpFileType_Cert,
    DumpFileType_Initial,
};

enum DumpFileFlag {
    DumpFileFlag_XCI = 1 << 0,
    DumpFileFlag_Set = 1 << 1,
    DumpFileFlag_UID = 1 << 2,
    DumpFileFlag_Cert = 1 << 3,
    DumpFileFlag_Initial = 1 << 4,

    DumpFileFlag_AllBin = DumpFileFlag_Set | DumpFileFlag_UID | DumpFileFlag_Cert | DumpFileFlag_Initial,
    DumpFileFlag_All = DumpFileFlag_XCI | DumpFileFlag_AllBin,
};

inline const char *g_option_list[] = {
    "Install",
    "Dump",
    "Exit",
};

inline auto GetXciSizeFromRomSize(u8 rom_size) -> s64 {
    switch (rom_size) {
        case 0xFA: return 1024ULL * 1024ULL * 1024ULL * 1ULL;
        case 0xF8: return 1024ULL * 1024ULL * 1024ULL * 2ULL;
        case 0xF0: return 1024ULL * 1024ULL * 1024ULL * 4ULL;
        case 0xE0: return 1024ULL * 1024ULL * 1024ULL * 8ULL;
        case 0xE1: return 1024ULL * 1024ULL * 1024ULL * 16ULL;
        case 0xE2: return 1024ULL * 1024ULL * 1024ULL * 32ULL;
    }
    return 0;
}

inline auto GetDumpTypeStr(u8 type) -> const char* {
    switch (type) {
        case DumpFileType_TrimmedXCI:
            if (App::GetApp()->m_dump_label_trim_xci.Get()) {
                return " (trimmed).xci";
            } [[fallthrough]];

        case DumpFileType_XCI: return ".xci";
        case DumpFileType_Set: return " (Card ID Set).bin";
        case DumpFileType_UID: return " (Card UID).bin";
        case DumpFileType_Cert: return " (Certificate).bin";
        case DumpFileType_Initial: return " (Initial Data).bin";
    }

    return "";
}

inline auto BuildXciName(const ApplicationEntry& e) -> fs::FsPath {
    fs::FsPath name_buf = e.lang_entry.name;
    title::utilsReplaceIllegalCharacters(name_buf, true);

    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s [%016lX][v%u]", name_buf.s, e.app_id, e.version);
    return path;
}

inline auto BuildXciBasePath(std::span<const ApplicationEntry> entries) -> fs::FsPath {
    fs::FsPath path;
    for (s64 i = 0; i < std::size(entries); i++) {
        if (i) {
            path += " + ";
        }
        path += BuildXciName(entries[i]);
    }

    return path;
}

#if 0
// builds path suiteable for usb transfer.
auto BuildFilePath(DumpFileType type, std::span<const ApplicationEntry> entries) -> fs::FsPath {
    return BuildXciBasePath(entries) + GetDumpTypeStr(type);
}
#endif

// builds path suiteable for file dumps.
inline auto BuildFullDumpPath(DumpFileType type, std::span<const ApplicationEntry> entries) -> fs::FsPath {
    const auto base_path = BuildXciBasePath(entries);
    fs::FsPath out;

    if (App::GetApp()->m_dump_app_folder.Get()) {
        if (App::GetApp()->m_dump_append_folder_with_xci.Get()) {
            out = base_path + ".xci/" + base_path + GetDumpTypeStr(type);
        } else {
            out = base_path + "/" + base_path + GetDumpTypeStr(type);
        }
    } else {
        out = base_path + GetDumpTypeStr(type);
    }

    return fs::AppendPath("/dumps/Gamecard", out);
}

// @Gc is the mount point, S is for secure partion, the remaining is the
// the gamecard handle value in lower-case hex.
inline auto BuildGcPath(const char* name, const FsGameCardHandle* handle, FsGameCardPartition partiton = FsGameCardPartition_Secure) -> fs::FsPath {
    static const char mount_parition[] = {
        [FsGameCardPartition_Update] = 'U',
        [FsGameCardPartition_Normal] = 'N',
        [FsGameCardPartition_Secure] = 'S',
        [FsGameCardPartition_Logo] = 'L',
    };

    fs::FsPath path;
    std::snprintf(path, sizeof(path), "@Gc%c%08x://%s", mount_parition[partiton], handle->value, name);
    return path;
}

struct XciSource final : dump::BaseSource {
    // application name.
    std::string application_name{};
    // extra
    std::vector<u8> id_set{};
    std::vector<u8> uid{};
    std::vector<u8> cert{};
    std::vector<u8> initial{};
    // size of the entire xci.
    s64 xci_size{};
    Menu* menu{};
    int icon{};

    Result Read(const std::string& path, void* buf, s64 off, s64 size, u64* bytes_read) override {
        if (path.ends_with(GetDumpTypeStr(DumpFileType_XCI))) {
            size = ClipSize(off, size, xci_size);
            *bytes_read = size;
            return menu->GcStorageRead(buf, off, size);
        } else {
            std::span<const u8> span;
            if (path.ends_with(GetDumpTypeStr(DumpFileType_Set))) {
                span = id_set;
            } else if (path.ends_with(GetDumpTypeStr(DumpFileType_UID))) {
                span = uid;
            } else if (path.ends_with(GetDumpTypeStr(DumpFileType_Cert))) {
                span = cert;
            } else if (path.ends_with(GetDumpTypeStr(DumpFileType_Initial))) {
                span = initial;
            }

            R_UNLESS(!span.empty(), Result_GcBadReadForDump);

            size = ClipSize(off, size, span.size());
            *bytes_read = size;

            std::memcpy(buf, span.data() + off, size);
            R_SUCCEED();
        }
    }

    auto GetName(const std::string& path) const -> std::string override {
        return application_name;
    }

    auto GetSize(const std::string& path) const -> s64 override {
        if (path.ends_with(GetDumpTypeStr(DumpFileType_XCI))) {
            return xci_size;
        } else if (path.ends_with(GetDumpTypeStr(DumpFileType_Set))) {
            return id_set.size();
        } else if (path.ends_with(GetDumpTypeStr(DumpFileType_UID))) {
            return uid.size();
        } else if (path.ends_with(GetDumpTypeStr(DumpFileType_Cert))) {
            return cert.size();
        } else if (path.ends_with(GetDumpTypeStr(DumpFileType_Initial))) {
            return initial.size();
        }
        return 0;
    }

    auto GetIcon(const std::string& path) const -> int override {
        return icon;
    }

private:
    static auto InRange(s64 off, s64 offset, s64 size) -> bool {
        return off < offset + size && off >= offset;
    }

    static auto ClipSize(s64 off, s64 size, s64 file_size) -> s64 {
        return std::min(size, file_size - off);
    }
};



// from Gamecard-Installer-NX
inline Result fsOpenGameCardStorage(FsStorage* out, const FsGameCardHandle* handle, FsGameCardPartitionRaw partition) {
    const struct {
        FsGameCardHandle handle;
        u32 partition;
    } in = { *handle, (u32)partition };

    return serviceDispatchIn(fsGetServiceSession(), 30, in, .out_num_objects = 1, .out_objects = &out->s);
}

inline Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier* out) {
    return serviceDispatch(fsGetServiceSession(), 501,
        .out_num_objects = 1,
        .out_objects = &out->s
    );
}

struct GcSource final : yati::source::Base {
    GcSource(const ApplicationEntry& entry, fs::FsNativeGameCard* fs);
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read);

    yati::container::Collections m_collections{};
    yati::ConfigOverride m_config{};
    fs::FsNativeGameCard* m_fs{};
    fs::File m_file{};
    s64 m_offset{};
    s64 m_size{};

private:
    static auto InRange(s64 off, s64 offset, s64 size) -> bool {
        return off < offset + size && off >= offset;
    }
};

inline GcSource::GcSource(const ApplicationEntry& entry, fs::FsNativeGameCard* fs)
: m_fs{fs} {
    m_offset = -1;

    s64 offset{};
    const auto add_collections = [&](const auto& collections) {
        for (auto collection : collections) {
            collection.offset = offset;
            m_collections.emplace_back(collection);
            offset += collection.size;
        }
    };

    const auto add_entries = [&](const auto& entries) {
        for (auto& e : entries) {
            add_collections(e);
        }
    };

    // yati can handle all of this for use, however, yati lacks information
    // for ncas until it installs the cnmt and parses it.
    // as we already have this info, we can only send yati what we want to install.
    if (App::GetApp()->m_ticket_only.Get()) {
        add_collections(entry.tickets);
    } else {
        if (!App::GetApp()->m_skip_base.Get()) {
            add_entries(entry.application);
        }
        if (!App::GetApp()->m_skip_patch.Get()) {
            add_entries(entry.patch);
        }
        if (!App::GetApp()->m_skip_addon.Get()) {
            add_entries(entry.add_on);
        }
        if (!App::GetApp()->m_skip_data_patch.Get()) {
            add_entries(entry.data_patch);
        }
        if (!App::GetApp()->m_skip_ticket.Get()) {
            add_collections(entry.tickets);
        }
    }

    // we don't need to verify the nca's, this speeds up installs.
    m_config.skip_nca_hash_verify = true;
    m_config.skip_rsa_header_fixed_key_verify = true;
    m_config.skip_rsa_npdm_fixed_key_verify = true;
}

inline Result GcSource::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    // check is we need to open a new file.
    if (!InRange(off, m_offset, m_size)) {
        m_file.Close();

        // find new file based on the offset.
        bool found = false;
        for (auto& collection : m_collections) {
            if (InRange(off, collection.offset, collection.size)) {
                found = true;
                m_offset = collection.offset;
                m_size = collection.size;
                R_TRY(m_fs->OpenFile(fs::AppendPath("/", collection.name), FsOpenMode_Read, &m_file));
                break;
            }
        }

        // this will never fail, unless i break something in yati.
        R_UNLESS(found, Result_GcBadReadForDump);
    }

    return m_file.Read(off - m_offset, buf, size, 0, bytes_read);
}


} // namespace sphaira::ui::menu::gc
