#include "wifi_manager.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sphaira::wifi {

namespace {

NifmRequest g_connect_request{};
bool g_connect_request_active = false; // main thread only (wifi menu Update/OptionBox callbacks)

void CloseConnectRequest() {
    if (g_connect_request_active) {
        nifmRequestClose(&g_connect_request);
        g_connect_request_active = false;
    }
}

} // namespace

auto WifiProfile::GetAuthString() const -> std::string {
    if (auth == NifmAuthentication_Open && (enc == NifmEncryption_None || enc == NifmEncryption_Invalid)) {
        return "Open"_i18n;
    }

    std::string auth_str;
    switch (auth) {
        case NifmAuthentication_Open: auth_str = "Open"; break;
        case NifmAuthentication_Shared: auth_str = "Shared (WEP)"; break;
        case NifmAuthentication_Wpa: auth_str = "WPA"; break;
        case NifmAuthentication_WpaPsk: auth_str = "WPA-PSK"; break;
        case NifmAuthentication_Wpa2: auth_str = "WPA2"; break;
        case NifmAuthentication_Wpa2Psk: auth_str = "WPA2-PSK"; break;
        default: auth_str = "Secured"_i18n; break;
    }

    std::string enc_str;
    switch (enc) {
        case NifmEncryption_None: break;
        case NifmEncryption_Wep: enc_str = "WEP"; break;
        case NifmEncryption_Tkip: enc_str = "TKIP"; break;
        case NifmEncryption_Aes: enc_str = "AES"; break;
        default: break;
    }

    if (!enc_str.empty()) {
        return auth_str + " (" + enc_str + ")";
    }
    return auth_str;
}

auto GetProfiles() -> std::vector<WifiProfile> {
    std::vector<WifiProfile> profiles;

    Uuid active_uuid{};
    bool has_active = false;
    NifmNetworkProfileData active_profile{};
    if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&active_profile))) {
        active_uuid = active_profile.uuid;
        has_active = true;
    }

    constexpr s32 MAX_PROFILES = 128;
    std::vector<NifmNetworkProfileBasicInfo> basic_infos(MAX_PROFILES);
    s32 total_entries = 0;

    const Result rc = nifmEnumerateNetworkProfiles(NifmNetworkProfileType_User, basic_infos.data(), MAX_PROFILES, &total_entries);
    if (R_FAILED(rc)) {
        log_write("[WIFI] nifmEnumerateNetworkProfiles failed: 0x%X\n", R_VALUE(rc));
        return profiles;
    }

    const s32 count = std::min<s32>(total_entries, MAX_PROFILES);
    for (s32 i = 0; i < count; ++i) {
        const auto& info = basic_infos[i];
        WifiProfile p{};
        p.uuid = info.uuid;
        p.name = info.network_name;

        char ssid_buf[sizeof(info.ssid) + 1] = {0};
        const size_t len = std::min<size_t>(info.ssid_len, sizeof(info.ssid));
        std::memcpy(ssid_buf, info.ssid, len);
        ssid_buf[len] = '\0';
        p.ssid = ssid_buf;
        if (p.name.empty()) {
            p.name = p.ssid;
        }

        p.auth = info.authentication;
        p.enc = info.encryption;
        p.is_connected = has_active && (std::memcmp(&info.uuid, &active_uuid, sizeof(Uuid)) == 0);

        NifmNetworkProfileData full{};
        if (R_SUCCEEDED(nifmGetNetworkProfile(info.uuid, &full))) {
            char pass_buf[sizeof(full.wireless_setting_data.passphrase) + 1] = {0};
            std::memcpy(pass_buf, full.wireless_setting_data.passphrase, sizeof(full.wireless_setting_data.passphrase));
            pass_buf[sizeof(full.wireless_setting_data.passphrase)] = '\0';
            p.passphrase = pass_buf;
        }

        profiles.push_back(std::move(p));
    }

    std::sort(profiles.begin(), profiles.end(), [](const WifiProfile& a, const WifiProfile& b) {
        if (a.is_connected != b.is_connected) {
            return a.is_connected > b.is_connected;
        }
        return a.name < b.name;
    });

    return profiles;
}

auto GetProfileData(const Uuid& uuid, NifmNetworkProfileData& out_data) -> Result {
    return nifmGetNetworkProfile(uuid, &out_data);
}

auto SetProfileData(const NifmNetworkProfileData& data) -> Result {
    Uuid out_uuid{};
    return nifmSetNetworkProfile(&data, &out_uuid);
}

auto RemoveProfile(const Uuid& uuid) -> Result {
    Service* srv = nifmGetServiceSession_GeneralService();
    if (!srv || !serviceIsActive(srv)) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }
    serviceAssumeDomain(srv);
    return serviceDispatchIn(srv, 10, uuid);
}

auto RenameProfile(const Uuid& uuid, const std::string& new_name) -> Result {
    NifmNetworkProfileData data{};
    Result rc = nifmGetNetworkProfile(uuid, &data);
    if (R_FAILED(rc)) {
        return rc;
    }

    std::memset(data.network_name, 0, sizeof(data.network_name));
    std::strncpy(data.network_name, new_name.c_str(), sizeof(data.network_name) - 1);

    Uuid out_uuid{};
    return nifmSetNetworkProfile(&data, &out_uuid);
}

auto ChangePassphrase(const Uuid& uuid, const std::string& new_pass) -> Result {
    NifmNetworkProfileData data{};
    Result rc = nifmGetNetworkProfile(uuid, &data);
    if (R_FAILED(rc)) {
        return rc;
    }

    std::memset(data.wireless_setting_data.passphrase, 0, sizeof(data.wireless_setting_data.passphrase));
    std::strncpy(reinterpret_cast<char*>(data.wireless_setting_data.passphrase), new_pass.c_str(), sizeof(data.wireless_setting_data.passphrase) - 1);

    Uuid out_uuid{};
    return nifmSetNetworkProfile(&data, &out_uuid);
}

auto ChangeSsid(const Uuid& uuid, const std::string& new_ssid) -> Result {
    NifmNetworkProfileData data{};
    Result rc = nifmGetNetworkProfile(uuid, &data);
    if (R_FAILED(rc)) {
        return rc;
    }

    std::memset(data.wireless_setting_data.ssid, 0, sizeof(data.wireless_setting_data.ssid));
    const size_t len = std::min(new_ssid.size(), sizeof(data.wireless_setting_data.ssid));
    std::memcpy(data.wireless_setting_data.ssid, new_ssid.data(), len);
    data.wireless_setting_data.ssid_len = static_cast<u8>(len);

    Uuid out_uuid{};
    return nifmSetNetworkProfile(&data, &out_uuid);
}

void CancelConnect() {
    if (g_connect_request_active) {
        nifmRequestCancel(&g_connect_request);
        nifmRequestClose(&g_connect_request);
        g_connect_request_active = false;
    }
}

auto Connect(const Uuid& uuid) -> Result {
    CancelConnect();

    if (!IsWirelessEnabled()) {
        const Result rc = nifmSetWirelessCommunicationEnabled(true);
        if (R_FAILED(rc)) {
            log_write("[WIFI] nifmSetWirelessCommunicationEnabled failed: 0x%X\n", R_VALUE(rc));
            return rc;
        }
    }

    Result rc = nifmCreateRequest(&g_connect_request, true);
    if (R_FAILED(rc)) {
        log_write("[WIFI] nifmCreateRequest failed: 0x%X\n", R_VALUE(rc));
        return rc;
    }
    g_connect_request_active = true;

    rc = nifmRequestSetNetworkProfileId(&g_connect_request, uuid);
    if (R_FAILED(rc)) {
        log_write("[WIFI] nifmRequestSetNetworkProfileId failed: 0x%X\n", R_VALUE(rc));
        CancelConnect();
        return rc;
    }

    rc = nifmRequestSubmit(&g_connect_request);
    if (R_FAILED(rc)) {
        log_write("[WIFI] nifmRequestSubmit failed: 0x%X\n", R_VALUE(rc));
        CancelConnect();
        return rc;
    }

    return 0;
}

auto PollConnect() -> ConnectStatus {
    if (!g_connect_request_active) {
        return {ConnectState::None, 0};
    }

    NifmRequestState state{};
    Result rc = nifmGetRequestState(&g_connect_request, &state);
    if (R_FAILED(rc)) {
        log_write("[WIFI] nifmGetRequestState failed: 0x%X\n", R_VALUE(rc));
        CloseConnectRequest();
        return {ConnectState::Failed, rc};
    }

    if (state == NifmRequestState_OnHold) {
        return {ConnectState::Pending, 0};
    }

    if (state == NifmRequestState_Available) {
        const Result res = nifmGetResult(&g_connect_request);
        CloseConnectRequest();
        if (R_SUCCEEDED(res)) {
            return {ConnectState::Succeeded, 0};
        } else {
            return {ConnectState::Failed, res};
        }
    }

    Result res = nifmGetResult(&g_connect_request);
    if (R_SUCCEEDED(res)) {
        res = MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen);
    }
    log_write("[WIFI] connection failed with state %d, result: 0x%X\n", static_cast<int>(state), R_VALUE(res));
    CloseConnectRequest();
    return {ConnectState::Failed, res};
}

auto SetWirelessEnabled(bool enable) -> Result {
    return nifmSetWirelessCommunicationEnabled(enable);
}

auto IsWirelessEnabled() -> bool {
    bool enabled = false;
    if (R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&enabled))) {
        return enabled;
    }
    return false;
}

auto IsConnectedTo(const Uuid& uuid) -> bool {
    NifmNetworkProfileData cur{};
    if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&cur))) {
        return std::memcmp(&cur.uuid, &uuid, sizeof(Uuid)) == 0;
    }
    return false;
}

auto UuidToString(const Uuid& uuid) -> std::string {
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid.uuid[0], uuid.uuid[1], uuid.uuid[2], uuid.uuid[3],
        uuid.uuid[4], uuid.uuid[5],
        uuid.uuid[6], uuid.uuid[7],
        uuid.uuid[8], uuid.uuid[9],
        uuid.uuid[10], uuid.uuid[11], uuid.uuid[12], uuid.uuid[13], uuid.uuid[14], uuid.uuid[15]);
    return buf;
}

} // namespace sphaira::wifi
