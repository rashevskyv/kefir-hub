# libhaze patch recover: sections 21 to 22 (a local abort re-enumerates MTP in device mode)

# Replace `old` with `new` in `file` unless `marker` (a string only the new shape has) is present.
function(haze_patch_once file marker old new label)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "[libhaze-patch] ${file} not found")
    endif()
    file(READ "${file}" src)
    string(FIND "${src}" "${marker}" find_marker)
    if(NOT find_marker EQUAL -1)
        message(STATUS "[libhaze-patch] ${label} already patched")
        return()
    endif()
    string(FIND "${src}" "${old}" find_old)
    if(find_old EQUAL -1)
        message(FATAL_ERROR "[libhaze-patch] source does not match supported shape for ${label}")
    endif()
    string(REPLACE "${old}" "${new}" src "${src}")
    file(WRITE "${file}" "${src}")
    message(STATUS "[libhaze-patch] applied ${label}")
endfunction()

# --- 21. include/haze/console_main_loop.hpp : recover every broken transport ---
# SendObject resets the cancel flag in its scope exit, so IsCancelled() was always false here:
# the loop left the thread with usb:ds closed and the app handed the port to host mode.
haze_patch_once("include/haze/console_main_loop.hpp"
    "const bool local_cancel = ptp_responder.IsBroken();"
    "const bool local_cancel = ptp_responder.IsCancelled();"
    "/* sphaira: the transport only breaks on a local abort (cancel, failed install write). */\n                    const bool local_cancel = ptp_responder.IsBroken();"
    "console_main_loop.hpp broken-transport recovery")

# Exit() signals the stop event right after CancelTransfer(); the reactor has not seen it yet.
haze_patch_once("include/haze/console_main_loop.hpp"
    "waitSingle(waiterForUEvent(&m_cancel_event), 0)"
    "                    /* 5. Check stop request before loop re-enters Initialize. */\n                    if (m_event_reactor.GetResult() == haze::ResultStopRequested()) {"
    "                    /* 5. Check stop request before loop re-enters Initialize. */\n                    if (m_event_reactor.GetResult() == haze::ResultStopRequested() || R_SUCCEEDED(waitSingle(waiterForUEvent(&m_cancel_event), 0))) {"
    "console_main_loop.hpp stop check during recovery")

# --- 22. source/usb_session.cpp : 0x828C is the status of the URB we cancelled ourselves ---
haze_patch_once("source/usb_session.cpp"
    "res.GetValue() == 0x828C"
    "if (res == haze::ResultCancelled() || R_SUCCEEDED(res)) {"
    "/* sphaira: 0x828C reports the URB cancelled by usbDsEndpoint_Cancel above, which is what we asked for. */\n        if (res == haze::ResultCancelled() || R_SUCCEEDED(res) || res.GetValue() == 0x828C) {"
    "usb_session.cpp cancelled-urb status")
