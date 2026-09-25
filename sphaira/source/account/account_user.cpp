#include "account/account_user.hpp"
#include "account/account_user_internal.hpp"
#include "account/account_link.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "image.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::account_user {
namespace {

constexpr u32 CMD_BEGIN_REG = 200;
constexpr u32 CMD_COMPLETE_REG = 201;
constexpr u32 CMD_DELETE_USER = 203;
constexpr u32 CMD_GET_PROFILE_EDITOR = 205;
constexpr u32 CMD_EDITOR_STORE = 100;
constexpr u32 CMD_EDITOR_STORE_IMAGE = 101;

constexpr u32 BUF_IN_PTR = SfBufferAttr_In | SfBufferAttr_HipcPointer;
constexpr u32 BUF_IN_MAP = SfBufferAttr_In | SfBufferAttr_HipcMapAlias;

auto OpenAccSu(Service* out) -> Result {
    R_TRY(smGetService(out, "acc:su"));
    R_SUCCEED();
}

auto StoreProfile(const AccountUid& uid, const AccountProfileBase& base, const AccountUserData& data,
    const u8* jpeg, u64 jpeg_size) -> Result
{
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service editor{};
    R_TRY(serviceDispatchIn(&accsu, CMD_GET_PROFILE_EDITOR, uid,
        .out_num_objects = 1,
        .out_objects = &editor));
    ON_SCOPE_EXIT(serviceClose(&editor));

    if (jpeg && jpeg_size) {
        R_TRY(serviceDispatchIn(&editor, CMD_EDITOR_STORE_IMAGE, base,
            .buffer_attrs = { BUF_IN_PTR, BUF_IN_MAP },
            .buffers = { { &data, sizeof(data) }, { jpeg, jpeg_size } }));
    } else {
        R_TRY(serviceDispatchIn(&editor, CMD_EDITOR_STORE, base,
            .buffer_attrs = { BUF_IN_PTR },
            .buffers = { { &data, sizeof(data) } }));
    }
    R_SUCCEED();
}

} // namespace

auto FormatPackCreated(const std::string& folder_name, const std::string& json_created) -> std::string {
    std::string stamp = json_created;
    if (stamp.size() < 15 && folder_name.size() >= 15) {
        stamp = folder_name.substr(0, 15);
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(stamp.c_str(), "%4d%2d%2d_%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return {};
    }
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%02d.%02d.%04d, %02d:%02d", d, mo, y, h, mi);
    return buf;
}

auto ReadJsonField(const std::string& json, const char* key) -> std::string {
    const auto needle = std::string{"\""} + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    auto i = pos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size() || json[i] != ':') {
        return {};
    }
    i++;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size() || json[i] != '"') {
        return {};
    }
    i++;
    std::string out;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            i++;
        }
        out += json[i++];
    }
    return out;
}

auto ReadJsonIntField(const std::string& json, const char* key) -> std::optional<s64> {
    const auto needle = std::string{"\""} + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    auto i = pos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size() || json[i] != ':') {
        return std::nullopt;
    }
    i++;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
        i++;
    }
    if (i >= json.size()) {
        return std::nullopt;
    }
    char* endptr = nullptr;
    const char* start = json.c_str() + i;
    const long long val = std::strtoll(start, &endptr, 10);
    if (endptr == start) {
        return std::nullopt;
    }
    return static_cast<s64>(val);
}

auto LoadProfile(const AccountUid& uid, AccountProfileBase& base, AccountUserData& data) -> Result {
    AccountProfile profile{};
    R_TRY(accountGetProfile(&profile, uid));
    ON_SCOPE_EXIT(accountProfileClose(&profile));
    R_TRY(accountProfileGet(&profile, &data, &base));
    base.uid = uid;
    R_SUCCEED();
}

auto LoadImageJpeg(const AccountUid& uid, std::vector<u8>& out) -> Result {
    AccountProfile profile{};
    R_TRY(accountGetProfile(&profile, uid));
    ON_SCOPE_EXIT(accountProfileClose(&profile));

    u32 size{};
    R_TRY(accountProfileGetImageSize(&profile, &size));
    R_UNLESS(size > 0, Result_FsEmpty);
    out.resize(size);
    u32 actual{};
    R_TRY(accountProfileLoadImage(&profile, out.data(), out.size(), &actual));
    out.resize(actual);
    R_SUCCEED();
}

auto Rename(const AccountUid& uid, const std::string& nickname) -> Result {
    AccountProfileBase base{};
    AccountUserData data{};
    R_TRY(LoadProfile(uid, base, data));
    std::memset(base.nickname, 0, sizeof(base.nickname));
    const auto n = std::min(nickname.size(), sizeof(base.nickname) - 1);
    std::memcpy(base.nickname, nickname.data(), n);
    R_TRY(StoreProfile(uid, base, data, nullptr, 0));
    R_SUCCEED();
}

auto SetImageJpeg(const AccountUid& uid, const std::vector<u8>& jpeg) -> Result {
    R_UNLESS(!jpeg.empty(), Result_FsEmpty);
    AccountProfileBase base{};
    AccountUserData data{};
    R_TRY(LoadProfile(uid, base, data));
    R_TRY(StoreProfile(uid, base, data, jpeg.data(), jpeg.size()));
    R_SUCCEED();
}

auto Create(const std::string& nickname, AccountUid& out_uid, const std::vector<u8>& jpeg) -> Result {
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    AccountUid uid{};
    R_TRY(serviceDispatchOut(&accsu, CMD_BEGIN_REG, uid));

    auto cancel = [&]() {
        serviceDispatchIn(&accsu, 202, uid);
    };

    AccountProfileBase base{};
    base.uid = uid;
    const auto n = std::min(nickname.size(), sizeof(base.nickname) - 1);
    std::memcpy(base.nickname, nickname.data(), n);

    AccountUserData data{};
    std::vector<u8> icon = jpeg;
    if (!icon.empty()) {
        auto normalized = ImageNormalizeAvatar(icon);
        if (normalized.empty()) {
            normalized = ImageNormalizeIcon(icon);
        }
        if (!normalized.empty()) {
            icon = std::move(normalized);
        }
    }
    auto store = StoreProfile(uid, base, data,
        icon.empty() ? nullptr : icon.data(), icon.size());
    if (R_FAILED(store)) {
        log_write("[USER] create store failed 0x%X\n", store);
        cancel();
        R_TRY(store);
    }

    auto complete = serviceDispatchIn(&accsu, CMD_COMPLETE_REG, uid);
    if (R_FAILED(complete)) {
        complete = serviceDispatchIn(&accsu, 206, uid); // CompleteUserRegistrationForcibly
    }
    if (R_FAILED(complete)) {
        log_write("[USER] create complete failed 0x%X\n", complete);
        cancel();
        R_TRY(complete);
    }

    out_uid = uid;
    log_write("[USER] created %s\n", nickname.c_str());
    R_SUCCEED();
}

auto Delete(const AccountUid& uid) -> Result {
    const auto live = App::GetAccountList();
    if (live.size() <= 1) {
        log_write("[USER] refuse DeleteUser: last remaining profile %s\n",
            account_link::UidHex(uid).c_str());
        return Result_FsEmpty;
    }

    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    // Best-effort local BAAS unlink so Horizon-linked / linkalho users can DeleteUser.
    // Keep ACCOUNT alive: do not TerminateAccountDaemons / UnlinkLinkedProfiles / UnregisterAsync.
    {
        Service admin{};
        const auto admin_rc = serviceDispatchIn(&accsu, 250, uid,
            .out_num_objects = 1,
            .out_objects = &admin);
        if (R_SUCCEEDED(admin_rc)) {
            ON_SCOPE_EXIT(serviceClose(&admin));
            const auto unlink_rc = serviceDispatch(&admin, 203); // DeleteRegistrationInfoLocally
            if (R_FAILED(unlink_rc)) {
                log_write("[USER] DeleteRegistrationInfoLocally 0x%X uid %s (continue)\n",
                    unlink_rc, account_link::UidHex(uid).c_str());
            } else {
                log_write("[USER] DeleteRegistrationInfoLocally ok uid %s\n",
                    account_link::UidHex(uid).c_str());
            }
        } else {
            log_write("[USER] GetBaasAccountAdministrator 0x%X uid %s (continue)\n",
                admin_rc, account_link::UidHex(uid).c_str());
        }
    }

    const auto rc = serviceDispatchIn(&accsu, CMD_DELETE_USER, uid);
    if (R_FAILED(rc)) {
        log_write("[USER] DeleteUser 0x%X uid %s\n", rc, account_link::UidHex(uid).c_str());
        return rc;
    }
    log_write("[USER] deleted %s\n", account_link::UidHex(uid).c_str());
    R_SUCCEED();
}

} // namespace sphaira::account_user
