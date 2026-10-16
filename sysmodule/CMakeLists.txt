project(sphaira_fan
    LANGUAGES C
)

set(SPHAIRA_FAN_PROGRAM_ID 00FF46554E43544C)

add_executable(sphaira_fan
    source/main.c
)

set_target_properties(sphaira_fan PROPERTIES
    C_STANDARD 11
    C_EXTENSIONS ON
)

target_compile_options(sphaira_fan PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

find_program(NX_ELF2NSO_EXE NAMES elf2nso HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
find_program(NX_NPDMTOOL_EXE NAMES npdmtool HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
find_program(NX_BUILD_PFS0_EXE NAMES build_pfs0 HINTS "${DEVKITPRO}/tools/bin" REQUIRED)

set(SPHAIRA_FAN_NSO "${CMAKE_CURRENT_BINARY_DIR}/sphaira_fan.nso")
set(SPHAIRA_FAN_NPDM "${CMAKE_CURRENT_BINARY_DIR}/sphaira_fan.npdm")
set(SPHAIRA_FAN_EXEFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/exefs")
set(SPHAIRA_FAN_EXEFS_NSP "${CMAKE_CURRENT_BINARY_DIR}/exefs.nsp")
set(SPHAIRA_FAN_CONTENTS_DIR "${CMAKE_BINARY_DIR}/atmosphere/contents/${SPHAIRA_FAN_PROGRAM_ID}")

add_custom_command(
    OUTPUT "${SPHAIRA_FAN_NSO}"
    COMMAND ${NX_ELF2NSO_EXE} "$<TARGET_FILE:sphaira_fan>" "${SPHAIRA_FAN_NSO}"
    DEPENDS sphaira_fan
    COMMENT "Converting sphaira_fan to NSO format"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SPHAIRA_FAN_NPDM}"
    COMMAND ${NX_NPDMTOOL_EXE} "${CMAKE_CURRENT_SOURCE_DIR}/sphaira_fan.json" "${SPHAIRA_FAN_NPDM}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sphaira_fan.json"
    COMMENT "Generating NPDM for sphaira_fan"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SPHAIRA_FAN_EXEFS_NSP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${SPHAIRA_FAN_EXEFS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPHAIRA_FAN_EXEFS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${SPHAIRA_FAN_NSO}" "${SPHAIRA_FAN_EXEFS_DIR}/main"
    COMMAND ${CMAKE_COMMAND} -E copy "${SPHAIRA_FAN_NPDM}" "${SPHAIRA_FAN_EXEFS_DIR}/main.npdm"
    COMMAND ${NX_BUILD_PFS0_EXE} "${SPHAIRA_FAN_EXEFS_DIR}" "${SPHAIRA_FAN_EXEFS_NSP}"
    DEPENDS "${SPHAIRA_FAN_NSO}" "${SPHAIRA_FAN_NPDM}"
    COMMENT "Packing sphaira_fan ExeFS"
    VERBATIM
)

add_custom_target(sphaira_fan_exefs ALL
    DEPENDS "${SPHAIRA_FAN_EXEFS_NSP}"
)

# sphaira embeds this in its romfs so the module can be installed on demand
set(SPHAIRA_FAN_EXEFS_NSP "${SPHAIRA_FAN_EXEFS_NSP}" PARENT_SCOPE)

add_custom_command(
    TARGET sphaira_fan_exefs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPHAIRA_FAN_CONTENTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${SPHAIRA_FAN_EXEFS_NSP}" "${SPHAIRA_FAN_CONTENTS_DIR}/exefs.nsp"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/toolbox.json" "${SPHAIRA_FAN_CONTENTS_DIR}/toolbox.json"
)
