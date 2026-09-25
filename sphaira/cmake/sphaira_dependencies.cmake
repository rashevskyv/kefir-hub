include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(ftpsrv
    GIT_REPOSITORY https://github.com/ITotalJustice/ftpsrv.git
    GIT_TAG 85b3cf0
    SOURCE_SUBDIR NONE
    # allow anonymous (no login) FTP access with any/empty username - see script.
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_ftpsrv.cmake
)

FetchContent_Declare(libhaze
    GIT_REPOSITORY https://github.com/ITotalJustice/libhaze.git
    GIT_TAG 0be1523
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_libhaze.cmake
)

FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG 845c9fc
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 5c20573
)

FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.11.1
)

FetchContent_Declare(minIni
    GIT_REPOSITORY https://github.com/ITotalJustice/minIni-nx.git
    GIT_TAG 6e952b6
)

FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.7
    SOURCE_SUBDIR build/cmake
)

FetchContent_Declare(libusbhsfs
    GIT_REPOSITORY https://github.com/ITotalJustice/libusbhsfs.git
    GIT_TAG d0a973e
)

FetchContent_Declare(libnxtc
    GIT_REPOSITORY https://github.com/ITotalJustice/libnxtc.git
    GIT_TAG 0d369b8
)

FetchContent_Declare(nvjpg
    GIT_REPOSITORY https://github.com/ITotalJustice/oss-nvjpg.git
    GIT_TAG 45680e7
    # NvMap::free() frees the decode buffer while it is still marked uncached - see script.
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_nvjpg.cmake
)

FetchContent_Declare(libsmb2
    GIT_REPOSITORY https://github.com/sahlberg/libsmb2.git
    GIT_TAG v4.0.0
    PATCH_COMMAND patch -p1 -N -i ${CMAKE_CURRENT_SOURCE_DIR}/../assets/patches/libsmb2.patch || true
)

FetchContent_Declare(libnfs
    GIT_REPOSITORY https://github.com/ITotalJustice/libnfs.git
    GIT_TAG 65f3e11
)

set(USE_NEW_ZSTD ON)
# has issues with some homebrew and game icons (oxenfree, overwatch2).
set(USE_NVJPG ON)

set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_COMPRESSION OFF)
set(ZSTD_BUILD_DECOMPRESSION ON)
set(ZSTD_BUILD_DICTBUILDER OFF)
set(ZSTD_LEGACY_SUPPORT OFF)
set(ZSTD_MULTITHREAD_SUPPORT OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_TESTS OFF)

set(MININI_LIB_NAME minIni)
set(MININI_USE_STDIO ON)
set(MININI_USE_NX OFF)
set(MININI_USE_FLOAT OFF)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(NANOVG_DEBUG ON)
endif()
set(NANOVG_NO_JPEG OFF)
set(NANOVG_NO_PNG OFF)
set(NANOVG_NO_BMP ON)
set(NANOVG_NO_PSD ON)
set(NANOVG_NO_TGA ON)
set(NANOVG_NO_GIF ON)
set(NANOVG_NO_HDR ON)
set(NANOVG_NO_PIC ON)
set(NANOVG_NO_PNM ON)

set(YYJSON_DISABLE_READER OFF)
set(YYJSON_DISABLE_WRITER OFF)
set(YYJSON_DISABLE_UTILS ON)
set(YYJSON_DISABLE_FAST_FP_CONV ON)
set(YYJSON_DISABLE_NON_STANDARD ON)
set(YYJSON_DISABLE_UTF8_VALIDATION ON)
set(YYJSON_DISABLE_UNALIGNED_MEMORY_ACCESS OFF)

# enable this if you want ntfs and ext4 support, at the cost of a huge final binary size.
set(USBHSFS_GPL OFF)
set(USBHSFS_SXOS_DISABLE ON)

set(ENABLE_DOCUMENTATION OFF CACHE BOOL "Build documentation" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "Build example programs" FORCE)
set(ENABLE_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_OpenSSL ON)
set(CMAKE_DISABLE_FIND_PACKAGE_GSSAPI ON)
set(OPENSSL_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR})
add_compile_definitions(__SWITCH__)

FetchContent_MakeAvailable(
    ftpsrv
    libhaze
    nanovg
    stb
    minIni
    yyjson
    zstd
    libusbhsfs
    libnxtc
    nvjpg
    libsmb2
    libnfs
)

target_compile_definitions(libhaze PRIVATE -DSPHAIRA_VERSION="${sphaira_VERSION}")
target_compile_definitions(smb2 PRIVATE -D__SWITCH__)
target_sources(smb2 PRIVATE ${libsmb2_SOURCE_DIR}/lib/compat.c)

if (EXISTS "$ENV{DEVKITPRO}/libnx/include/switch/nacp.h")
    file(READ "$ENV{DEVKITPRO}/libnx/include/switch/nacp.h" LIBNX_NACP_HEADER)
    if (LIBNX_NACP_HEADER MATCHES "NacpLanguageEntryData[ \t\r\n]+lang_data")
        file(READ "${libnxtc_SOURCE_DIR}/source/nxtc.c" LIBNXTC_SOURCE)
        string(REPLACE "nacp->lang_data.lang_data.lang" "nacp->lang_data.lang" LIBNXTC_SOURCE "${LIBNXTC_SOURCE}")
        string(REPLACE "nacp->lang[" "nacp->lang_data.lang[" LIBNXTC_SOURCE "${LIBNXTC_SOURCE}")
        file(WRITE "${libnxtc_SOURCE_DIR}/source/nxtc.c" "${LIBNXTC_SOURCE}")
    endif()
endif()

# PATCH_COMMAND only runs when the dependency is (re)populated, so an existing
# _deps checkout never picks up a newly added patch. the script is idempotent,
# so just run it again at configure time.
execute_process(
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_ftpsrv.cmake
    WORKING_DIRECTORY ${ftpsrv_SOURCE_DIR}
    RESULT_VARIABLE ftpsrv_patch_result
)
if (NOT ftpsrv_patch_result EQUAL 0)
    message(FATAL_ERROR "failed to patch ftpsrv: ${ftpsrv_patch_result}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_libhaze.cmake
    WORKING_DIRECTORY ${libhaze_SOURCE_DIR}
    RESULT_VARIABLE libhaze_patch_result
)
if (NOT libhaze_patch_result EQUAL 0)
    message(FATAL_ERROR "failed to patch libhaze: ${libhaze_patch_result}")
endif()

if (USE_NVJPG)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_nvjpg.cmake
        WORKING_DIRECTORY ${nvjpg_SOURCE_DIR}
        RESULT_VARIABLE nvjpg_patch_result
    )
    if (NOT nvjpg_patch_result EQUAL 0)
        message(FATAL_ERROR "failed to patch nvjpg: ${nvjpg_patch_result}")
    endif()
endif()

set(FTPSRV_LIB_BUILD TRUE)
set(FTPSRV_LIB_VFS_CUSTOM ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs_nx.h)
set(FTPSRV_LIB_PATH_SIZE 0x301)
set(FTPSRV_LIB_SESSIONS 16)
# the transfer loop moves at most one buffer per poll, so a bigger buffer means
# fewer round trips (and fewer LED/progress callbacks) per megabyte. upstream's
# sysmod build uses the same 512 KiB.
set(FTPSRV_LIB_BUF_SIZE 1024*512)

set(FTPSRV_LIB_CUSTOM_DEFINES
    USE_VFS_SAVE=$<BOOL:FALSE>
    USE_VFS_STORAGE=$<BOOL:TRUE>
    # disabled as it may conflict with the gamecard menu.
    USE_VFS_GC=$<BOOL:FALSE>
    USE_VFS_USBHSFS=$<BOOL:TRUE>
    VFS_NX_BUFFER_IO=$<BOOL:TRUE>
    # let sphaira handle init / closing of the hdd.
    USE_VFS_USBHSFS_INIT=$<BOOL:FALSE>
    # disable romfs mounting as otherwise we cannot write / modify sphaira.nro
    USE_VFS_ROMFS=$<BOOL:FALSE>
    FTP_SOCKET_HEADER="${ftpsrv_SOURCE_DIR}/src/platform/nx/socket_nx.h"
)

add_subdirectory(${ftpsrv_SOURCE_DIR} binary_dir)

add_library(ftpsrv_helper
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs_nx.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs/vfs_nx_none.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs/vfs_nx_root.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs/vfs_nx_fs.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs/vfs_nx_storage.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs/vfs_nx_stdio.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/vfs/vfs_nx_hdd.c
    ${ftpsrv_SOURCE_DIR}/src/platform/nx/utils.c
)

target_link_libraries(ftpsrv_helper PUBLIC ftpsrv libusbhsfs)
target_include_directories(ftpsrv_helper PUBLIC ${ftpsrv_SOURCE_DIR}/src/platform)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(libnxtc
    ${libnxtc_SOURCE_DIR}/source/nxtc.c
    ${libnxtc_SOURCE_DIR}/source/nxtc_log.c
    ${libnxtc_SOURCE_DIR}/source/nxtc_utils.c
)
target_include_directories(libnxtc PUBLIC ${libnxtc_SOURCE_DIR}/include)

if (USE_NVJPG)
    add_library(nvjpg
        ${nvjpg_SOURCE_DIR}/lib/decoder.cpp
        ${nvjpg_SOURCE_DIR}/lib/image.cpp
        ${nvjpg_SOURCE_DIR}/lib/surface.cpp
    )
    target_include_directories(nvjpg PUBLIC ${nvjpg_SOURCE_DIR}/include)
    set_target_properties(nvjpg PROPERTIES CXX_STANDARD 26)
endif()
