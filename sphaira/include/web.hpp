#pragma once

#include "fs.hpp"
#include <switch.h>
#include <string>
#include <vector>

namespace sphaira {

struct WebShareEntry {
    fs::FsPath path{};
    std::string name{};
};

struct WebShareResult {
    std::string url{};
    int qr_image{};
};

auto WebShow(const std::string& url) -> Result;
auto WebShareImages(const std::vector<WebShareEntry>& entries, WebShareResult& out) -> Result;
auto WebShareFolder(const fs::FsPath& path, WebShareResult& out) -> Result;
void WebShareStop();

} // namespace sphaira
