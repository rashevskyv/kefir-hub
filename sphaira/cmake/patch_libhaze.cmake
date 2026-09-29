# Patches the fetched libhaze library. Run as a FetchContent PATCH_COMMAND
# (working directory = libhaze source root):
#   ${CMAKE_COMMAND} -P .../patch_libhaze.cmake
#
# Each patch is idempotent: skipped if its complete required shape is already present.
#
# 1. include/haze.h: add total to CallbackDataProgress
# 2. include/haze/ptp_responder.hpp: add total parameter to WriteCallbackProgress
# 3. source/ptp_responder.cpp: implement WriteCallbackProgress with total
# 4. source/ptp_responder_ptp_operations.cpp: pass genuine total in GetObject and SendObject
# 5. source/ptp_responder_ptp_operations.cpp: fix storage_id in SendObjectInfo
# 5b. source/ptp_responder_ptp_operations.cpp: expose Kefir Hub responder version in GetDeviceInfo
# 6. source/ptp_responder_mtp_operations.cpp: MTP property handling and fixes
# 7. include/haze/ptp_data_parser.hpp: UTF-16 to UTF-8 decoding
# 8. include/haze/ptp_data_builder.hpp: UTF-8 to UTF-16 encoding
# 9. source/threaded_file_transfer.cpp: resize read buffer before EOF break
# 10. include/haze/results.hpp: ResultCancelled definition
# 11. include/haze.h: aborted flag in CallbackDataFile and CancelTransfer declaration
# 12. include/haze/console_main_loop.hpp: transfer cancellation consumer and CancelTransfer
# 13. source/haze.cpp: CancelTransfer implementation
# 14. include/haze/ptp_responder.hpp: m_reactor and WriteCallbackFile aborted parameter
# 15. source/ptp_responder.cpp: HandleRequest ResultCancelled handling and WriteCallbackFile forwarding
# 16. source/usb_session.cpp: UsbError_UrbCancelled to ResultCancelled translation
# 17. source/ptp_responder_ptp_operations.cpp: incomplete file deletion and aborted callback

include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_base.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_ptp.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_mtp.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_data.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_cancel.cmake")
