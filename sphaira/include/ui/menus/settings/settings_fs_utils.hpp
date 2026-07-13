#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui::menu::settings::detail {


auto ReadTextFile(const std::string& path) -> std::string;
auto ReadLines(const std::string& path) -> std::vector<std::string>;
auto WriteLines(const std::string& path, const std::vector<std::string>& lines) -> Result;
auto StartsWith(const std::string& str, const char* prefix) -> bool;
auto SplitCommand(const std::string& line) -> std::vector<std::string>;
auto ExtractBracketName(const std::string& line) -> std::string;
auto ExtractIniKey(const std::string& line) -> std::string;
auto ExtractJsonStringField(const std::string& json, const std::string& field) -> std::string;


auto ParentPath(const std::string& path) -> std::string;
auto EnsureParentDirectory(const std::string& path) -> Result;

auto CopyFileSimple(const std::string& src, const std::string& dst) -> Result;
auto DeletePath(const std::string& path) -> Result;
auto CopyDirectoryContents(const std::string& src, const std::string& dst) -> Result;
auto MovePath(const std::string& src, const std::string& dst) -> Result;

auto IniValueEquals(const char* path, const char* section, const char* key, const char* value) -> bool;
auto SetIniValue(const char* path, const char* section, const char* key, const char* value) -> Result;
auto ReadIniRawValue(const std::string& path, const std::string& section, const std::string& key) -> std::string;
auto SetIniRawValue(const std::string& path, const std::string& section, const std::string& key, const std::string& value) -> Result;

} // namespace sphaira::ui::menu::settings::detail
