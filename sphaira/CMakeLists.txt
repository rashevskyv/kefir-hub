cmake_minimum_required(VERSION 3.13)

set(sphaira_VERSION 0.13.879)

project(sphaira
    VERSION ${sphaira_VERSION}
    LANGUAGES C CXX
)

find_package(Git REQUIRED)


execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" symbolic-ref --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(sphaira_VERSION_HASH "${sphaira_VERSION} [${GIT_COMMIT}]")

add_executable(sphaira
    source/ui/menus/appstore.cpp
    source/ui/menus/appstore/appstore_repo.cpp
    source/ui/menus/appstore/appstore_ops.cpp
    source/ui/menus/appstore/appstore_entry_menu.cpp
    source/ui/menus/appstore/appstore_filter.cpp
    source/ui/menus/file_viewer.cpp
    source/ui/menus/file_viewer/file_viewer_internal.cpp
    source/ui/menus/file_viewer/file_viewer_text.cpp
    source/ui/menus/file_viewer/file_viewer_text_layout.cpp
    source/ui/menus/file_viewer/file_viewer_text_index.cpp
    source/ui/menus/file_viewer/file_viewer_image.cpp
    source/ui/menus/filebrowser.cpp
    source/ui/menus/filebrowser/filebrowser_internal.cpp
    source/ui/menus/filebrowser/filebrowser_view.cpp
    source/ui/menus/filebrowser/filebrowser_view_draw.cpp
    source/ui/menus/filebrowser/filebrowser_scan.cpp
    source/ui/menus/filebrowser/filebrowser_metadata.cpp
    source/ui/menus/filebrowser/filebrowser_options.cpp
    source/ui/menus/filebrowser/filebrowser_options_advanced.cpp
    source/ui/menus/filebrowser/filebrowser_share.cpp
    source/ui/menus/filebrowser/filebrowser_sources.cpp
    source/ui/menus/filebrowser/filebrowser_assoc.cpp
    source/ui/menus/filebrowser/filebrowser_forwarder.cpp
    source/ui/menus/filebrowser/filebrowser_ops.cpp
    source/ui/menus/filebrowser/filebrowser_ops_archive.cpp
    source/ui/menus/file_picker.cpp
    source/ui/menus/file_picker_draw.cpp
    source/ui/menus/homebrew.cpp
    source/ui/menus/homebrew_scan.cpp
    source/ui/menus/homebrew_ops.cpp
    source/ui/menus/install_share.cpp

    source/ui/menus/kefir_menu.cpp
    source/ui/menus/kefir/kefir_downgrade_box.cpp
    source/ui/menus/kefir/kefir_links.cpp
    source/ui/menus/kefir/kefir_draw.cpp
    source/ui/menus/kefir/kefir_ops.cpp
    source/ui/menus/kefir/kefir_changelog.cpp
    source/ui/menus/kefir/kefir_changelog_parser.cpp
    source/ui/menus/kefir/kefir_firmware.cpp
    source/ui/menus/kefir/kefir_firmware_ops.cpp
    source/ui/menus/main_menu.cpp
    source/ui/menus/menu_base.cpp
    source/ui/menus/menu_base_draw.cpp

    source/ui/menus/settings_menu.cpp
    source/ui/menus/settings/settings_menu_draw.cpp
    source/ui/menus/settings/settings_internal.cpp
    source/ui/menus/settings/settings_categories.cpp
    source/ui/menus/settings/settings_categories_items.cpp
    source/ui/menus/settings/settings_software.cpp
    source/ui/menus/settings/settings_dbi.cpp
    source/ui/menus/settings/settings_kefir.cpp
    source/ui/menus/settings/settings_themes.cpp
    source/ui/menus/settings/settings_translate.cpp
    source/ui/menus/settings/settings_sources.cpp
    source/ui/menus/settings/settings_fs_utils.cpp
    source/ui/menus/settings/settings_translations.cpp
    source/ui/menus/settings/settings_tweaks.cpp
    source/ui/menus/settings/settings_fancurve.cpp
    source/ui/menus/settings/settings_fancurve_draw.cpp
    source/ui/menus/settings/settings_fancurve_ops.cpp
    source/ui/menus/save_menu.cpp
    source/ui/menus/save/save_hub_menu.cpp
    source/ui/menus/save/save_paths.cpp
    source/ui/menus/save/save_discovery.cpp
    source/ui/menus/save/save_metadata_decoders.cpp
    source/ui/menus/save/save_archive_metadata.cpp
    source/ui/menus/save/save_backup_inspection.cpp
    source/ui/menus/save/save_locations.cpp
    source/ui/menus/save/save_menu_detail.cpp
    source/ui/menus/save/save_menu_draw.cpp
    source/ui/menus/save/save_menu_filters.cpp
    source/ui/menus/save/save_menu_options.cpp
    source/ui/menus/save/save_menu_scan.cpp
    source/ui/menus/save/save_menu_target.cpp
    source/ui/menus/save/save_menu_catalog.cpp
    source/ui/menus/save/save_menu_actions.cpp
    source/ui/menus/save/save_backup_pub.cpp
    source/ui/menus/save/save_backup_writer.cpp
    source/ui/menus/save/save_deletion.cpp
    source/ui/menus/save/save_folder_restore.cpp
    source/ui/menus/save/save_menu_ops.cpp
    source/ui/menus/save/save_remote_sync.cpp
    source/ui/menus/save/save_restore_route.cpp
    source/ui/menus/save/save_restore_zip.cpp
    source/ui/menus/save/save_slot_backend.cpp
    source/ui/menus/themezer.cpp
    source/ui/menus/themezer/themezer_api.cpp
    source/ui/menus/themezer/themezer_ops.cpp
    source/ui/menus/themezer/themezer_dialogs.cpp
    source/ui/menus/theme_creator.cpp
    source/ui/menus/tools_menu.cpp
    source/ui/menus/wifi_menu.cpp
    source/wifi_manager.cpp
    source/ui/menus/uninstaller_menu.cpp
    source/ui/menus/uninstaller_ops.cpp
    source/ui/menus/users_menu.cpp
    source/ui/menus/users/users_internal.cpp
    source/ui/menus/users/users_profile.cpp
    source/ui/menus/users/users_profile_dialogs.cpp
    source/ui/menus/users/users_manage.cpp
    source/ui/menus/users/users_manage_backups.cpp
    source/ui/menus/users/users_manage_ops.cpp
    source/ui/menus/users/users_restore.cpp
    source/ui/menus/users/users_restore_worker.cpp
    source/ui/menus/users/users_restore_library.cpp
    source/ui/menus/users/users_restore_remote.cpp
    source/ui/menus/users/users_restore_remote_parser.cpp
    source/ui/menus/users/users_nand.cpp
    source/ui/menus/users/users_nand_library.cpp
    source/ui/menus/users/users_nand_library_ops.cpp
    source/account/account_link.cpp
    source/account/account_link_users.cpp
    source/account/account_link_donor.cpp
    source/account/account_link_pkg.cpp
    source/account/account_user.cpp
    source/account/account_user_export.cpp
    source/account/account_user_packs.cpp
    source/account/account_playtime.cpp
    source/account/account_restore.cpp
    source/account/account_restore_safety.cpp
    source/account/nand_transfer.cpp
    source/account/nand_transfer_archive.cpp
    source/account/nand_transfer_users.cpp
    source/meminfo.cpp
    source/ui/menus/cheats_menu.cpp
    source/ui/menus/cheats/cheats_game_scanner.cpp
    source/ui/menus/cheats/cheats_ops.cpp
    source/ui/menus/cheats/cheats_dmnt.cpp
    source/ui/menus/cheats/cheats_lookup.cpp
    source/ui/menus/cheats/cheats_db.cpp
    source/ui/menus/cheats/cheat_files_menu.cpp
    source/ui/menus/cheats/cheat_content_menu.cpp
    source/ui/menus/cheats/cheat_game_select_menu.cpp
    source/ui/menus/cheats/cheat_game_select_scan.cpp
    source/ui/menus/cheats/cheat_download_menu.cpp
    source/ui/menus/cheats/cheat_download_parsers.cpp
    source/ui/menus/cheats/cheat_download_net.cpp
    source/ui/menus/cheats/cheat_download_writer.cpp
    source/ui/menus/ghdl.cpp
    source/ui/menus/ghdl_api.cpp
    source/ui/menus/dbi_menu.cpp
    source/ui/menus/dbi/dbi_menu_options.cpp
    source/ui/menus/dbi/dbi_internal.cpp
    source/ui/menus/dbi/dbi_draw.cpp
    source/ui/menus/dbi/dbi_draw_transfer.cpp
    source/ui/menus/dbi/dbi_usb.cpp
    source/ui/menus/dbi/dbi_local.cpp
    source/ui/menus/dbi/dbi_plan.cpp
    source/ui/menus/dbi/dbi_session.cpp

    source/ui/menus/gc_menu.cpp
    source/ui/menus/gc_menu_ops.cpp
    source/ui/menus/game_menu.cpp
    source/ui/menus/game/game_internal.cpp
    source/ui/menus/game/game_details.cpp
    source/ui/menus/game/game_details_ops.cpp
    source/ui/menus/game/game_save_manager.cpp
    source/ui/menus/game/game_scan.cpp
    source/ui/menus/game/game_ops.cpp
    source/ui/menus/grid_menu_base.cpp
    source/ui/menus/install_stream_menu_base.cpp
    source/ui/menus/install_stream.cpp

    source/ui/error_box.cpp
    source/ui/about_box.cpp
    source/ui/notification.cpp
    source/ui/nvg_util.cpp
    source/ui/image_viewport.cpp
    source/ui/action_icon.cpp
    source/ui/option_box.cpp
    source/ui/popup_list.cpp
    source/ui/zip_extract_box.cpp
    source/ui/progress_box.cpp
    source/ui/progress_box_draw.cpp
    source/ui/screensaver.cpp
    source/ui/scrollable_text.cpp
    source/ui/hold_confirm_box.cpp
    source/ui/sidebar.cpp
    source/ui/sidebar_entries.cpp
    source/ui/widget.cpp
    source/ui/list.cpp
    source/ui/list_draw.cpp
    source/ui/scrolling_text.cpp
    source/ui/forwarder_editor.cpp
    source/ui/image_crop.cpp
    source/ui/file_icon.cpp
    source/ui/steamgriddb_icon.cpp
    source/ui/steamgriddb_widgets.cpp
    source/ui/remote_input.cpp

    source/app.cpp
    source/app_startup.cpp
    source/app_loop.cpp
    source/app_frame.cpp
    source/app_ui_stack.cpp
    source/app_usb.cpp
    source/app_settings.cpp
    source/app_mtp_settings.cpp
    source/app_network_settings.cpp
    source/app_save_settings.cpp
    source/app_theme.cpp
    source/app_display_options.cpp
    source/download.cpp
    source/download_cache.cpp
    source/download_curl_callbacks.cpp
    source/download_curl_transfer.cpp
    source/download_webdav.cpp
    source/dumper.cpp
    source/option.cpp
    source/evman.cpp
    source/fs.cpp
    source/fs_file_dir.cpp
    source/image.cpp
    source/location.cpp
    source/log.cpp
    source/main.cpp
    source/ams_su.c
    source/hats_version.cpp
    source/net.cpp
    source/nro.cpp
    source/ntp.cpp
    source/nxlink.cpp
    source/owo.cpp
    source/owo_romfs.cpp
    source/owo_nca.cpp
    source/forwarder_auto_install.cpp
    source/auto_update.cpp
    source/swkbd.cpp
    source/web_qr.cpp
    source/web_upload.cpp
    source/web_mdns.cpp
    source/web_fs.cpp
    source/web_upload_routes.cpp
    source/web_file_routes.cpp
    source/web_router.cpp
    source/web.cpp
    source/web_http.cpp
    source/web_screenshots.cpp
    source/hasher.cpp
    source/i18n.cpp
    source/ftpsrv_helper.cpp
    source/ftpsrv_vfs.cpp
    source/haze_helper.cpp
    source/haze/haze_internal.cpp
    source/haze/haze_fs_proxy.cpp
    source/haze/haze_install_proxy.cpp
    source/haze/haze_save_proxy.cpp
    source/haze/haze_save_proxy_scan.cpp
    source/haze/haze_game_proxy.cpp
    source/threaded_file_transfer.cpp
    source/threaded_file_transfer_core.cpp
    source/threaded_file_transfer_preflight.cpp
    source/threaded_file_transfer_verify.cpp
    source/threaded_file_transfer_zip_io.cpp
    source/title_info.cpp
    source/title_move.cpp
    source/title_nacp.cpp
    source/title_nsp.cpp
    source/minizip_helper.cpp
    source/fs_zip.cpp
    source/fs_ncm.cpp
    source/utils/utils.cpp
    source/utils/utils_payload.cpp
    source/utils/utils_reboot.cpp
    source/utils/devoptab_common.cpp
    source/utils/devoptab_common_ops.cpp
    source/utils/devoptab_buffered.cpp
    source/utils/devoptab_curl_thread.cpp
    source/utils/devoptab_curl_device.cpp
    source/utils/devoptab_curl_file.cpp
    source/utils/devoptab_curl_dir.cpp
    source/utils/devoptab_romfs.cpp
    source/utils/devoptab_nca.cpp
    source/utils/devoptab_smb2.cpp
    source/utils/devoptab_nfs.cpp
    source/utils/devoptab_mtp.cpp
    source/utils/devoptab_mtp_usb.cpp
    source/utils/devoptab_mtp_proto.cpp
    source/utils/devoptab_mtp_ops.cpp

    source/usb/base.cpp
    source/usb/usbds.cpp
    source/usb/usbhs.cpp
    source/usb/usb_uploader.cpp

    source/yati/yati.cpp
    source/yati/yati_analysis.cpp
    source/yati/yati_pipeline.cpp
    source/yati/yati_metadata.cpp
    source/yati/container/nsp.cpp
    source/yati/container/xci.cpp
    source/yati/source/file.cpp
    source/yati/source/usb.cpp
    source/yati/source/usb_dbi.cpp
    source/yati/source/stream.cpp

    source/yati/nx/es.cpp
    source/yati/nx/keys.cpp
    source/yati/nx/nca.cpp
    source/yati/nx/ncz.cpp
    source/yati/nx/ncm.cpp
    source/yati/nx/ns.cpp
    source/yati/nx/nxdumptool_rsa.c
)

target_compile_definitions(sphaira PRIVATE
    -DAPP_VERSION="${sphaira_VERSION}"
    -DAPP_VERSION_HASH="${sphaira_VERSION_HASH}"
    -DCURL_NO_OLDIES=1
    -DENABLE_NETWORK_INSTALL=$<BOOL:${ENABLE_NETWORK_INSTALL}>
    -DBUILD_SMB2
)

target_compile_options(sphaira PRIVATE
    -Wall
    -Wextra

    # unsure if it's a good idea to enable these by default as
    # it may cause breakage upon compiler updates.
    # -Werror
    # -Wfatal-errors

    # disabled as nx uses s64 for size and offset, however stl uses size_t instead, thus
    # there being a lot of warnings.
    -Wno-sign-compare
    # disabled as many overriden methods don't use the params.
    -Wno-unused-parameter
    # pedantic warning, missing fields are set to 0.
    -Wno-missing-field-initializers
    # disabled as it warns for strcat 2 paths together, but it will never
    # overflow due to fs enforcing a max path len anyway.
    -Wno-format-truncation

    # the below are taken from my gba emulator, they've served me well ;)
    -Wformat-overflow=2
    -Wundef
    -Wmissing-include-dirs
    -fstrict-aliasing
    -Wstrict-overflow=2
    -Walloca
    -Wduplicated-cond
    -Wwrite-strings
    -Wdate-time
    -Wlogical-op
    -Wpacked
    -Wcast-qual
    -Wcast-align
    -Wimplicit-fallthrough=5
    -Wsuggest-final-types
    -Wuninitialized
    -fdiagnostics-all-candidates
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sphaira_dependencies.cmake)

find_package(ZLIB REQUIRED)
find_library(minizip_lib minizip REQUIRED)
find_path(minizip_inc minizip REQUIRED)

find_library(physfs_lib physfs REQUIRED)

find_package(CURL REQUIRED)
find_path(mbedtls_inc mbedtls REQUIRED)
find_library(mbedcrypto_lib mbedcrypto REQUIRED)

if (NOT USE_NEW_ZSTD)
    find_path(zstd_inc zstd.h REQUIRED)
    find_library(zstd_lib zstd REQUIRED)
endif()

set_target_properties(sphaira PROPERTIES
    C_STANDARD 23
    C_EXTENSIONS ON
    CXX_STANDARD 26
    CXX_EXTENSIONS ON
)

target_link_libraries(sphaira PRIVATE
    ftpsrv_helper
    libhaze
    minIni
    nanovg
    stb
    yyjson
    # libusbhsfs
    libnxtc
    smb2
    nfs

    ${physfs_lib}
    ${minizip_lib}
    ZLIB::ZLIB
    CURL::libcurl
    ${mbedcrypto_lib}
)

if (USE_NEW_ZSTD)
    message(STATUS "USING UPSTREAM ZSTD")
    target_link_libraries(sphaira PRIVATE libzstd_static)
else()
    message(STATUS "USING LOCAL ZSTD")
    target_link_libraries(sphaira PRIVATE ${zstd_lib})
    target_include_directories(sphaira PRIVATE ${zstd_inc})
endif()

if (USE_NVJPG)
    target_link_libraries(sphaira PRIVATE nvjpg)
    target_compile_definitions(sphaira PRIVATE USE_NVJPG)
endif()

target_include_directories(sphaira PRIVATE
    include
    source
    ${minizip_inc}
    ${mbedtls_inc}
    ${libsmb2_SOURCE_DIR}/include
    ${libnfs_SOURCE_DIR}/include
    ${libnfs_SOURCE_DIR}/include/nfsc
    ${libnfs_SOURCE_DIR}/nfs
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sphaira_packaging.cmake)
