#pragma once

#include "ui/menus/ghdl.hpp"
#include "ui/progress_box.hpp"
#include <string>
#include <vector>

namespace sphaira::ui::menu::gh {

constexpr auto CACHE_PATH = "/switch/sphaira/cache/github";

auto GenerateApiUrl(const Entry& e) -> std::string;
void from_json(const fs::FsPath& path, Entry& e);
auto DownloadReleaseJsonJson(ProgressBox* pbox, const std::string& url, std::vector<GhApiEntry>& out) -> Result;
auto DownloadApp(ProgressBox* pbox, const GhApiAsset& asset, const AssetEntry* matched) -> Result;
void DoDirectLinkDownload(const std::string& url);
void OpenDirectLinkPrompt(std::string filled = {});

} // namespace sphaira::ui::menu::gh
