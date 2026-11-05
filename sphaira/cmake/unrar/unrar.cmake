# UnRAR (rarlab.com, "unrarsrc") as a static library: the RAR backend for rar archives
# libarchive cannot read (solid RAR4) and for RAR5 / multi-volume sets.
# License: UnRAR license (license.txt next to this file), not GPL; see that file.
# Built single-threaded (no RAR_SMP) with the DLL API (RARDLL: dll.cpp / dll.hpp).

FetchContent_Declare(unrar
    URL https://www.rarlab.com/rar/unrarsrc-7.3.2.tar.gz
    URL_HASH SHA256=f61a1160d6e2ebbda2ad353c8e82f62ab4312f97fde23c6c2c982d25e0014f99
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # no CMakeLists.txt upstream: the target is defined below.
    SOURCE_SUBDIR NONE
    # UTF-8 file names on newlib - see script.
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/patch_unrar.cmake
)
FetchContent_MakeAvailable(unrar)

# the object list of the upstream makefile's "lib" target, minus threadpool (RAR_SMP only).
set(UNRAR_SOURCES
    rar strlist strfn pathfn smallfn global file filefn filcreat archive arcread unicode
    system crypt crc rawread encname resource match timefn rdwrfn consio options errhnd
    rarvm secpassword rijndael getbits sha1 sha256 blake2s hash extinfo extract volume
    list find unpack headers rs16 cmddata ui largepage filestr scantree dll qopen
)
list(TRANSFORM UNRAR_SOURCES PREPEND "${unrar_SOURCE_DIR}/")
list(TRANSFORM UNRAR_SOURCES APPEND ".cpp")
list(REMOVE_ITEM UNRAR_SOURCES "${unrar_SOURCE_DIR}/rar.cpp") # the command line tool's main()

add_library(unrar STATIC ${UNRAR_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/switch_stubs.c)
# no INTERFACE include dir: UnRAR's log.hpp, file.hpp... would shadow the app's headers.
# Only rar_reader.cpp includes dll.hpp; it gets the folder and _UNIX in sphaira/CMakeLists.txt.
target_compile_definitions(unrar PRIVATE
    RARDLL
    SILENT
    LITTLE_ENDIAN
    __SWITCH__
    _FILE_OFFSET_BITS=64
    # newlib has utimensat() declared but not UTIME_NOW; the value is unused (see switch_stubs.c).
    UTIME_NOW=1073741823
)
target_compile_options(unrar PRIVATE
    # UnRAR reports errors with C++ exceptions and catches them inside dll.cpp;
    # none cross into Kefir Hub code (callbacks never throw).
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
    # the NEON CRC32 / AES paths probe the CPU with getauxval(), which the Switch lacks;
    # use the portable code instead.
    -U__ARM_FEATURE_CRYPTO
    -U__ARM_FEATURE_CRC32
    # third-party code: do not drown the build log.
    -w
)
set_target_properties(unrar PROPERTIES
    CXX_STANDARD 11
    CXX_EXTENSIONS ON
    # keep UnRAR's exception tables out of the -fno-exceptions LTO link of the app.
    INTERPROCEDURAL_OPTIMIZATION OFF
)
