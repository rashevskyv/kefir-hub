#pragma once

#include "fs.hpp"
#include <functional>
#include <string>
#include <vector>

namespace sphaira::ui {
struct ProgressBox;
}

namespace sphaira::ui::menu::save {

enum class SizingProvenance {
    InstalledControlData,
    InstalledControlDataAndArchiveMetadata,
    ArchiveMetadata,
};

enum class SaveBackendStatus {
    Success = 0,
    UnsupportedSaveType,
    UnsupportedSpace,
    UnsupportedRank,
    UnsupportedIndex,
    MissingControlData,
    InvalidSizes,
    MissingOwnerId,
    InvalidAccountUid,
    InvalidApplicationId,
    InvalidFlags,
    Cancelled,
    IpcFailed,
    VerificationFailed,
};

struct SaveCreationRequest {
    FsSaveDataAttribute attr{};
    FsSaveDataSpaceId space_id{FsSaveDataSpaceId_User};
    s64 data_size{0};
    s64 journal_size{0};
    u64 owner_id{0};
    s64 available_size{0x4000};
    u32 flags{0};
    SizingProvenance provenance{SizingProvenance::InstalledControlData};
    AccountUid selected_uid{};
};

struct SaveCreationResult {
    SaveBackendStatus status{SaveBackendStatus::UnsupportedSaveType};
    Result rc{0};
    bool ipc_executed{false};
    bool create_succeeded{false};
    bool verified{false};
    FsSaveDataInfo verified_info{};
    FsSaveDataExtraData verified_extra{};
};

struct SaveArchiveSizing {
    bool has_sizing{false};
    s64 data_size{0};
    s64 journal_size{0};
    bool has_metadata{false};
    u64 owner_id{0};
    FsSaveDataAttribute attr{};
};

struct SaveArchiveAdmissionResult {
    bool admitted{false};
    Result rc{0};
    SaveArchiveSizing sizing{};
    s64 payload_file_count{0};
    s64 payload_directory_count{0};
    s64 payload_file_bytes{0};
};

struct SaveGrowRequest {
    FsSaveDataInfo target_info{};
    FsSaveDataSpaceId space_id{FsSaveDataSpaceId_User};
    s64 requested_data_size{0};
    s64 requested_journal_size{0};
};

struct SaveGrowResult {
    SaveBackendStatus status{SaveBackendStatus::UnsupportedSaveType};
    Result rc{0};
    bool ipc_executed{false};
    bool verified{false};
    bool is_noop{false};
    s64 actual_data_size{0};
    s64 actual_journal_size{0};
    FsSaveDataExtraData verified_extra{};
};

auto ValidateCreationRequest(const SaveCreationRequest& req) -> SaveBackendStatus;

auto PlanAccountSaveCreation(
    u64 application_id,
    const AccountUid& selected_uid,
    const SaveArchiveSizing* archive_sizing,
    SaveCreationRequest& out_request,
    SaveBackendStatus* out_status = nullptr
) -> Result;

auto InspectSaveArchiveAdmission(
    const fs::FsPath& archive_path,
    ProgressBox* pbox = nullptr,
    bool allow_empty = false
) -> SaveArchiveAdmissionResult;

auto CreateSaveDataChecked(
    const SaveCreationRequest& request,
    std::function<bool()> should_cancel = nullptr
) -> SaveCreationResult;

auto ExtendSaveDataChecked(
    const SaveGrowRequest& request,
    std::function<bool()> should_cancel = nullptr
) -> SaveGrowResult;

auto GetBackendStatusMessage(SaveBackendStatus status) -> std::string;

auto FormatSaveCreationPrompt(
    const SaveCreationRequest& req,
    const std::string& game_name,
    const std::string& user_nickname
) -> std::string;

} // namespace sphaira::ui::menu::save
