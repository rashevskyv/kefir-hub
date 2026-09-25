#pragma once

#include "title_info.hpp"
#include "defines.hpp"
#include <switch.h>

namespace sphaira::title {

void FakeNacpEntry(ThreadResultData* e);
bool TryParseNacpV2(const u8* raw_nacp, size_t raw_size, char* out_name, char* out_author);
Result LoadControlManual(u64 id, NacpStruct& nacp, ThreadResultData* data);

} // namespace sphaira::title
