#include "ui/menus/gc_menu_internal.hpp"
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

namespace sphaira::ui::menu::gc {

Result Menu::GcMount() {
    GcUnmount();

    // after storage has been mounted, it will take X attempts to mount
    // the fs, same as mounting storage.
    for (u32 i = 0; i < REMOUNT_ATTEMPT_MAX; i++) {
        R_TRY(fsDeviceOperatorGetGameCardHandle(std::addressof(m_dev_op), std::addressof(m_handle)));
        m_fs = std::make_unique<fs::FsNativeGameCard>(std::addressof(m_handle), FsGameCardPartition_Secure);
        if (R_SUCCEEDED(m_fs->GetFsOpenResult())) {
            break;
        }
    }

    R_TRY(m_fs->GetFsOpenResult());

    fs::Dir dir;
    R_TRY(m_fs->OpenDirectory("/", FsDirOpenMode_ReadFiles, std::addressof(dir)));

    std::vector<FsDirectoryEntry> buf;
    R_TRY(dir.ReadAll(buf));

    yati::container::Collections ticket_collections;
    for (const auto& e : buf) {
        if (!std::string_view(e.name).ends_with(".tik") && !std::string_view(e.name).ends_with(".cert")) {
            continue;
        }

        ticket_collections.emplace_back(e.name, 0, e.file_size);
    }

    for (const auto& e : buf) {
        // we could use ncm to handle finding all the ncas for us
        // however, we can parse faster than ncm.
        // not only that, the first few calls trying to mount ncm db for
        // the gamecard will fail as it has not yet been parsed (or it's locked?).
        // we could, of course, just wait until ncm is ready, which is about
        // 32ms, but i already have code for manually parsing cnmt so lets re-use it.
        if (!std::string_view(e.name).ends_with(".cnmt.nca")) {
            continue;
        }

        // we don't yet use the header or extended header.
        ncm::PackagedContentMeta header;
        std::vector<u8> extended_header;
        std::vector<NcmPackagedContentInfo> infos;
        const auto path = BuildGcPath(e.name, &m_handle);
        R_TRY(nca::ParseCnmt(path, 0, header, extended_header, infos));

        u8 key_gen;
        FsRightsId rights_id;
        R_TRY(fsGetRightsIdAndKeyGenerationByPath(path, FsContentAttributes_All, &key_gen, &rights_id));

        // always add tickets, yati will ignore them if not needed.
        GcCollections collections;
        // add cnmt file.
        collections.emplace_back(e.name, e.file_size, NcmContentType_Meta, 0);

        for (const auto& packed_info : infos) {
            const auto& info = packed_info.info;
            // these don't exist for gamecards, however i may copy/paste this code
            // somewhere so i'm future proofing against myself.
            if (info.content_type == NcmContentType_DeltaFragment) {
                continue;
            }

            // find the nca file, this will never fail for gamecards, see above comment.
            const auto str = utils::hexIdToStr(info.content_id);
            const auto it = std::find_if(buf.cbegin(), buf.cend(), [str](auto& e){
                return !std::strncmp(str.str, e.name, std::strlen(str.str));
            });

            R_UNLESS(it != buf.cend(), Result_YatiNcaNotFound);
            collections.emplace_back(it->name, it->file_size, info.content_type, info.id_offset);
        }

        const auto app_id = ncm::GetAppId(header);
        ApplicationEntry* app_entry{};
        for (auto& app : m_entries) {
            if (app.app_id == app_id) {
                app_entry = &app;
                break;
            }
        }

        if (!app_entry) {
            app_entry = &m_entries.emplace_back(app_id, header.title_version);
        }

        app_entry->version = std::max(app_entry->version, header.title_version);
        app_entry->key_gen = std::max(app_entry->key_gen, key_gen);

        if (header.meta_type == NcmContentMetaType_Application) {
            app_entry->application.emplace_back(collections);
        } else if (header.meta_type == NcmContentMetaType_Patch) {
            app_entry->patch.emplace_back(collections);
        } else if (header.meta_type == NcmContentMetaType_AddOnContent) {
            app_entry->add_on.emplace_back(collections);
        } else if (header.meta_type == NcmContentMetaType_DataPatch) {
            app_entry->data_patch.emplace_back(collections);
        }
    }

    R_UNLESS(m_entries.size(), Result_GcEmptyGamecard);

    // append tickets to every application, yati will ignore if undeeded.
    for (auto& e : m_entries) {
        e.tickets = ticket_collections;
    }

    // load all control data, icons are loaded when displayed.
    for (auto& e : m_entries) {
        R_TRY(LoadControlData(e));
    }

    if (m_entries.size() > 1) {
        SetAction(Button::L2, Action{"Prev"_i18n, [this](){
            if (m_entry_index != 0) {
                OnChangeIndex(m_entry_index - 1);
            }
        }});
        SetAction(Button::R2, Action{"Next"_i18n, [this](){
            if (m_entry_index < m_entries.size()) {
                OnChangeIndex(m_entry_index + 1);
            }
        }});
    }

    OnChangeIndex(0);
    m_mounted = true;
    R_SUCCEED();
}

void Menu::GcUnmount() {
    GcUmountStorage();

    m_fs.reset();
    m_entries.clear();
    m_entry_index = 0;
    m_mounted = false;
    FreeImage();

    RemoveAction(Button::L2);
    RemoveAction(Button::R2);
}

Result Menu::GcMountStorage() {
    GcUmountStorage();

    R_TRY(GcMountPartition(FsGameCardPartitionRaw_Normal));
    R_TRY(fsStorageGetSize(&m_storage, &m_storage_full_size));

    u8 header[0x200];
    R_TRY(fsStorageRead(&m_storage, 0, header, sizeof(header)));

    u32 magic;
    u32 trim_size;
    u8 rom_size;
    std::memcpy(&magic, header + 0x100, sizeof(magic));
    std::memcpy(&rom_size, header + 0x10D, sizeof(rom_size));
    std::memcpy(&trim_size, header + 0x118, sizeof(trim_size));
    std::memcpy(&m_package_id, header + 0x110, sizeof(m_package_id));
    std::memcpy(m_initial_data_hash, header + 0x160, sizeof(m_initial_data_hash));
    R_UNLESS(magic == XCI_MAGIC, Result_GcBadXciMagic);

    // calculate the reported size, error if not found.
    m_storage_full_size = GetXciSizeFromRomSize(rom_size);
    log_write("[GC] m_storage_full_size: %zd rom_size: 0x%X\n", m_storage_full_size, rom_size);
    R_UNLESS(m_storage_full_size > 0, Result_GcBadXciRomSize);

    R_TRY(fsStorageGetSize(&m_storage, &m_parition_normal_size));
    R_TRY(GcMountPartition(FsGameCardPartitionRaw_Secure));
    R_TRY(fsStorageGetSize(&m_storage, &m_parition_secure_size));

    m_storage_trimmed_size = sizeof(header) + trim_size * 512ULL;
    m_storage_total_size = m_parition_normal_size + m_parition_secure_size;
    m_storage_mounted = true;

    log_write("[GC] m_storage_trimmed_size: %zd\n", m_storage_trimmed_size);
    log_write("[GC] m_storage_total_size: %zd\n", m_storage_total_size);

    R_SUCCEED();
}

void Menu::GcUmountStorage() {
    if (m_storage_mounted) {
        m_storage_mounted = false;
        GcUnmountPartition();
    }
}

Result Menu::GcMountPartition(FsGameCardPartitionRaw partition) {
    if (m_partition == partition) {
        R_SUCCEED();
    }

    GcUnmountPartition();

    // first attempt always fails due to qlaunch having the secure area mounted.
    // the 2nd attempt will succeeded, but qlaunch will fail to mount
    // the gamecard as it will only attempt to mount once.
    Result rc;
    for (u32 i = 0; i < REMOUNT_ATTEMPT_MAX; i++) {
        R_TRY(fsDeviceOperatorGetGameCardHandle(&m_dev_op, &m_handle));
        if (R_SUCCEEDED(rc = fsOpenGameCardStorage(&m_storage, &m_handle, partition))){
            break;
        }
    }

    m_partition = partition;
    return rc;
}

void Menu::GcUnmountPartition() {
    if (m_partition != FsGameCardPartitionRaw_None) {
        m_partition = FsGameCardPartitionRaw_None;
        fsStorageClose(&m_storage);
    }
}

Result Menu::GcStorageReadInternal(void* buf, s64 off, s64 size, u64* bytes_read) {
    if (off < m_parition_normal_size) {
        size = std::min<s64>(size, m_parition_normal_size - off);
        R_TRY(GcMountPartition(FsGameCardPartitionRaw_Normal));
    } else {
        off = off - m_parition_normal_size;
        R_TRY(GcMountPartition(FsGameCardPartitionRaw_Secure));
    }

    R_TRY(fsStorageRead(&m_storage, off, buf, size));
    *bytes_read = size;
    R_SUCCEED();
}

Result Menu::GcStorageRead(void* _buf, s64 off, s64 size) {
    auto buf = static_cast<u8*>(_buf);
    u64 bytes_read;
    u8 data[0x200];

    size = std::min(size, m_storage_total_size - off);
    if (size <= 0) {
        R_SUCCEED();
    }

    const auto unaligned_off = off % 0x200;
    off -= unaligned_off;
    if (size > 0 && unaligned_off) {
        R_TRY(GcStorageReadInternal(data, off, sizeof(data), &bytes_read));

        const auto csize = std::min<s64>(size, 0x200 - unaligned_off);
        std::memcpy(buf, data + unaligned_off, csize);
        off += bytes_read;
        size -= csize;
        buf += csize;
    }

    const auto unaligned_size = size % 0x200;
    size -= unaligned_size;
    while (size > 0) {
        R_TRY(GcStorageReadInternal(buf, off, size, &bytes_read));

        off += bytes_read;
        size -= bytes_read;
        buf += bytes_read;
    }

    if (unaligned_size) {
        R_TRY(GcStorageReadInternal(data, off, sizeof(data), &bytes_read));

        const auto csize = std::min<s64>(size, 0x200 - unaligned_size);
        std::memcpy(buf, data + unaligned_size, csize);
    }

    R_SUCCEED();
}

Result Menu::GcPoll(bool* inserted) {
    R_TRY(fsDeviceOperatorIsGameCardInserted(&m_dev_op, inserted));

    // if the handle changed, re-mount the game card.
    if (*inserted && m_mounted) {
        FsGameCardHandle handle;
        R_TRY(fsDeviceOperatorGetGameCardHandle(std::addressof(m_dev_op), std::addressof(handle)));
        if (handle.value != m_handle.value) {
            R_TRY(GcMount());
        }
    }

    R_SUCCEED();
}

Result Menu::GcOnEvent(bool force) {
    bool inserted{};
    R_TRY(GcPoll(&inserted));

    if (force || m_mounted != inserted) {
        log_write("gc state changed\n");
        m_mounted = inserted;
        if (m_mounted) {
            log_write("trying to mount\n");
            m_mounted = R_SUCCEEDED(GcMount());
            if (m_mounted) {
                App::PlaySoundEffect(SoundEffect::SoundEffect_Startup);
            }
        } else {
            log_write("trying to unmount\n");
            GcUnmount();
        }
    }

    R_SUCCEED();
}

Result Menu::UpdateStorageSize() {
    m_size_free_sd = 0;
    m_size_total_sd = 0;
    m_size_free_nand = 0;
    m_size_total_nand = 0;

    fs::FsNativeContentStorage fs_nand{FsContentStorageId_User};
    fs::FsNativeContentStorage fs_sd{FsContentStorageId_SdCard};

    R_TRY(fs_sd.GetFreeSpace("/", &m_size_free_sd));
    R_TRY(fs_sd.GetTotalSpace("/", &m_size_total_sd));
    R_TRY(fs_nand.GetFreeSpace("/", &m_size_free_nand));
    R_TRY(fs_nand.GetTotalSpace("/", &m_size_total_nand));
    R_SUCCEED();
}

void Menu::FreeImage() {
    if (m_icon) {
        nvgDeleteImage(App::GetVg(), m_icon);
        m_icon = 0;
    }
}

Result Menu::LoadControlData(ApplicationEntry& e) {
    const auto data = title::Get(e.app_id);
    R_UNLESS(data->status == title::NacpLoadStatus::Loaded, 0x1);

    e.icon = data->icon;
    e.lang_entry = data->lang;
    R_SUCCEED();
}

void Menu::OnChangeIndex(s64 new_index) {
    FreeImage();
    m_entry_index = new_index;

    if (m_entries.empty()) {
        this->SetSubHeading("No GameCard inserted");
    } else {
        const auto index = m_entries.empty() ? 0 : m_entry_index + 1;
        this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_entries.size()));

        const auto& e = m_entries[m_entry_index];

        TimeStamp ts;
        const auto image = ImageLoadFromMemory(e.icon, ImageFlag_JPEG);
        if (!image.data.empty()) {
            m_icon = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
        }
    }
}

Result Menu::DumpGames(u32 flags) {
    // first, try and mount the storage.
    // this will fill out the xci header, verify and get sizes.
    R_TRY(GcMountStorage());

    const auto do_dump = [this](u32 flags) -> Result {
        App::SetBoostMode(true);
        ON_SCOPE_EXIT(App::SetBoostMode(false));

        u32 location_flags = dump::DumpLocationFlag_All;

        // if we need to dump any of the bins, read fs memory until we find
        // what we are looking for.
        // the below code, along with the structs is taken from nxdumptool.
        GameCardSecurityInformation security_info;
        if ((flags &~ DumpFileFlag_XCI)) {
            location_flags &= ~dump::DumpLocationFlag_UsbS2S;
            R_TRY(GcGetSecurityInfo(security_info));
        }

        auto source = std::make_shared<XciSource>();
        source->menu = this;
        source->application_name = m_entries[m_entry_index].lang_entry.name;
        source->icon = m_icon;

        std::vector<fs::FsPath> paths;
        if (flags & DumpFileFlag_XCI) {
            if (App::GetApp()->m_dump_trim_xci.Get()) {
                source->xci_size = m_storage_trimmed_size;
                paths.emplace_back(BuildFullDumpPath(DumpFileType_TrimmedXCI, m_entries));
            } else {
                source->xci_size = m_storage_total_size;
                paths.emplace_back(BuildFullDumpPath(DumpFileType_XCI, m_entries));
            }
        }

        if (flags & DumpFileFlag_Set) {
            source->id_set.resize(sizeof(FsGameCardIdSet));
            R_TRY(fsDeviceOperatorGetGameCardIdSet(&m_dev_op, source->id_set.data(), source->id_set.size(), source->id_set.size()));
            paths.emplace_back(BuildFullDumpPath(DumpFileType_Set, m_entries));
        }

        if (flags & DumpFileFlag_UID) {
            source->uid.resize(sizeof(security_info.specific_data.card_uid));
            std::memcpy(source->uid.data(), &security_info.specific_data.card_uid, source->uid.size());
            paths.emplace_back(BuildFullDumpPath(DumpFileType_UID, m_entries));
        }

        if (flags & DumpFileFlag_Cert) {
            source->cert.resize(sizeof(security_info.certificate));
            std::memcpy(source->cert.data(), &security_info.certificate, source->cert.size());
            paths.emplace_back(BuildFullDumpPath(DumpFileType_Cert, m_entries));
        }

        if (flags & DumpFileFlag_Initial) {
            source->initial.resize(sizeof(security_info.initial_data));
            std::memcpy(source->initial.data(), &security_info.initial_data, source->initial.size());
            paths.emplace_back(BuildFullDumpPath(DumpFileType_Initial, m_entries));
        }

        dump::Dump(source, paths, [](Result){}, location_flags);

        R_SUCCEED();
    };

    // run some checks to see if the gamecard we can read past the trimmed size.
    // if we can, then this is a full / valid gamecard.
    // if it fails, it's likely a flashcart with a trimmed xci (will N check this?)
    bool is_trimmed = false;
    Result trim_rc = 0;
    if ((flags & DumpFileFlag_XCI) && m_storage_trimmed_size < m_storage_total_size) {
        const auto start_offset = std::min<s64>(0, m_storage_trimmed_size - 0x4000);
        // works on fw 1.2.0 and below.
        std::vector<u8> temp(1024*1024*1);
        if (R_FAILED(trim_rc = GcStorageRead(temp.data(), m_storage_trimmed_size, std::min<s64>(temp.size(), m_storage_total_size - start_offset)))) {
            log_write("[GC] WARNING1! GameCard is already trimmed: 0x%X FlashError: %u\n", trim_rc, trim_rc == 0x13D002);
            is_trimmed = true;
        }

        if (!is_trimmed) {
            // works on fw 1.2.0 and below.
            if (R_FAILED(trim_rc = GcStorageRead(temp.data(), m_storage_total_size - temp.size(), temp.size()))) {
                log_write("[GC] WARNING2! GameCard is already trimmed: 0x%X FlashError: %u\n", trim_rc, trim_rc == 0x13D002);
                is_trimmed = true;
            }
        }
    }

    // if trimmed and the user wants to dump the full xci, error.
    if ((flags & DumpFileFlag_XCI) && is_trimmed && App::GetApp()->m_dump_trim_xci.Get()) {
        App::Push<ui::OptionBox>(
            "WARNING: GameCard is already trimmed!"_i18n,
            "Back"_i18n, "Continue"_i18n, 0, [&](auto op_index){
                if (op_index && *op_index) {
                    do_dump(flags);
                }
            }, m_icon
        );
    } else if ((flags & DumpFileFlag_XCI) && is_trimmed) {
        App::PushErrorBox(trim_rc, "GameCard is trimmed, full dump is not possible!"_i18n);
    } else {
        do_dump(flags);
    }

    R_SUCCEED();
}

Result Menu::GcGetSecurityInfo(GameCardSecurityInformation& out) {
    R_TRY(GcMountPartition(FsGameCardPartitionRaw_Secure));

    constexpr u64 title_id = 0x0100000000000000; // FS
    Handle handle{};
    DebugEventInfo event_info{};
    u64 pids[0x50]{};
    s32 process_count{};

    R_TRY(svcGetProcessList(&process_count, pids, std::size(pids)));
    for (s32 i = 0; i < (process_count - 1); i++) {
        if (R_SUCCEEDED(svcDebugActiveProcess(&handle, pids[i]))) {
            ON_SCOPE_EXIT(svcCloseHandle(handle));

            if (R_FAILED(svcGetDebugEvent(&event_info, handle)) || title_id != event_info.info.create_process.program_id) {
                continue;
            }

            const auto package_id = m_package_id;
            static u64 addr{};
            MemoryInfo mem_info{};
            u32 page_info{};
            std::vector<u8> data{};

            for (;;) {
                R_TRY(svcQueryDebugProcessMemory(&mem_info, &page_info, handle, addr));

                // if addr=0 then we hit the reserved memory section
                addr = mem_info.addr + mem_info.size;
                if (!addr) {
                    break;
                }

                // skip memory that we don't want
                if (mem_info.attr || !mem_info.size || (mem_info.perm & Perm_Rw) != Perm_Rw || (mem_info.type & MemState_Type) != MemType_CodeMutable) {
                    continue;
                }

                data.resize(mem_info.size);
                R_TRY(svcReadDebugProcessMemory(data.data(), handle, mem_info.addr, data.size()));

                for (s64 i = 0; i < data.size(); i += 8) {
                    if (i + sizeof(out.initial_data) >= data.size()) {
                        break;
                    }

                    if (!std::memcmp(&package_id, data.data() + i, sizeof(m_package_id))) [[unlikely]] {
                        log_write("[GC] found the package id\n");
                        u8 hash[SHA256_HASH_SIZE];
                        sha256CalculateHash(hash, data.data() + i, 0x200);

                        if (!std::memcmp(hash, m_initial_data_hash, sizeof(hash))) {
                            // successive calls will jump to the addr as the location will not change.
                            addr = mem_info.addr;
                            log_write("[GC] found the security info\n");
                            log_write("\tperm: 0x%X\n", mem_info.perm);
                            log_write("\ttype: 0x%X\n", mem_info.type & MemState_Type);
                            log_write("\taddr: 0x%016lX\n", mem_info.addr);
                            log_write("\toff: 0x%016lX\n", mem_info.addr + i);
                            std::memcpy(&out, data.data() + i - offsetof(GameCardSecurityInformation, initial_data), sizeof(out));
                            R_SUCCEED();
                        }
                    }
                }
            }
        }
    }

    R_THROW(Result_GcFailedToGetSecurityInfo);
}

} // namespace sphaira::ui::menu::gc
