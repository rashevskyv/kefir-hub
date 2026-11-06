# hacBrewPackLZ (gitlab.com/ismaellozanoperezserrano/hacbrewpacklz) switch engine as a static library:
# rebuilds installed games with their RomFS LZ4 compressed through the NCA compression layer (FW 12.0.0+).
# License: GPL-2.0 upstream; the author agreed (2026-11-05) to relicense so it can go into this GPL-3.0 app.
# Pending: the relicense commit upstream. Until then this pin is the agreed version.
# Built: switch/core (the engine) + switch/nx (console services), minus nx_main.cpp (their app's main) and app/ (their UI).
# Kefir Hub talks to it only through hbp_bridge.cpp, compiled into this library.

FetchContent_Declare(hbp
    GIT_REPOSITORY https://gitlab.com/ismaellozanoperezserrano/hacbrewpacklz.git
    GIT_TAG b32546855b33b7b868e9c5ea9635225e924e4caf # v1.4.3
    # no CMakeLists.txt upstream: the target is defined below.
    SOURCE_SUBDIR NONE
)
FetchContent_MakeAvailable(hbp)

file(GLOB HBP_SOURCES ${hbp_SOURCE_DIR}/switch/core/*.cpp)
list(APPEND HBP_SOURCES
    ${hbp_SOURCE_DIR}/switch/nx/nx_console.cpp
    ${hbp_SOURCE_DIR}/switch/nx/nx_sys.cpp
    ${hbp_SOURCE_DIR}/switch/nx/nx_remote.cpp
    ${hbp_SOURCE_DIR}/switch/nx/nx_ipc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/hbp_bridge.cpp
)

add_library(hbp STATIC ${HBP_SOURCES})
target_include_directories(hbp PRIVATE
    ${hbp_SOURCE_DIR}/switch
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(hbp PRIVATE libusbhsfs)
target_compile_definitions(hbp PRIVATE __SWITCH__)
target_compile_options(hbp PRIVATE
    # the engine reports errors with C++ exceptions; hbp_bridge.cpp catches all of them,
    # none cross into Kefir Hub code.
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
    # third-party code: do not drown the build log.
    -w
)
set_target_properties(hbp PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS ON
    # keep the engine's exception tables out of the -fno-exceptions LTO link of the app.
    INTERPROCEDURAL_OPTIMIZATION OFF
)
