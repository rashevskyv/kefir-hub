#pragma once

#include <switch.h>
#include <string_view>

namespace sphaira::yati::source {

// sphaira's extension to the dbi list request: a backend that recognises
// 'SPHA' in data_size appends "|<size>" to every name it returns;
// a backend that recognises 'SPHQ' appends "|<size>|<selected>".
constexpr u32 DBI_LIST_SIZE_EXT = 0x53504841; // 'SPHA'
constexpr u32 DBI_LIST_QUEUE_EXT = 0x51485053; // 'SPHQ'
constexpr std::string_view DBI_SPHQ_EMPTY_MARKER = "::SPHQ::";
constexpr std::string_view DBI_SPHQ_EMPTY_PAYLOAD = "::SPHQ::\n";
constexpr std::string_view DBI_SPHQ_REV_PREFIX = "::SPHQ_REV::|";

} // namespace sphaira::yati::source
