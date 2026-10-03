#include "ui/menus/save/save_slot_backend.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include <sys/statvfs.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

namespace sphaira::ui::menu::save {

namespace {

constexpr bool IsConcreteSaveDataSpace(FsSaveDataSpaceId space_id) {
    return space_id == FsSaveDataSpaceId_System ||
           space_id == FsSaveDataSpaceId_User ||
           space_id == FsSaveDataSpaceId_SdSystem ||
           space_id == FsSaveDataSpaceId_Temporary ||
           space_id == FsSaveDataSpaceId_SdUser ||
           space_id == FsSaveDataSpaceId_ProperSystem ||
           space_id == FsSaveDataSpaceId_SafeMode;
}

bool MatchSaveAttr(const FsSaveDataAttribute& a, const FsSaveDataAttribute& b) {
    return a.application_id == b.application_id &&
           a.uid.uid[0] == b.uid.uid[0] && a.uid.uid[1] == b.uid.uid[1] &&
           a.system_save_data_id == b.system_save_data_id &&
           a.save_data_type == b.save_data_type &&
           a.save_data_rank == b.save_data_rank &&
           a.save_data_index == b.save_data_index;
}

bool MatchSaveAttr(const FsSaveDataInfo& a, const FsSaveDataAttribute& b) {
    return a.application_id == b.application_id &&
           a.uid.uid[0] == b.uid.uid[0] && a.uid.uid[1] == b.uid.uid[1] &&
           a.system_save_data_id == b.system_save_data_id &&
           a.save_data_type == b.save_data_type &&
           a.save_data_rank == b.save_data_rank &&
           a.save_data_index == b.save_data_index;
}

auto QuerySaveDataSpaceFreeBytes(FsSaveDataSpaceId space_id, s64* out_free_bytes) -> Result {
    if (!out_free_bytes) return FsError_InvalidSize;
    *out_free_bytes = 0;

    switch (space_id) {
        case FsSaveDataSpaceId_User:
        case FsSaveDataSpaceId_Temporary: {
            fs::FsNativeBis bis(FsBisPartitionId_User);
            if (R_FAILED(bis.GetFsOpenResult())) return bis.GetFsOpenResult();
            return bis.GetFreeSpace("/", out_free_bytes);
        }
        case FsSaveDataSpaceId_System:
        case FsSaveDataSpaceId_ProperSystem: {
            fs::FsNativeBis bis(FsBisPartitionId_System);
            if (R_FAILED(bis.GetFsOpenResult())) return bis.GetFsOpenResult();
            return bis.GetFreeSpace("/", out_free_bytes);
        }
        case FsSaveDataSpaceId_SafeMode: {
            fs::FsNativeBis bis(FsBisPartitionId_SafeMode);
            if (R_FAILED(bis.GetFsOpenResult())) return bis.GetFsOpenResult();
            return bis.GetFreeSpace("/", out_free_bytes);
        }
        case FsSaveDataSpaceId_SdSystem:
        case FsSaveDataSpaceId_SdUser: {
            struct statvfs st{};
            if (statvfs("sdmc:/", &st) == 0) {
                *out_free_bytes = static_cast<s64>(st.f_bfree) * static_cast<s64>(st.f_bsize);
                return 0;
            }
            return FsError_PathNotFound;
        }
        default:
            return FsError_PathNotFound;
    }
}

} // namespace

auto ValidateCreationRequest(const SaveCreationRequest& req) -> SaveBackendStatus {
    if (req.attr.save_data_type != FsSaveDataType_Account) return SaveBackendStatus::UnsupportedSaveType;
    if (req.space_id != FsSaveDataSpaceId_User) return SaveBackendStatus::UnsupportedSpace;
    if (req.attr.save_data_rank != FsSaveDataRank_Primary) return SaveBackendStatus::UnsupportedRank;
    if (req.attr.save_data_index != 0) return SaveBackendStatus::UnsupportedIndex;
    if (req.attr.system_save_data_id != 0) return SaveBackendStatus::UnsupportedSaveType;
    if (req.attr.application_id == 0) return SaveBackendStatus::InvalidApplicationId;
    if (req.selected_uid.uid[0] == 0 && req.selected_uid.uid[1] == 0) return SaveBackendStatus::InvalidAccountUid;
    if (std::memcmp(&req.attr.uid, &req.selected_uid, sizeof(AccountUid)) != 0) return SaveBackendStatus::InvalidAccountUid;
    if (req.owner_id == 0) return SaveBackendStatus::MissingOwnerId;
    if (req.data_size <= 0 || req.data_size > std::numeric_limits<s64>::max() || (req.data_size % 0x4000 != 0)) return SaveBackendStatus::InvalidSizes;
    if (req.journal_size < 0 || req.journal_size > std::numeric_limits<s64>::max() || (req.journal_size % 0x4000 != 0)) return SaveBackendStatus::InvalidSizes;
    if (req.available_size != 0x4000 || req.flags != 0) return SaveBackendStatus::InvalidFlags;
    return SaveBackendStatus::Success;
}

auto PlanAccountSaveCreation(
    u64 application_id,
    const AccountUid& selected_uid,
    const SaveArchiveSizing* archive_sizing,
    SaveCreationRequest& out_request,
    SaveBackendStatus* out_status
) -> Result {
    if (application_id == 0) {
        if (out_status) *out_status = SaveBackendStatus::InvalidApplicationId;
        return FsError_PathNotFound;
    }

    if (selected_uid.uid[0] == 0 && selected_uid.uid[1] == 0) {
        if (out_status) *out_status = SaveBackendStatus::InvalidAccountUid;
        return FsError_PathNotFound;
    }

    const auto accounts = App::GetAccountList();
    bool user_found = false;
    for (const auto& acc : accounts) {
        if (!std::memcmp(&acc.uid, &selected_uid, sizeof(AccountUid))) {
            user_found = true;
            break;
        }
    }
    if (!user_found) {
        if (out_status) *out_status = SaveBackendStatus::InvalidAccountUid;
        return FsError_PathNotFound;
    }

    s64 final_data = 0;
    s64 final_journal = 0;
    u64 owner_id = 0;
    SizingProvenance provenance = SizingProvenance::InstalledControlData;

    u64 actual_size = 0;
    auto control_data = std::make_unique<NsApplicationControlData>();
    const auto rc = nsGetApplicationControlData(
        NsApplicationControlSource_Storage,
        application_id,
        control_data.get(),
        sizeof(NsApplicationControlData),
        &actual_size
    );

    if (R_SUCCEEDED(rc) && actual_size >= sizeof(NacpStruct)) {
        const u64 base_data_u64 = control_data->nacp.user_account_save_data_size
            ? control_data->nacp.user_account_save_data_size
            : control_data->nacp.user_account_save_data_size_max;
        const u64 base_journal_u64 = control_data->nacp.user_account_save_data_journal_size
            ? control_data->nacp.user_account_save_data_journal_size
            : control_data->nacp.user_account_save_data_journal_size_max;
        owner_id = control_data->nacp.save_data_owner_id;

        if (owner_id == 0) {
            if (out_status) *out_status = SaveBackendStatus::MissingOwnerId;
            return FsError_PathNotFound;
        }

        if (base_data_u64 == 0 || base_data_u64 > static_cast<u64>(std::numeric_limits<s64>::max()) || (base_data_u64 % 0x4000 != 0)) {
            if (out_status) *out_status = SaveBackendStatus::InvalidSizes;
            return FsError_InvalidSize;
        }
        if (base_journal_u64 > static_cast<u64>(std::numeric_limits<s64>::max()) || (base_journal_u64 % 0x4000 != 0)) {
            if (out_status) *out_status = SaveBackendStatus::InvalidSizes;
            return FsError_InvalidSize;
        }

        final_data = static_cast<s64>(base_data_u64);
        final_journal = static_cast<s64>(base_journal_u64);

        if (archive_sizing && archive_sizing->has_sizing) {
            const s64 arch_data = archive_sizing->data_size;
            const s64 arch_journal = archive_sizing->journal_size;
            if (arch_data <= 0 || arch_data > std::numeric_limits<s64>::max() || (arch_data % 0x4000 != 0) ||
                arch_journal < 0 || arch_journal > std::numeric_limits<s64>::max() || (arch_journal % 0x4000 != 0)) {
                if (out_status) *out_status = SaveBackendStatus::InvalidSizes;
                return FsError_InvalidSize;
            }
            final_data = std::max(final_data, arch_data);
            final_journal = std::max(final_journal, arch_journal);
            provenance = SizingProvenance::InstalledControlDataAndArchiveMetadata;
        }
    } else {
        if (!archive_sizing || !archive_sizing->has_metadata) {
            if (out_status) *out_status = archive_sizing ? SaveBackendStatus::GameNotInstalled : SaveBackendStatus::MissingControlData;
            return R_FAILED(rc) ? rc : static_cast<Result>(FsError_PathNotFound);
        }
        if (archive_sizing->attr.application_id != application_id) {
            if (out_status) *out_status = SaveBackendStatus::InvalidApplicationId;
            return FsError_PathNotFound;
        }
        if (archive_sizing->attr.save_data_type != FsSaveDataType_Account ||
            archive_sizing->attr.system_save_data_id != 0) {
            if (out_status) *out_status = SaveBackendStatus::UnsupportedSaveType;
            return FsError_PathNotFound;
        }
        if (archive_sizing->attr.save_data_rank != FsSaveDataRank_Primary) {
            if (out_status) *out_status = SaveBackendStatus::UnsupportedRank;
            return FsError_PathNotFound;
        }
        if (archive_sizing->attr.save_data_index != 0) {
            if (out_status) *out_status = SaveBackendStatus::UnsupportedIndex;
            return FsError_PathNotFound;
        }
        if (archive_sizing->owner_id == 0) {
            if (out_status) *out_status = SaveBackendStatus::GameNotInstalled;
            return FsError_PathNotFound;
        }
        const s64 arch_data = archive_sizing->data_size;
        const s64 arch_journal = archive_sizing->journal_size;
        if (arch_data <= 0 || arch_data > std::numeric_limits<s64>::max() || (arch_data % 0x4000 != 0) ||
            arch_journal < 0 || arch_journal > std::numeric_limits<s64>::max() || (arch_journal % 0x4000 != 0)) {
            if (out_status) *out_status = SaveBackendStatus::InvalidSizes;
            return FsError_InvalidSize;
        }

        final_data = arch_data;
        final_journal = arch_journal;
        owner_id = archive_sizing->owner_id;
        provenance = SizingProvenance::ArchiveMetadata;
    }

    out_request = SaveCreationRequest{};
    out_request.attr.application_id = application_id;
    out_request.attr.uid = selected_uid;
    out_request.attr.system_save_data_id = 0;
    out_request.attr.save_data_type = FsSaveDataType_Account;
    out_request.attr.save_data_rank = FsSaveDataRank_Primary;
    out_request.attr.save_data_index = 0;
    out_request.space_id = FsSaveDataSpaceId_User;
    out_request.data_size = final_data;
    out_request.journal_size = final_journal;
    out_request.owner_id = owner_id;
    out_request.available_size = 0x4000;
    out_request.flags = 0;
    out_request.provenance = provenance;
    out_request.selected_uid = selected_uid;

    const auto v_status = ValidateCreationRequest(out_request);
    if (v_status != SaveBackendStatus::Success) {
        if (out_status) *out_status = v_status;
        return FsError_InvalidSize;
    }

    if (out_status) *out_status = SaveBackendStatus::Success;
    return 0;
}

auto CreateSaveDataChecked(
    const SaveCreationRequest& request,
    std::function<bool()> should_cancel
) -> SaveCreationResult {
    SaveCreationResult result{};

    const auto v_status = ValidateCreationRequest(request);
    if (v_status != SaveBackendStatus::Success) {
        result.status = v_status;
        result.rc = FsError_InvalidSize;
        return result;
    }

    if (should_cancel && should_cancel()) {
        result.status = SaveBackendStatus::Cancelled;
        result.rc = Result_TransferCancelled;
        return result;
    }

    FsSaveDataCreationInfo info{};
    info.save_data_size = request.data_size;
    info.journal_size = request.journal_size;
    info.available_size = request.available_size;
    info.owner_id = request.owner_id;
    info.flags = request.flags;
    info.save_data_space_id = request.space_id;

    FsSaveDataMetaInfo meta{};
    meta.size = 0x40060;
    meta.type = FsSaveDataMetaType_Thumbnail;

    const auto create_rc = fsCreateSaveDataFileSystem(&request.attr, &info, &meta);
    result.ipc_executed = true;
    if (R_FAILED(create_rc)) {
        result.status = SaveBackendStatus::IpcFailed;
        result.rc = create_rc;
        return result;
    }
    result.create_succeeded = true;

    const auto discovered = DiscoverSaveDataInfo(&request.attr.uid, request.attr.save_data_type);
    std::vector<FsSaveDataInfo> exact_matches;
    for (const auto& entry : discovered) {
        if (entry.save_data_space_id == request.space_id && entry.save_data_id != 0 && MatchSaveAttr(entry, request.attr)) {
            exact_matches.push_back(entry);
        }
    }

    if (exact_matches.size() != 1) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = FsError_PathNotFound;
        return result;
    }

    const auto& matched = exact_matches.front();
    FsSaveDataExtraData extra{};
    const auto read_rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
        &extra, sizeof(extra), static_cast<FsSaveDataSpaceId>(matched.save_data_space_id), matched.save_data_id);
    if (R_FAILED(read_rc)) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = read_rc;
        return result;
    }

    if (!MatchSaveAttr(extra.attr, request.attr)) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = FsError_PathNotFound;
        return result;
    }

    if (extra.data_size != request.data_size || extra.journal_size != request.journal_size) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = FsError_InvalidSize;
        return result;
    }

    result.status = SaveBackendStatus::Success;
    result.rc = 0;
    result.verified = true;
    result.verified_info = matched;
    result.verified_extra = extra;
    return result;
}

auto ExtendSaveDataChecked(
    const SaveGrowRequest& request,
    std::function<bool()> should_cancel
) -> SaveGrowResult {
    SaveGrowResult result{};

    if (!IsConcreteSaveDataSpace(request.space_id) || request.space_id != request.target_info.save_data_space_id) {
        result.status = SaveBackendStatus::UnsupportedSpace;
        result.rc = FsError_PathNotFound;
        return result;
    }

    if (request.target_info.save_data_id == 0 ||
        request.requested_data_size <= 0 || request.requested_data_size > std::numeric_limits<s64>::max() ||
        (request.requested_data_size % 0x4000 != 0) ||
        request.requested_journal_size < 0 || request.requested_journal_size > std::numeric_limits<s64>::max() ||
        (request.requested_journal_size % 0x4000 != 0)) {
        result.status = SaveBackendStatus::InvalidSizes;
        result.rc = FsError_InvalidSize;
        return result;
    }

    FsSaveDataExtraData current_extra{};
    const auto read_rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
        &current_extra, sizeof(current_extra), request.space_id, request.target_info.save_data_id);
    if (R_FAILED(read_rc)) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = read_rc;
        return result;
    }

    if (!MatchSaveAttr(request.target_info, current_extra.attr)) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = FsError_PathNotFound;
        return result;
    }

    if (current_extra.data_size <= 0 || (current_extra.data_size % 0x4000 != 0) ||
        current_extra.journal_size < 0 || (current_extra.journal_size % 0x4000 != 0)) {
        result.status = SaveBackendStatus::InvalidSizes;
        result.rc = FsError_InvalidSize;
        return result;
    }

    if (request.requested_data_size < current_extra.data_size ||
        request.requested_journal_size < current_extra.journal_size) {
        result.status = SaveBackendStatus::InvalidSizes;
        result.rc = FsError_InvalidSize;
        return result;
    }

    if (request.requested_data_size == current_extra.data_size &&
        request.requested_journal_size == current_extra.journal_size) {
        result.status = SaveBackendStatus::Success;
        result.is_noop = true;
        result.verified = true;
        result.actual_data_size = current_extra.data_size;
        result.actual_journal_size = current_extra.journal_size;
        result.verified_extra = current_extra;
        return result;
    }

    const s64 delta_data = request.requested_data_size - current_extra.data_size;
    const s64 delta_journal = request.requested_journal_size - current_extra.journal_size;
    const s64 additional_required_bytes = delta_data + delta_journal;

    if (additional_required_bytes > 0) {
        s64 target_free_bytes = 0;
        const auto space_rc = QuerySaveDataSpaceFreeBytes(request.space_id, &target_free_bytes);
        if (R_FAILED(space_rc)) {
            result.status = SaveBackendStatus::VerificationFailed;
            result.rc = space_rc;
            return result;
        }
        if (target_free_bytes < additional_required_bytes) {
            result.status = SaveBackendStatus::InvalidSizes;
            result.rc = FsError_InvalidSize;
            return result;
        }
    }

    if (should_cancel && should_cancel()) {
        result.status = SaveBackendStatus::Cancelled;
        result.rc = Result_TransferCancelled;
        return result;
    }

    const auto extend_rc = fsExtendSaveDataFileSystem(
        request.space_id, request.target_info.save_data_id,
        request.requested_data_size, request.requested_journal_size);
    result.ipc_executed = true;
    if (R_FAILED(extend_rc)) {
        result.status = SaveBackendStatus::IpcFailed;
        result.rc = extend_rc;
        return result;
    }

    FsSaveDataExtraData post_extra{};
    const auto post_read_rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
        &post_extra, sizeof(post_extra), request.space_id, request.target_info.save_data_id);
    if (R_FAILED(post_read_rc)) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = post_read_rc;
        return result;
    }

    if (!MatchSaveAttr(request.target_info, post_extra.attr)) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = FsError_PathNotFound;
        return result;
    }

    if (post_extra.data_size < request.requested_data_size ||
        post_extra.journal_size < request.requested_journal_size) {
        result.status = SaveBackendStatus::VerificationFailed;
        result.rc = FsError_InvalidSize;
        return result;
    }

    result.status = SaveBackendStatus::Success;
    result.rc = 0;
    result.verified = true;
    result.actual_data_size = post_extra.data_size;
    result.actual_journal_size = post_extra.journal_size;
    result.verified_extra = post_extra;
    return result;
}

auto GetBackendStatusMessage(SaveBackendStatus status, const char* game_name) -> std::string {
    switch (status) {
        case SaveBackendStatus::GameNotInstalled: {
            char msg[1024];
            std::snprintf(msg, sizeof(msg), "%s is not installed. Install the game to restore its save."_i18n.c_str(), game_name);
            return msg;
        }
        case SaveBackendStatus::UnsupportedSaveType: return "Save slot creation is only supported for Account saves."_i18n;
        case SaveBackendStatus::UnsupportedSpace: return "Save slot creation is only supported in User space."_i18n;
        case SaveBackendStatus::UnsupportedRank: return "Save slot creation is only supported for primary save slots."_i18n;
        case SaveBackendStatus::UnsupportedIndex: return "Save slot creation is only supported for slot index 0."_i18n;
        case SaveBackendStatus::MissingControlData: return "Application control data not found for installed title."_i18n;
        case SaveBackendStatus::InvalidSizes: return "Save slot sizing is invalid or unaligned."_i18n;
        case SaveBackendStatus::MissingOwnerId: return "Application control data is missing save data owner."_i18n;
        case SaveBackendStatus::InvalidAccountUid: return "No valid local user selected for save slot creation."_i18n;
        case SaveBackendStatus::InvalidApplicationId: return "Invalid application ID for save slot creation."_i18n;
        case SaveBackendStatus::InvalidFlags: return "Save slot creation flags are invalid."_i18n;
        case SaveBackendStatus::Cancelled: return "Save slot creation was cancelled."_i18n;
        case SaveBackendStatus::IpcFailed: return "Failed to create save data filesystem."_i18n;
        case SaveBackendStatus::VerificationFailed: return "Save slot verification failed after creation."_i18n;
        default: return "Save slot operation failed."_i18n;
    }
}

} // namespace sphaira::ui::menu::save
