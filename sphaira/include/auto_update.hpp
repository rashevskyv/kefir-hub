#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <cctype>
#include <algorithm>
#include <cstring>

#ifdef __SWITCH__
#include "fs.hpp"
#endif

namespace sphaira::auto_update {

enum class Mode : long {
    Off = 0,
    Silent = 1,
    Notify = 2,
    OnDemand = 3,
};

enum class JobState : std::uint8_t {
    Idle = 0,
    Checking,
    Available,
    Downloading,
    Installing,
    Ready,
    Failed,
};

struct Job {
    JobState state{JobState::Idle};
    float progress{};
    std::string version{};
    std::string url{};
};

struct ReleaseAsset {
    std::string name{};
    std::string browser_download_url{};
    std::string content_type{};
    uint64_t size{0};
};

inline std::string ToLower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

inline std::string_view GetBasename(std::string_view path) {
    auto pos = path.find_last_of("/\\");
    if (pos != std::string_view::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

inline int ScoreAsset(std::string_view asset_name, std::string_view running_exe_basename) {
    const std::string lower_asset = ToLower(asset_name);
    const std::string lower_running = ToLower(running_exe_basename);

    const bool is_nro = lower_asset.ends_with(".nro");
    const bool is_zip = lower_asset.ends_with(".zip");

    if (!is_nro && !is_zip) {
        return 0;
    }

    // Exact name match
    if (lower_asset == lower_running) {
        return 100;
    }

    const bool running_is_kefir = lower_running.find("kefir") != std::string::npos;
    const bool running_is_sphaira = lower_running.find("sphaira") != std::string::npos;

    if (running_is_kefir) {
        if (is_nro && lower_asset.find("kefir") != std::string::npos) {
            return 90;
        }
        if (is_nro && lower_asset.find("sphaira") != std::string::npos) {
            return 85;
        }
        if (is_nro) {
            return 80;
        }
        if (is_zip && lower_asset.find("kefir") != std::string::npos) {
            return 70;
        }
        if (is_zip) {
            return 60;
        }
    } else if (running_is_sphaira) {
        if (is_nro && lower_asset.find("sphaira") != std::string::npos) {
            return 90;
        }
        if (is_nro && lower_asset.find("kefir") != std::string::npos) {
            return 85;
        }
        if (is_nro) {
            return 80;
        }
        if (is_zip && lower_asset.find("sphaira") != std::string::npos) {
            return 70;
        }
        if (is_zip) {
            return 60;
        }
    } else {
        // hbmenu or generic name
        if (is_nro && (lower_asset.find("kefir-hub") != std::string::npos || lower_asset.find("sphaira") != std::string::npos)) {
            return 90;
        }
        if (is_nro) {
            return 80;
        }
        if (is_zip) {
            return 60;
        }
    }

    return 0;
}

inline int SelectBestAsset(std::span<const ReleaseAsset> assets, std::string_view running_exe_path) {
    const std::string_view basename = GetBasename(running_exe_path);
    int best_index = -1;
    int best_score = 0;

    for (size_t i = 0; i < assets.size(); ++i) {
        const int score = ScoreAsset(assets[i].name, basename);
        if (score > best_score) {
            best_score = score;
            best_index = static_cast<int>(i);
        }
    }

    return best_index;
}

#ifdef __SWITCH__
// Resolves the destination install path for the NRO update.
fs::FsPath ResolveInstallDestination(const fs::FsPath& running_exe_path);

// Atomically installs the staging file to destination (and /hbmenu.nro if replace_hbmenu is enabled).
bool InstallNroUpdate(const fs::FsPath& staging_path, const fs::FsPath& dest_path, bool replace_hbmenu);

auto GetJob() -> Job;
void SetJobState(JobState state);
void SetJobProgress(float progress);
void SetAvailable(std::string version, std::string url);
auto ConsumeNotifyPrompt() -> bool;
void StartDownload();
#endif

} // namespace sphaira::auto_update

