#pragma once

#include "defines.hpp"
#include <string>
#include <vector>

namespace sphaira::wifi {

struct WifiProfile {
    Uuid uuid{};
    std::string name{};
    std::string ssid{};
    std::string passphrase{};
    NifmAuthentication auth{NifmAuthentication_Invalid};
    NifmEncryption enc{NifmEncryption_Invalid};
    bool is_connected{false};
    bool selected{false};

    auto GetAuthString() const -> std::string;
};

// Enumerate all saved user Wi-Fi profiles
auto GetProfiles() -> std::vector<WifiProfile>;

// Get details of a single profile
auto GetProfileData(const Uuid& uuid, NifmNetworkProfileData& out_data) -> Result;

// Save/update profile
auto SetProfileData(const NifmNetworkProfileData& data) -> Result;

// Remove profile by UUID
auto RemoveProfile(const Uuid& uuid) -> Result;

// Rename network profile display name
auto RenameProfile(const Uuid& uuid, const std::string& new_name) -> Result;

// Update Wi-Fi security passphrase
auto ChangePassphrase(const Uuid& uuid, const std::string& new_pass) -> Result;

// Update SSID
auto ChangeSsid(const Uuid& uuid, const std::string& new_ssid) -> Result;

enum class ConnectState {
    None,
    Pending,
    Succeeded,
    Failed,
};

struct ConnectStatus {
    ConnectState state{ConnectState::None};
    Result result{0};
};

// Initiate connection to profile
auto Connect(const Uuid& uuid) -> Result;

// Poll active connection request
auto PollConnect() -> ConnectStatus;

// Cancel and close active connection request
void CancelConnect();

// Toggle wireless communication
auto SetWirelessEnabled(bool enable) -> Result;
auto IsWirelessEnabled() -> bool;

// Check if currently connected to a specific profile
auto IsConnectedTo(const Uuid& uuid) -> bool;

// UUID to formatted hex string helper
auto UuidToString(const Uuid& uuid) -> std::string;

} // namespace sphaira::wifi
