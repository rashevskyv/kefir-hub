# libhaze patch recover: sections 21 to 25 (local abort re-enumerates MTP in device mode; host cancel
# drops the partial file; SendObjectPropList is logged; the class Cancel Request is honoured)

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

# --- 23. source/ptp_responder_ptp_operations.cpp : upgrade trees patched before the early-EOT check ---
# (fresh trees get it from ops_so_body_new_tail in patch_libhaze_cleanup.cmake)
haze_patch_once("source/ptp_responder_ptp_operations.cpp"
    "[LIBHAZE] host cancelled transfer"
    "            R_THROW(haze::ResultCancelled());\n        }\n\n        transfer_success = true;"
    "            R_THROW(haze::ResultCancelled());\n        }\n\n        /* sphaira: the host ended the data phase early (PC-side cancel); drop the partial file. */\n        if (has_known_size && offset < file_size) {\n            log_write(\"[LIBHAZE] host cancelled transfer: %s\\n\", obj->GetName());\n            R_RETURN(this->WriteResponse(PtpResponseCode_IncompleteTransfer));\n        }\n\n        transfer_success = true;"
    "ptp_responder_ptp_operations.cpp early-EOT abort")

# --- 24. source/ptp_responder_mtp_operations.cpp : log every SendObjectPropList (plan H3) ---
# A folder drop that "does nothing" left no trace; this names the object the host asked for.
haze_patch_once("source/ptp_responder_mtp_operations.cpp"
    "void log_write("
    "#include <haze/ptp_responder_types.hpp>\n\nnamespace haze {"
    "#include <haze/ptp_responder_types.hpp>\n\nextern \"C\" {\n    __attribute__((weak)) void log_write(const char* s, ...) {}\n}\n\nnamespace haze {"
    "ptp_responder_mtp_operations.cpp log_write declaration")

haze_patch_once("source/ptp_responder_mtp_operations.cpp"
    "[LIBHAZE] SendObjectPropList"
    "        /* sphaira: the storage root is the parent (no device-root routing). */"
    "        log_write(\"[LIBHAZE] SendObjectPropList storage=%08X parent=%08X format=%04X name=%s\\n\", storage_id, parent_object, format_code, m_buffers->filename_string_buffer);\n\n        /* sphaira: the storage root is the parent (no device-root routing). */"
    "ptp_responder_mtp_operations.cpp SendObjectPropList log")

# --- 25. host-side cancel: the class Cancel Request on the control endpoint (plan H2) ---
# Windows cancels a copy by (1) no longer sending data and (2) a Still Image class Cancel Request
# (0x64) on the control endpoint. libhaze never looked at the control endpoint, so SendObject kept
# waiting until Windows sent its NEXT command ~a minute later and swallowed that as file data.
# The bulk-out read now also waits on the interface setup event; a Cancel Request during an
# object transfer retires the pending read and reports the end of data (no response phase).
haze_patch_once("include/haze/usb_session.hpp"
    "HandleSetupPacket"
    "            Result CancelEndpoint(UsbSessionEndpoint ep, u32 urb_id);"
    "            Result CancelEndpoint(UsbSessionEndpoint ep, u32 urb_id);\n            Event *GetSetupEvent() const;\n            bool HandleSetupPacket();"
    "usb_session.hpp setup packet handling")

haze_patch_once("source/usb_session.cpp"
    "UsbSession::HandleSetupPacket"
    "        R_RETURN(res);\n    }\n\n}"
    "        R_RETURN(res);
    }

    extern \"C\" void log_write(const char* s, ...);

    Event *UsbSession::GetSetupEvent() const {
        return std::addressof(m_interface->SetupEvent);
    }

    /* sphaira: service a class request on the control endpoint. */
    /* Returns true when the host cancels (or resets) the transaction in progress. */
    bool UsbSession::HandleSetupPacket() {
        alignas(0x1000) static u8 ctrl_buffer[0x1000];
        struct { u8 bmRequestType; u8 bRequest; u16 wValue; u16 wIndex; u16 wLength; } setup = {};

        eventClear(std::addressof(m_interface->SetupEvent));
        const Result setup_rc = usbDsInterface_GetSetupPacket(m_interface, std::addressof(setup), sizeof(setup));
        if (R_FAILED(setup_rc)) {
            return false;
        }
        log_write(\"[LIBHAZE] setup packet: type=%02X request=%02X length=%u\\n\", setup.bmRequestType, setup.bRequest, setup.wLength);

        /* One stage of the control transfer: post it, wait (bounded) and harvest the report. */
        const auto stage = [&](bool in, u32 size) {
            u32 urb_id = 0;
            UsbDsReportData report;
            Event * const event = in ? std::addressof(m_interface->CtrlInCompletionEvent) : std::addressof(m_interface->CtrlOutCompletionEvent);
            const u32 post_value = in ? usbDsInterface_CtrlInPostBufferAsync(m_interface, ctrl_buffer, size, std::addressof(urb_id))
                                      : usbDsInterface_CtrlOutPostBufferAsync(m_interface, ctrl_buffer, size, std::addressof(urb_id));
            const Result post_rc = post_value;
            Result wait_rc = post_rc;
            if (R_SUCCEEDED(post_rc)) {
                wait_rc = eventWait(event, 1000000000ULL);
            }
            if (R_FAILED(wait_rc)) {
                log_write(\"[LIBHAZE] control %s stage failed: 0x%08X\\n\", in ? \"in\" : \"out\", wait_rc.GetValue());
                return;
            }
            eventClear(event);
            if (in) {
                usbDsInterface_GetCtrlInReportData(m_interface, std::addressof(report));
            } else {
                usbDsInterface_GetCtrlOutReportData(m_interface, std::addressof(report));
            }
        };

        /* USB Still Image class requests. */
        if (setup.bmRequestType == 0x21 && setup.bRequest == 0x64) {
            /* Cancel Request: cancellation code + transaction id, then the status stage. */
            stage(false, setup.wLength < sizeof(ctrl_buffer) ? setup.wLength : sizeof(ctrl_buffer));
            stage(true, 0);
            return true;
        }
        if (setup.bmRequestType == 0x21 && setup.bRequest == 0x66) {
            /* Device Reset Request: no data stage. */
            stage(true, 0);
            return true;
        }
        if (setup.bmRequestType == 0xA1 && setup.bRequest == 0x67) {
            /* Get Device Status: length 4, status OK (0x2001), then the status stage. */
            ctrl_buffer[0] = 4; ctrl_buffer[1] = 0; ctrl_buffer[2] = 0x01; ctrl_buffer[3] = 0x20;
            stage(true, 4);
            stage(false, 0);
            return false;
        }

        usbDsInterface_StallCtrl(m_interface);
        return false;
    }

}"
    "usb_session.cpp setup packet handling")

haze_patch_once("include/haze/async_usb_server.hpp"
    "ConsumeHostCancel"
    "            bool IsBroken() const { return m_broken.load(std::memory_order_acquire); }"
    "            bool IsBroken() const { return m_broken.load(std::memory_order_acquire); }
            /* sphaira: an object transfer is in its data phase / the host sent the class Cancel Request. */
            mutable std::atomic<bool> m_object_transfer{false};
            mutable std::atomic<bool> m_host_cancelled{false};
            void BeginObjectTransfer() const { SetCleanup(false); SetCancelled(false); m_host_cancelled.store(false); m_object_transfer.store(true); }
            void EndObjectTransfer() const { SetCleanup(false); SetCancelled(false); m_object_transfer.store(false); }
            bool ConsumeHostCancel() const { return m_host_cancelled.exchange(false); }"
    "async_usb_server.hpp host cancel state")

haze_patch_once("source/async_usb_server.cpp"
    "bool host_cancel = false;"
    "        Result wait_rc = ResultSuccess();"
    "        Result wait_rc = ResultSuccess();\n        bool host_cancel = false;"
    "async_usb_server.cpp host cancel flag")

haze_patch_once("source/async_usb_server.cpp"
    "g_usb_session.GetSetupEvent()"
    "                wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 1000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));"
    "                wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 1000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)), waiterForEvent(g_usb_session.GetSetupEvent()));
                /* sphaira: a class request on the control endpoint; a Cancel Request ends an object transfer. */
                if (R_SUCCEEDED(wait_rc) && waiter_idx == 1) {
                    if (g_usb_session.HandleSetupPacket() && m_object_transfer.load(std::memory_order_acquire)) {
                        host_cancel = true;
                        break;
                    }
                    continue;
                }"
    "async_usb_server.cpp setup event wait")

haze_patch_once("source/async_usb_server.cpp"
    "if (host_cancel) {"
    "        /* sphaira: reap in-flight URB on cancel and immediately abort */"
    "        /* sphaira: the host cancelled the transaction: retire the pending read and report the end of data. */
        if (host_cancel) {
            g_usb_session.CancelEndpoint(ep, urb_id);
            m_host_cancelled.store(true, std::memory_order_release);
            *out_size_transferred = 0;
            R_SUCCEED();
        }

        /* sphaira: reap in-flight URB on cancel and immediately abort */"
    "async_usb_server.cpp host cancel end of data")

# SendObject side (fresh trees get both from ops_so_body_new_tail in patch_libhaze_cleanup.cmake).
haze_patch_once("source/ptp_responder_ptp_operations.cpp"
    "m_usb_server.BeginObjectTransfer();"
    "        m_usb_server.SetCleanup(false);\n        m_usb_server.SetCancelled(false);\n        ON_SCOPE_EXIT {\n            m_usb_server.SetCleanup(false);\n            m_usb_server.SetCancelled(false);\n        };"
    "        m_usb_server.BeginObjectTransfer();\n        ON_SCOPE_EXIT { m_usb_server.EndObjectTransfer(); };"
    "ptp_responder_ptp_operations.cpp object transfer scope")

haze_patch_once("source/ptp_responder_ptp_operations.cpp"
    "m_usb_server.ConsumeHostCancel()"
    "        /* sphaira: the host ended the data phase early (PC-side cancel); drop the partial file. */\n        if (has_known_size && offset < file_size) {\n            log_write(\"[LIBHAZE] host cancelled transfer: %s\\n\", obj->GetName());\n            R_RETURN(this->WriteResponse(PtpResponseCode_IncompleteTransfer));\n        }"
    "        /* sphaira: PC-side cancel: drop the partial file. A class Cancel Request has no response phase. */\n        if (has_known_size && offset < file_size) {\n            log_write(\"[LIBHAZE] host cancelled transfer: %s\\n\", obj->GetName());\n            R_SUCCEED_IF(m_usb_server.ConsumeHostCancel());\n            R_RETURN(this->WriteResponse(PtpResponseCode_IncompleteTransfer));\n        }"
    "ptp_responder_ptp_operations.cpp host cancel response")
