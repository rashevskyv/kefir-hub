#pragma once

#include "utils/ownfoil_api.hpp"
#include <yyjson.h>
#include <string>
#include <vector>
#include <stop_token>

namespace sphaira::ownfoil::api {

auto Flatten(const std::string& str) -> std::string;
auto GetBool(yyjson_val* obj, const char* key) -> bool;
auto GetString(yyjson_val* obj, const char* key) -> std::string;
auto IdArray(const std::vector<std::string>& ids) -> std::string;
auto JsonString(const std::string& str) -> std::string;

auto OpenData(yyjson_doc* doc, std::string& error) -> yyjson_val*;
auto OpenApps(yyjson_doc* doc, s64& total, std::string& error) -> yyjson_val*;

void ParseImage(yyjson_val* obj, const std::string& base_url, ShopImage& out);
void ParseImage(yyjson_val* src, const char* key, const std::string& base_url, ShopImage& out);
auto ParseDownload(yyjson_val* item, const std::string& base_url) -> ShopDownload;
auto ParseVersion(yyjson_val* item) -> std::string;

bool PostQuery(const std::string& url, const Config& config, std::stop_token token, const char* body, std::vector<u8>& data, std::string& error);
bool FetchGraphql(const std::string& base_url, const Config& config, std::stop_token token, const std::string& query, const std::string& variables, std::vector<u8>& data, std::string& error);

bool ParseApps(const std::vector<u8>& data, const std::string& base_url, AppType type, std::vector<ShopApp>& out, s64& total, std::string& error);
bool ParseUpdates(const std::vector<u8>& data, std::vector<ShopUpdate>& out, s64& total, std::string& error);
bool ParseTitle(const std::vector<u8>& data, const std::string& base_url, ShopTitle& out, std::string& error);

} // namespace sphaira::ownfoil::api
