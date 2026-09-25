#include "utils/utils.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"

#include <cstring>
#include <cstdio>
#include <switch.h>
#include <vector>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <compare>

namespace sphaira::utils {
namespace {

HashStr hexIdToStrInternal(auto id) {
    HashStr str{};
    const auto id_lower = std::byteswap(*(u64*)id.c);
    const auto id_upper = std::byteswap(*(u64*)(id.c + 0x8));
    std::snprintf(str.str, 0x21, "%016lx%016lx", id_lower, id_upper);
    return str;
}

std::string formatSizeInetrnal(double size, double base) {
    static const char* const suffixes[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
    size_t suffix_index = 0;

    while (size >= base && suffix_index < std::size(suffixes) - 1) {
        size /= base;
        suffix_index++;
    }

    char buffer[32];
    if (suffix_index == 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", size, suffixes[suffix_index]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, suffixes[suffix_index]);
    }

    return buffer;
}

} // namespace

HashStr hexIdToStr(FsRightsId id) {
    return hexIdToStrInternal(id);
}

HashStr hexIdToStr(NcmRightsId id) {
    return hexIdToStrInternal(id.rights_id);
}

HashStr hexIdToStr(NcmContentId id) {
    return hexIdToStrInternal(id);
}

std::string formatSizeStorage(u64 size) {
    return formatSizeInetrnal(size, 1024.0);
}

std::string Trim(std::string str) {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    str = str.substr(first, last - first + 1);
    if (str.size() >= 2 && ((str.front() == '\'' && str.back() == '\'') || (str.front() == '"' && str.back() == '"'))) {
        str = str.substr(1, str.size() - 2);
    }
    return str;
}

std::string TrimAsciiWhitespace(std::string value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }

    size_t start{};
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        start++;
    }

    if (start) {
        value.erase(0, start);
    }
    return value;
}

} // namespace sphaira::utils
