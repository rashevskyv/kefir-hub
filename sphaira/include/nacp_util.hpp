#pragma once

#include <switch.h>
#include <cstddef>

namespace sphaira::nacp_util {
namespace detail {

template<typename T>
auto GetLanguageEntries(T& nacp) {
    if constexpr (requires(T value) { value.lang_data.lang; }) {
        return &nacp.lang_data.lang[0];
    } else {
        return &nacp.lang[0];
    }
}

} // namespace detail

inline auto GetLanguageEntry(NacpStruct& nacp, std::size_t index = 0) -> NacpLanguageEntry& {
    return detail::GetLanguageEntries(nacp)[index];
}

inline auto GetLanguageEntry(const NacpStruct& nacp, std::size_t index = 0) -> const NacpLanguageEntry& {
    return detail::GetLanguageEntries(nacp)[index];
}

inline auto GetName(const NacpStruct& nacp, std::size_t index = 0) -> const char* {
    return GetLanguageEntry(nacp, index).name;
}

inline auto GetAuthor(const NacpStruct& nacp, std::size_t index = 0) -> const char* {
    return GetLanguageEntry(nacp, index).author;
}

inline auto GetDisplayVersion(const NacpStruct& nacp) -> const char* {
    return nacp.display_version;
}

} // namespace sphaira::nacp_util
