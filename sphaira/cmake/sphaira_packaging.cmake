# Packaging, RomFS assets, and NRO generation for Sphaira / Kefir Hub

# copy the romfs
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# create assets target
dkp_add_asset_target(sphaira_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

file(GLOB_RECURSE sphaira_romfs_source_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    ${CMAKE_SOURCE_DIR}/assets/romfs/*
)
add_custom_target(sphaira_romfs_sync
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        ${CMAKE_CURRENT_BINARY_DIR}/romfs/avatars
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/romfs
        ${CMAKE_CURRENT_BINARY_DIR}/romfs
    DEPENDS ${sphaira_romfs_source_files}
)
add_dependencies(sphaira_romfs sphaira_romfs_sync)
set_property(TARGET sphaira_romfs APPEND PROPERTY DKP_ASSET_FILES ${sphaira_romfs_source_files})

# wait until hbl is built first as we need the exefs to embed
add_dependencies(sphaira hbl_nso hbl_npdm)

# set the embed path for assets and hbl
target_compile_options(sphaira PRIVATE
    --embed-dir=${CMAKE_SOURCE_DIR}/assets/embed
    --embed-dir=${CMAKE_BINARY_DIR}/hbl
)

# add nanovg shaders to romfs
dkp_install_assets(sphaira_romfs
    DESTINATION shaders
    TARGETS
        fill_aa_fsh
        fill_fsh
        fill_vsh
)

# create nacp
nx_generate_nacp(
    OUTPUT kefir-hub.nacp
    NAME "Kefir Hub"
    AUTHOR Kefir
    VERSION ${sphaira_VERSION}
)

# create nro
nx_create_nro(sphaira
    OUTPUT ${CMAKE_BINARY_DIR}/kefir-hub.nro
    ICON ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP kefir-hub.nacp
    ROMFS sphaira_romfs
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/switch/kefir-hub)

add_custom_command(
    TARGET sphaira_nro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/kefir-hub.nro
        ${CMAKE_BINARY_DIR}/switch/kefir-hub/kefir-hub.nro
)

message(STATUS "generating nro in: ${CMAKE_BINARY_DIR}/kefir-hub.nro")
message(STATUS "run nxlink -s ${CMAKE_BINARY_DIR}/kefir-hub.nro")
