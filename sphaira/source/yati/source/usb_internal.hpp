#pragma once

#include <switch.h>

namespace sphaira::yati::source {

// sphaira's extension to the dbi list request: a backend that recognises
// 'SPHA' in data_size appends "|<size>" to every name it returns;
// a backend that recognises 'SPHQ' appends "|<size>|<selected>".
constexpr u32 DBI_LIST_SIZE_EXT = 0x53504841; // 'SPHA'
constexpr u32 DBI_LIST_QUEUE_EXT = 0x51485053; // 'SPHQ'

} // namespace sphaira::yati::source
