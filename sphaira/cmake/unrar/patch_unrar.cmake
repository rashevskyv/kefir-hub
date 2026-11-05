# Patches the fetched UnRAR source. Run as a FetchContent PATCH_COMMAND
# (working directory = UnRAR source root):
#   ${CMAKE_COMMAND} -P .../unrar/patch_unrar.cmake
#
# The patch is idempotent: skipped if its marker is already present.
#
# File names on the Switch are UTF-8. UnRAR converts wide <-> narrow names with
# wcsrtombs / mbsrtowcs, which newlib's "C" locale limits to ASCII: an archive in
# a folder with a non-ASCII name would not open. UnRAR's Apple branch uses its own
# UTF-8 converters (WideToUtf / UtfToWide); take that branch on the Switch too.
# Only unicode.cpp changes: WideToChar() and CharToWide().
#
# UnRAR license, paragraph 2 (must stay with modified UnRAR source; full text in license.txt):
#   UnRAR source code may be used in any software to handle
#   RAR archives without limitations free of charge, but cannot be
#   used to develop RAR (WinRAR) compatible archiver and to
#   re-create RAR compression algorithm, which is proprietary.
#   Distribution of modified UnRAR source code in separate form
#   or as a part of other software is permitted, provided that
#   full text of this paragraph, starting from "UnRAR source code"
#   words, is included in license, or in documentation if license
#   is not available, and in source code comments of resulting package.

set(file "unicode.cpp")
if(NOT EXISTS "${file}")
    message(FATAL_ERROR "[unrar-patch] ${file} not found (cwd unexpected)")
endif()

file(READ "${file}" src)
if(src MATCHES "defined\\(__SWITCH__\\)")
    message(STATUS "[unrar-patch] unicode.cpp already patched")
    return()
endif()

set(old "#elif defined(_APPLE)\n")
set(new "#elif defined(_APPLE) || defined(__SWITCH__)\n")
string(REPLACE "${old}" "${new}" patched "${src}")
string(REGEX MATCHALL "defined\\(__SWITCH__\\)" hits "${patched}")
list(LENGTH hits count)
if(NOT count EQUAL 2)
    message(FATAL_ERROR "[unrar-patch] expected 2 UTF-8 branches in unicode.cpp, found ${count}: UnRAR changed, update this patch")
endif()
file(WRITE "${file}" "${patched}")
message(STATUS "[unrar-patch] unicode.cpp: UTF-8 names on the Switch")
