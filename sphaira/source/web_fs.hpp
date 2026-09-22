#pragma once

#include "fs.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira {

struct RootSource {
    std::string path; // "/", "/config", "ums0:/"
    std::string name;
    std::string meta;
};

auto GetMountRoots() -> std::vector<std::string>;
auto GetMountRoot() -> std::string;
auto GetRootSources() -> std::vector<RootSource>;
auto SourceRootFor(const std::string& path) -> std::string;
auto SourceNameFor(const std::string& root) -> std::string;
auto OpenFs(std::string_view path) -> std::unique_ptr<fs::Fs>;

} // namespace sphaira
