# libhaze patch cleanup: section 17 (SendObject & GetObject clean drain, no UAF, >4GB & ZLP)
# 17. source/ptp_responder_ptp_operations.cpp: clean drain, no UAF, *bytes_read=0, >4GB & ZLP

# --- 17. source/ptp_responder_ptp_operations.cpp : clean drain, no UAF, >4GB & ZLP ---
if(EXISTS "source/ptp_responder_ptp_operations.cpp")
    file(READ "source/ptp_responder_ptp_operations.cpp" src)

    set(dup_reactor "        if (m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {\n            m_reactor->SetResult(ResultSuccess());\n        }\n\n        if (m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {\n            m_reactor->SetResult(ResultSuccess());\n        }")
    set(single_reactor "        if (m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {\n            m_reactor->SetResult(ResultSuccess());\n        }")
    set(had_dup FALSE)
    string(FIND "${src}" "${dup_reactor}" find_dup)
    while(NOT find_dup EQUAL -1)
        set(had_dup TRUE)
        string(REPLACE "${dup_reactor}" "${single_reactor}" src "${src}")
        string(FIND "${src}" "${dup_reactor}" find_dup)
    endwhile()
    if(had_dup)
        file(WRITE "source/ptp_responder_ptp_operations.cpp" "${src}")
    endif()

    string(FIND "${src}" "*bytes_read = 0;\n                if (is_done)" find_bytes_read_top)
    string(FIND "${src}" "m_usb_server.SetCleanup(true);" find_set_cleanup)
    string(FIND "${src}" "write_res == haze::ResultCancelled()" find_write_cancel)
    string(FIND "${src}" "m_usb_server.IsCancelled()" find_usb_cancel)
    string(FIND "${src}" "m_usb_server.SetCancelled(true);" find_writer_set_cancelled)
    if(NOT find_bytes_read_top EQUAL -1 AND NOT find_set_cleanup EQUAL -1 AND NOT find_write_cancel EQUAL -1 AND NOT find_usb_cancel EQUAL -1 AND NOT find_writer_set_cancelled EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_ptp_operations.cpp cancel already patched")
    else()
        string(FIND "${src}" "void log_write(" find_lw_ops)
        if(find_lw_ops EQUAL -1)
            set(ops_incl_old "#include <cstdio>")
            set(ops_incl_new "#include <cstdio>\n#include <atomic>\n\nextern \"C\" {\n    __attribute__((weak)) void log_write(const char* s, ...) {}\n}")
            string(REPLACE "${ops_incl_old}" "${ops_incl_new}" src "${src}")
        endif()

        string(FIND "${src}" "[LIBHAZE] SendObjectInfo" find_soi_log)
        if(find_soi_log EQUAL -1)
            set(ops_soi_old "Result PtpResponder::SendObjectInfo(PtpDataParser &rdp) {\n        /* Prop list is reset on SendObjectInfo. */")
            set(ops_soi_new "Result PtpResponder::SendObjectInfo(PtpDataParser &rdp) {\n        log_write(\"[LIBHAZE] SendObjectInfo\\n\");\n        /* Prop list is reset on SendObjectInfo. */")
            string(REPLACE "${ops_soi_old}" "${ops_soi_new}" src "${src}")
        endif()

        string(FIND "${src}" "[LIBHAZE] SendObject\\n" find_so_log)
        if(find_so_log EQUAL -1)
            set(ops_so_old "Result PtpResponder::SendObject(PtpDataParser &rdp) {\n        /* Reset SendObject object ID on exit. */")
            set(ops_so_new "Result PtpResponder::SendObject(PtpDataParser &rdp) {\n        log_write(\"[LIBHAZE] SendObject\\n\");\n        /* Reset SendObject object ID on exit. */")
            string(REPLACE "${ops_so_old}" "${ops_so_new}" src "${src}")
        endif()

        string(FIND "${src}" "WriteCallbackFile(CallbackType_ReadEnd, obj->GetName(), !transfer_success);" find_read_end_aborted)
        if(find_read_end_aborted EQUAL -1)
            set(ops_read_cancel_old "        WriteCallbackFile(CallbackType_ReadBegin, obj->GetName());\n        ON_SCOPE_EXIT { WriteCallbackFile(CallbackType_ReadEnd, obj->GetName()); };")
            set(ops_read_cancel_new "        if (m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {\n            m_reactor->SetResult(ResultSuccess());\n        }\n\n        bool transfer_success = false;\n        WriteCallbackFile(CallbackType_ReadBegin, obj->GetName());\n        ON_SCOPE_EXIT { WriteCallbackFile(CallbackType_ReadEnd, obj->GetName(), !transfer_success); };")
            string(REPLACE "${ops_read_cancel_old}" "${ops_read_cancel_new}" src "${src}")

            set(ops_read_ok_old "            }, mode\n        ));\n\n        /* Flush the data response. */\n        R_TRY(db.Commit());\n\n        /* Write the success response. */\n        R_RETURN(this->WriteResponse(PtpResponseCode_Ok));\n    }")
            set(ops_read_ok_new "            }, mode\n        ));\n\n        /* Flush the data response. */\n        R_TRY(db.Commit());\n\n        transfer_success = true;\n\n        /* Write the success response. */\n        R_RETURN(this->WriteResponse(PtpResponseCode_Ok));\n    }")
            string(REPLACE "${ops_read_ok_old}" "${ops_read_ok_new}" src "${src}")
        endif()

        # SendObject shared preamble (clean state, file open, size calculation)
        set(ops_so_body_head
"        if (m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {
            m_reactor->SetResult(ResultSuccess());
        }

        /* Lock the object as a file. */
        FsFile file;
        R_TRY(Fs(obj).OpenFile(obj->GetName(), FsOpenMode_Write | FsOpenMode_Append, std::addressof(file)));

        bool transfer_success = false;

        /* Ensure we maintain a clean state on exit. */
        ON_SCOPE_EXIT {
            Fs(obj).CloseFile(std::addressof(file));
            if (!transfer_success) {
                Fs(obj).DeleteFile(obj->GetName());
                m_object_database.DeleteObject(obj);
            }
        };

        bool has_known_size = false;
        u64 file_size = 0;
        /* sphaira: pass genuine total to progress callback (write transfer). */
        s64 total_size = 0;
        u64 offset = 0;

        if (m_send_prop_list) {
            file_size = m_send_prop_list->size;
            if (m_send_prop_list->size > 0 && m_send_prop_list->size <= (u64)INT64_MAX) {
                total_size = (s64)m_send_prop_list->size;
            }
            has_known_size = true;
            R_TRY(Fs(obj).SetFileSize(std::addressof(file), file_size));
        } else if (data_header.length != 0xFFFFFFFFU) {
            if (data_header.length >= sizeof(PtpUsbBulkContainer)) {
                /* Got the real file size. */
                file_size = data_header.length - sizeof(PtpUsbBulkContainer);
                total_size = (s64)file_size;
                has_known_size = true;
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), file_size));
            } else {
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), 0));
            }
        } else {
            /* 0xFFFFFFFF sentinel: >=4GB or unknown length, terminated by short packet / ZLP (EOT). */
            file_size = static_cast<u64>(INT64_MAX);
            total_size = 0;
            has_known_size = false;
            R_TRY(Fs(obj).SetFileSize(std::addressof(file), 0));
        }

        /* Truncate the file to the received size. */
        ON_SCOPE_EXIT{
            if (transfer_success && offset != file_size) {
                Fs(obj).SetFileSize(std::addressof(file), offset);
            }
        };

        WriteCallbackFile(CallbackType_WriteBegin, obj->GetName());
        ON_SCOPE_EXIT { WriteCallbackFile(CallbackType_WriteEnd, obj->GetName(), !transfer_success); };

        auto mode = sphaira::thread::Mode::MultiThreaded;
        if (!Fs(obj).MultiThreadTransfer(0, false)) {
            mode = sphaira::thread::Mode::SingleThreaded;
        }")

        # Latest verified SendObject transfer loop tail
        set(ops_so_body_new_tail
"        bool is_done = false;
        std::atomic<bool> is_cancelled{false};

        m_usb_server.SetCleanup(false);
        m_usb_server.SetCancelled(false);
        ON_SCOPE_EXIT {
            m_usb_server.SetCleanup(false);
            m_usb_server.SetCancelled(false);
        };

        /* sphaira: clean transport drain without mid-transfer deletion */
        const Result transfer_res = sphaira::thread::Transfer(file_size,
            [this, &dp, &is_done, &is_cancelled, obj](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                *bytes_read = 0;
                if (is_done) {
                    R_SUCCEED();
                }

                if (m_reactor && m_reactor->GetResult() == haze::ResultStopRequested()) {
                    R_THROW(haze::ResultStopRequested());
                }

                auto check_cancellation = [&]() {
                    if (!is_cancelled.load(std::memory_order_acquire)) {
                        const bool reactor_cancelled = (m_reactor && m_reactor->GetResult() == haze::ResultCancelled());
                        const bool usb_cancelled = m_usb_server.IsCancelled();
                        if (reactor_cancelled || usb_cancelled) {
                            is_cancelled.store(true, std::memory_order_release);
                            m_usb_server.SetCleanup(true);
                            log_write(\"[LIBHAZE] starting transport cleanup for cancelled transfer: %s\\n\", obj->GetName());
                        }
                    } else if (!m_usb_server.IsInCleanup()) {
                        m_usb_server.SetCleanup(true);
                    }
                };

                check_cancellation();

                /* Read as many bytes as we can. */
                u32 bytes_received = 0;
                Result read_res = dp.ReadBuffer((u8*)data, size, std::addressof(bytes_received));

                check_cancellation();

                if (read_res == haze::ResultCancelled()) {
                    /* Host URB abort (0x748C) - host aborted transfer from PC side */
                    is_cancelled.store(true, std::memory_order_release);
                    is_done = true;
                    *bytes_read = 0;
                    R_SUCCEED();
                }

                *bytes_read = bytes_received;

                /* If we received fewer bytes than the batch size or reached EOT, we're done. */
                if (haze::ResultEndOfTransmission::Includes(read_res)) {
                    is_done = true;
                    R_SUCCEED();
                }

                if (R_FAILED(read_res)) {
                    if (read_res == haze::ResultStopRequested()) {
                        log_write(\"[LIBHAZE] transport stop during transfer\\n\");
                    } else {
                        log_write(\"[LIBHAZE] transport error/disconnect during transfer: 0x%08X\\n\", read_res.GetValue());
                    }
                    R_RETURN(read_res);
                }

                R_SUCCEED();
            },
            [this, &file, &obj, &offset, total_size, &is_cancelled](const void* data, s64 off, s64 size) -> Result {
                if (is_cancelled.load(std::memory_order_acquire)) {
                    offset += size;
                    R_SUCCEED();
                }

                /* Write to the file. */
                const Result write_res = Fs(obj).WriteFile(std::addressof(file), off, data, size, 0);
                if (write_res == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    m_usb_server.SetCancelled(true);
                    m_usb_server.SetCleanup(true);
                    offset += size;
                    R_SUCCEED();
                }
                R_TRY(write_res);

                WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);
                offset += size;
                R_SUCCEED();
            }, mode
        );

        if (R_FAILED(transfer_res)) {
            if (transfer_res == haze::ResultStopRequested()) {
                log_write(\"[LIBHAZE] transfer aborted by stop request\\n\");
            } else {
                log_write(\"[LIBHAZE] transfer aborted by transport error: 0x%08X\\n\", transfer_res.GetValue());
            }
            R_RETURN(transfer_res);
        }

        /* Finalize terminating ZLP if data length was exact multiple of 512 and not yet at EOT. */
        if (has_known_size && ((sizeof(PtpUsbBulkContainer) + file_size) % 512 == 0) && !dp.HasEot()) {
            R_TRY(dp.Finalize());
        }

        if (is_cancelled.load(std::memory_order_acquire)) {
            log_write(\"[LIBHAZE] transport cleanup finished for cancelled transfer: %s\\n\", obj->GetName());
            R_THROW(haze::ResultCancelled());
        }

        transfer_success = true;

        /* Write the success response. */
        R_RETURN(this->WriteResponse(PtpResponseCode_Ok));")

        # Full latest body
        set(ops_so_body_new "${ops_so_body_head}\n\n${ops_so_body_new_tail}")

        # Intermediate shape 3 tail (from 3rd review: has SetCleanup, lacks SetCancelled / check_cancellation / write cancel)
        set(ops_so_body_prev3
"        bool is_done = false;
        std::atomic<bool> is_cancelled{false};

        m_usb_server.SetCleanup(false);
        ON_SCOPE_EXIT { m_usb_server.SetCleanup(false); };

        /* sphaira: clean transport drain without mid-transfer deletion */
        const Result transfer_res = sphaira::thread::Transfer(file_size,
            [this, &dp, &is_done, &is_cancelled, obj](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                *bytes_read = 0;
                if (is_done) {
                    R_SUCCEED();
                }

                if (m_reactor && m_reactor->GetResult() == haze::ResultStopRequested()) {
                    R_THROW(haze::ResultStopRequested());
                }

                if (!is_cancelled.load(std::memory_order_relaxed) && m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    m_usb_server.SetCleanup(true);
                    log_write(\"[LIBHAZE] starting transport cleanup for cancelled transfer: %s\\n\", obj->GetName());
                }

                /* Read as many bytes as we can. */
                u32 bytes_received = 0;
                Result read_res = dp.ReadBuffer((u8*)data, size, std::addressof(bytes_received));

                if (read_res == haze::ResultCancelled()) {
                    /* Host URB abort (0x748C) - host aborted transfer from PC side */
                    is_cancelled.store(true, std::memory_order_release);
                    is_done = true;
                    *bytes_read = 0;
                    R_SUCCEED();
                }

                *bytes_read = bytes_received;

                /* If we received fewer bytes than the batch size or reached EOT, we're done. */
                if (haze::ResultEndOfTransmission::Includes(read_res)) {
                    is_done = true;
                    R_SUCCEED();
                }

                if (R_FAILED(read_res)) {
                    if (read_res == haze::ResultStopRequested()) {
                        log_write(\"[LIBHAZE] transport stop during transfer\\n\");
                    } else {
                        log_write(\"[LIBHAZE] transport error/disconnect during transfer: 0x%08X\\n\", read_res.GetValue());
                    }
                    R_RETURN(read_res);
                }

                R_SUCCEED();
            },
            [this, &file, &obj, &offset, total_size, &is_cancelled](const void* data, s64 off, s64 size) -> Result {
                if (is_cancelled.load(std::memory_order_acquire)) {
                    offset += size;
                    R_SUCCEED();
                }

                /* Write to the file. */
                R_TRY(Fs(obj).WriteFile(std::addressof(file), off, data, size, 0));

                WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);
                offset += size;
                R_SUCCEED();
            }, mode
        );

        if (R_FAILED(transfer_res)) {
            if (transfer_res == haze::ResultStopRequested()) {
                log_write(\"[LIBHAZE] transfer aborted by stop request\\n\");
            } else {
                log_write(\"[LIBHAZE] transfer aborted by transport error: 0x%08X\\n\", transfer_res.GetValue());
            }
            R_RETURN(transfer_res);
        }

        /* Finalize terminating ZLP if data length was exact multiple of 512 and not yet at EOT. */
        if (has_known_size && ((sizeof(PtpUsbBulkContainer) + file_size) % 512 == 0) && !dp.HasEot()) {
            R_TRY(dp.Finalize());
        }

        if (is_cancelled.load(std::memory_order_acquire)) {
            log_write(\"[LIBHAZE] transport cleanup finished for cancelled transfer: %s\\n\", obj->GetName());
            R_THROW(haze::ResultCancelled());
        }

        transfer_success = true;

        /* Write the success response. */
        R_RETURN(this->WriteResponse(PtpResponseCode_Ok));")

        # Intermediate shape 2 tail (from 2nd review: lacks SetCleanup, lacks *bytes_read=0)
        set(ops_so_body_prev2
"        bool is_done = false;
        std::atomic<bool> is_cancelled{false};

        /* sphaira: clean transport drain without mid-transfer deletion */
        const Result transfer_res = sphaira::thread::Transfer(file_size,
            [this, &dp, &is_done, &is_cancelled, obj](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                if (is_done) {
                    *bytes_read = 0;
                    R_SUCCEED();
                }

                if (m_reactor && m_reactor->GetResult() == haze::ResultStopRequested()) {
                    R_THROW(haze::ResultStopRequested());
                }

                if (!is_cancelled.load(std::memory_order_relaxed) && m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    log_write(\"[LIBHAZE] starting transport cleanup for cancelled transfer: %s\\n\", obj->GetName());
                }

                /* Read as many bytes as we can. */
                u32 bytes_received = 0;
                Result read_res = dp.ReadBuffer((u8*)data, size, std::addressof(bytes_received));

                if (read_res == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    is_done = true;
                    *bytes_read = 0;
                    R_SUCCEED();
                }

                *bytes_read = bytes_received;

                /* If we received fewer bytes than the batch size, we're done. */
                if (haze::ResultEndOfTransmission::Includes(read_res)) {
                    is_done = true;
                    R_SUCCEED();
                }

                if (R_FAILED(read_res)) {
                    if (read_res == haze::ResultStopRequested()) {
                        log_write(\"[LIBHAZE] transport stop during transfer\\n\");
                    } else {
                        log_write(\"[LIBHAZE] transport error/disconnect during transfer: 0x%08X\\n\", read_res.GetValue());
                    }
                    R_RETURN(read_res);
                }

                R_SUCCEED();
            },
            [this, &file, &obj, &offset, total_size, &is_cancelled](const void* data, s64 off, s64 size) -> Result {
                if (is_cancelled.load(std::memory_order_acquire)) {
                    offset += size;
                    R_SUCCEED();
                }

                /* Write to the file. */
                Result write_res = Fs(obj).WriteFile(std::addressof(file), off, data, size, 0);
                if (write_res == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    offset += size;
                    R_SUCCEED();
                }
                R_TRY(write_res);

                WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);
                offset += size;
                R_SUCCEED();
            }, mode
        );

        if (R_FAILED(transfer_res)) {
            if (transfer_res == haze::ResultStopRequested()) {
                log_write(\"[LIBHAZE] transfer aborted by stop request\\n\");
            } else {
                log_write(\"[LIBHAZE] transfer aborted by transport error: 0x%08X\\n\", transfer_res.GetValue());
            }
            R_RETURN(transfer_res);
        }

        /* Finalize terminating ZLP if data length was exact multiple of 512 and not yet at EOT. */
        if (has_known_size && ((sizeof(PtpUsbBulkContainer) + file_size) % 512 == 0) && !dp.HasEot()) {
            R_TRY(dp.Finalize());
        }

        if (is_cancelled.load(std::memory_order_acquire)) {
            log_write(\"[LIBHAZE] transport cleanup finished for cancelled transfer: %s\\n\", obj->GetName());
            R_THROW(haze::ResultCancelled());
        }

        transfer_success = true;

        /* Write the success response. */
        R_RETURN(this->WriteResponse(PtpResponseCode_Ok));")

        # Clean shape from upstream
        set(ops_so_body_clean
"        /* Lock the object as a file. */
        FsFile file;
        R_TRY(Fs(obj).OpenFile(obj->GetName(), FsOpenMode_Write | FsOpenMode_Append, std::addressof(file)));

        /* Ensure we maintain a clean state on exit. */
        ON_SCOPE_EXIT { Fs(obj).CloseFile(std::addressof(file)); };

        /* Dummy file size for the threaded transfer. */
        auto file_size = 4_GB;
        /* sphaira: pass genuine total to progress callback (write transfer). */
        s64 total_size = 0;
        u64 offset = 0;

        if (m_send_prop_list) {
            file_size = m_send_prop_list->size;
            if (m_send_prop_list->size > 0 && m_send_prop_list->size <= (u64)INT64_MAX) {
                total_size = (s64)m_send_prop_list->size;
            }
        } else {
            if (data_header.length >= sizeof(PtpUsbBulkContainer)) {
                /* Got the real file size. */
                file_size = data_header.length - sizeof(PtpUsbBulkContainer);
                if (data_header.length != 0xFFFFFFFFU) {
                    total_size = (s64)file_size;
                }
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), file_size));
            } else {
                /* Truncate the file after locking for write. */
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), 0));
            }
        }

        /* Truncate the file to the received size. */
        ON_SCOPE_EXIT{
            if (offset != file_size) {
                Fs(obj).SetFileSize(std::addressof(file), offset);
            }
        };

        WriteCallbackFile(CallbackType_WriteBegin, obj->GetName());
        ON_SCOPE_EXIT { WriteCallbackFile(CallbackType_WriteEnd, obj->GetName()); };

        auto mode = sphaira::thread::Mode::MultiThreaded;
        if (!Fs(obj).MultiThreadTransfer(0, false)) {
            mode = sphaira::thread::Mode::SingleThreaded;
        }

        bool is_done = false;

        R_TRY(sphaira::thread::Transfer(file_size,
            [this, &dp, &is_done](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                if (is_done) {
                    *bytes_read = 0;
                    R_SUCCEED();
                }

                /* Read as many bytes as we can. */
                u32 bytes_received;
                const Result read_res = dp.ReadBuffer((u8*)data, size, std::addressof(bytes_received));
                *bytes_read = bytes_received;

                /* If we received fewer bytes than the batch size, we're done. */
                if (haze::ResultEndOfTransmission::Includes(read_res)) {
                    is_done = true;
                    R_SUCCEED();
                }

                R_RETURN(read_res);
            },
            [this, &file, &obj, &offset, total_size](const void* data, s64 off, s64 size) -> Result {
                /* Write to the file. */
                R_TRY(Fs(obj).WriteFile(std::addressof(file), off, data, size, 0));
                WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);
                offset += size;
                R_SUCCEED();
            }, mode
        ));

        /* Write the success response. */
        R_RETURN(this->WriteResponse(PtpResponseCode_Ok));")

        set(ops_so_body_prev4
"                /* Write to the file. */
                const Result write_res = Fs(obj).WriteFile(std::addressof(file), off, data, size, 0);
                if (write_res == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    offset += size;
                    R_SUCCEED();
                }
                R_TRY(write_res);")

        set(ops_so_body_wfunc_new
"                /* Write to the file. */
                const Result write_res = Fs(obj).WriteFile(std::addressof(file), off, data, size, 0);
                if (write_res == haze::ResultCancelled()) {
                    is_cancelled.store(true, std::memory_order_release);
                    m_usb_server.SetCancelled(true);
                    m_usb_server.SetCleanup(true);
                    offset += size;
                    R_SUCCEED();
                }
                R_TRY(write_res);")

        string(REPLACE "${ops_so_body_prev4}" "${ops_so_body_wfunc_new}" src "${src}")
        string(REPLACE "${ops_so_body_prev3}" "${ops_so_body_new_tail}" src "${src}")
        string(REPLACE "${ops_so_body_prev2}" "${ops_so_body_new_tail}" src "${src}")
        string(REPLACE "${ops_so_body_clean}" "${ops_so_body_new}" src "${src}")

        string(FIND "${src}" "!transfer_success" find_transfer_success_after)
        string(FIND "${src}" "transfer_success && offset != file_size" find_trunc_check_after)
        string(FIND "${src}" "WriteCallbackFile(CallbackType_WriteEnd, obj->GetName(), !transfer_success);" find_write_end_aborted_after)
        string(FIND "${src}" "WriteCallbackFile(CallbackType_ReadEnd, obj->GetName(), !transfer_success);" find_read_end_aborted_after)
        string(FIND "${src}" "*bytes_read = 0;\n                if (is_done)" find_bytes_read_after)
        string(FIND "${src}" "m_usb_server.SetCleanup(true);" find_cleanup_after)
        string(FIND "${src}" "write_res == haze::ResultCancelled()" find_write_cancel_after)
        string(FIND "${src}" "m_usb_server.IsCancelled()" find_usb_cancel_after)
        string(FIND "${src}" "m_usb_server.SetCancelled(true);" find_writer_set_cancelled_after)
        if(find_transfer_success_after EQUAL -1 OR find_trunc_check_after EQUAL -1 OR find_write_end_aborted_after EQUAL -1 OR find_read_end_aborted_after EQUAL -1 OR find_bytes_read_after EQUAL -1 OR find_cleanup_after EQUAL -1 OR find_write_cancel_after EQUAL -1 OR find_usb_cancel_after EQUAL -1 OR find_writer_set_cancelled_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] source does not match supported shapes for ptp_responder_ptp_operations.cpp")
        endif()
        file(WRITE "source/ptp_responder_ptp_operations.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_ptp_operations.cpp cancel patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] source/ptp_responder_ptp_operations.cpp not found")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_usb.cmake")
