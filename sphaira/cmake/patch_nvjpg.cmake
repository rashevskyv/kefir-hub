# Patches the fetched oss-nvjpg library. Run as a FetchContent PATCH_COMMAND
# (working directory = nvjpg source root):
#   ${CMAKE_COMMAND} -P .../patch_nvjpg.cmake
#
# The patch is idempotent: skipped if its marker is already present.
#
# NvMap::free() hands the buffer back to the allocator (delete[]) BEFORE
# nvMapClose() turns the CPU cache back on for those pages, and ignores whether
# that ever succeeded. nvMapCreate marks the buffer uncached
# (svcSetMemoryAttribute mask 8), and DC ZVA - which memset uses for anything
# from ~160 bytes up - data-aborts on non-cacheable memory. So an allocation
# that lands on such a page dies in memset, far away from nvjpg. Close first,
# and if the pages cannot be made cacheable again (nvservices still has them
# device-shared), leak them: a leak costs memory, a poisoned heap page crashes.

set(map_hpp "include/nvjpg/nv/map.hpp")
if(NOT EXISTS "${map_hpp}")
    message(STATUS "[nvjpg-patch] ${map_hpp} not found (cwd unexpected), skipping")
    return()
endif()

file(READ "${map_hpp}" src)
if(src MATCHES "sphaira: restore the cache attribute")
    message(STATUS "[nvjpg-patch] map.hpp already patched")
    return()
endif()

set(old
"            delete[] static_cast<std::uint8_t *>(this->nvmap.cpu_addr);
            nvMapClose(&this->nvmap);
            return 0;")
set(new
"            /* sphaira: restore the cache attribute before the memory goes back
             * to the allocator - memset uses DC ZVA, which data-aborts on the
             * uncached pages nvMapCreate leaves behind. If it cannot be
             * restored, leak the buffer instead of poisoning the heap. */
            auto *mapmem = static_cast<std::uint8_t *>(this->nvmap.cpu_addr);
            auto mapmem_size = this->nvmap.size;
            nvMapClose(&this->nvmap);
            if (mapmem && R_SUCCEEDED(svcSetMemoryAttribute(mapmem, mapmem_size, 8, 0)))
                delete[] mapmem;
            return 0;")

string(REPLACE "${old}" "${new}" src "${src}")
if(NOT src MATCHES "sphaira: restore the cache attribute")
    message(FATAL_ERROR "[nvjpg-patch] failed to apply the NvMap::free() patch to map.hpp")
endif()

file(WRITE "${map_hpp}" "${src}")
message(STATUS "[nvjpg-patch] applied NvMap::free() cache-attribute patch")
