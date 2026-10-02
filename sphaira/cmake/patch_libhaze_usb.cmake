# libhaze patch usb & parser: sections 18 to 20
# 18. include/haze/ptp_data_parser.hpp: parser EOT only on success, exact Read<T>, partial ReadBuffer
# 19. include/haze/event_reactor.hpp & source/event_reactor.cpp: WaitForTimeout with consumer dispatch & normalized timeout
# 20. include/haze/async_usb_server.hpp & source/async_usb_server.cpp: URB lifecycle, mutable atomic state, and transport termination on broken endpoint

# --- 18. include/haze/ptp_data_parser.hpp : parser EOT only on success, partial read & HasEot ---
if(EXISTS "include/haze/ptp_data_parser.hpp")
    file(READ "include/haze/ptp_data_parser.hpp" src)
    string(FIND "${src}" "bool HasEot() const" find_has_eot)
    string(FIND "${src}" "haze::ResultInvalidArgument()" find_read_t_check)
    if(NOT find_has_eot EQUAL -1 AND NOT find_read_t_check EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_data_parser.hpp EOT semantics already patched")
    else()
        set(parser_flush_old "                m_received_size = 0;\n                m_offset = 0;\n\n                ON_SCOPE_EXIT {\n                    /* End of transmission occurs when receiving a bulk transfer less than the buffer size. */\n                    /* PTP uses zero-length termination, so zero is a possible size to receive. */\n                    m_eot = m_received_size < haze::UsbBulkPacketBufferSize;\n                };\n\n                R_RETURN(m_server->ReadPacket(m_data, haze::UsbBulkPacketBufferSize, std::addressof(m_received_size)));\n            }\n        public:\n            constexpr explicit PtpDataParser(void *data, AsyncUsbServer *server) : m_server(server), m_received_size(), m_offset(), m_data(static_cast<u8 *>(data)), m_eot() { /* ... */ }\n\n            Result Finalize() {")
        set(parser_flush_old2 "                ON_SCOPE_EXIT {\n                    /* End of transmission occurs when receiving a bulk transfer less than the buffer size. */\n                    /* PTP uses zero-length termination, so zero is a possible size to receive. */\n                    m_eot = m_received_size < haze::UsbBulkPacketBufferSize;\n                };\n\n                R_RETURN(m_server->ReadPacket(m_data, haze::UsbBulkPacketBufferSize, std::addressof(m_received_size)));\n            }\n        public:\n            constexpr explicit PtpDataParser(void *data, AsyncUsbServer *server) : m_server(server), m_received_size(), m_offset(), m_data(static_cast<u8 *>(data)), m_eot() { /* ... */ }\n\n            Result Finalize() {")

        set(parser_flush_new
"                u32 received = 0;
                const Result rc = m_server->ReadPacket(m_data, haze::UsbBulkPacketBufferSize, std::addressof(received));
                if (R_SUCCEEDED(rc)) {
                    m_received_size = received;
                    m_offset = 0;
                    m_eot = (m_received_size < haze::UsbBulkPacketBufferSize);
                }
                R_RETURN(rc);
            }
        public:
            constexpr explicit PtpDataParser(void *data, AsyncUsbServer *server) : m_server(server), m_received_size(), m_offset(), m_data(static_cast<u8 *>(data)), m_eot() { /* ... */ }

            bool HasEot() const {
                return m_eot;
            }

            Result Finalize() {")

        set(parser_readbuf_old
"                    /* If we cannot read more bytes now, flush. */
                    if (m_offset == m_received_size) {
                        R_TRY(this->Flush());
                    }")

        set(parser_readbuf_prev
"                    /* If we cannot read more bytes now, flush. */
                    if (m_offset == m_received_size) {
                        const Result rc = this->Flush();
                        if (R_FAILED(rc)) {
                            if (*out_read_count > 0) {
                                R_SUCCEED();
                            }
                            R_RETURN(rc);
                        }
                    }")

        set(parser_readbuf_new
"                    /* If we cannot read more bytes now, flush. */
                    if (m_offset == m_received_size) {
                        const Result rc = this->Flush();
                        if (R_FAILED(rc)) {
                            if (*out_read_count > 0 && haze::ResultEndOfTransmission::Includes(rc)) {
                                R_SUCCEED();
                            }
                            R_RETURN(rc);
                        }
                    }")

        set(parser_read_t_clean
"            template <typename T>
            Result Read(T *out_t) {
                u32 read_count;
                u8 bytes[sizeof(T)];

                R_TRY(this->ReadBuffer(bytes, sizeof(T), std::addressof(read_count)));

                std::memcpy(out_t, bytes, sizeof(T));

                R_SUCCEED();
            }")

        set(parser_read_t_prev
"            template <typename T>
            Result Read(T *out_t) {
                u32 read_count = 0;
                u8 bytes[sizeof(T)] = {};

                R_TRY(this->ReadBuffer(bytes, sizeof(T), std::addressof(read_count)));
                R_UNLESS(read_count == sizeof(T), haze::ResultInvalidParameter());

                std::memcpy(out_t, bytes, sizeof(T));

                R_SUCCEED();
            }")

        set(parser_read_t_prev2
"            template <typename T>
            Result Read(T *val) {
                u32 read_count;
                R_TRY(this->ReadBuffer(reinterpret_cast<u8 *>(val), sizeof(T), std::addressof(read_count)));
                R_UNLESS(read_count == sizeof(T), haze::ResultInvalidParameter());
                R_SUCCEED();
            }")

        set(parser_read_t_new
"            template <typename T>
            Result Read(T *out_t) {
                u32 read_count = 0;
                u8 bytes[sizeof(T)] = {};

                R_TRY(this->ReadBuffer(bytes, sizeof(T), std::addressof(read_count)));
                R_UNLESS(read_count == sizeof(T), haze::ResultInvalidArgument());

                std::memcpy(out_t, bytes, sizeof(T));

                R_SUCCEED();
            }")

        string(REPLACE "${parser_flush_old}" "${parser_flush_new}" src "${src}")
        string(REPLACE "${parser_flush_old2}" "${parser_flush_new}" src "${src}")
        string(REPLACE "${parser_readbuf_prev}" "${parser_readbuf_new}" src "${src}")
        string(REPLACE "${parser_readbuf_old}" "${parser_readbuf_new}" src "${src}")
        string(REPLACE "${parser_read_t_prev}" "${parser_read_t_new}" src "${src}")
        string(REPLACE "${parser_read_t_prev2}" "${parser_read_t_new}" src "${src}")
        string(REPLACE "${parser_read_t_clean}" "${parser_read_t_new}" src "${src}")

        string(FIND "${src}" "bool HasEot() const" find_has_eot_after)
        string(FIND "${src}" "haze::ResultInvalidArgument()" find_read_t_after)
        if(find_has_eot_after EQUAL -1 OR find_read_t_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply EOT/Read patch to ptp_data_parser.hpp")
        endif()
        file(WRITE "include/haze/ptp_data_parser.hpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_data_parser.hpp EOT semantics patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] include/haze/ptp_data_parser.hpp not found")
endif()

# --- 19. include/haze/event_reactor.hpp & source/event_reactor.cpp : WaitForTimeout ---
if(EXISTS "include/haze/event_reactor.hpp" AND EXISTS "source/event_reactor.cpp")
    file(READ "include/haze/event_reactor.hpp" h_src)
    string(FIND "${h_src}" "WaitForTimeout" find_wft_h)
    if(find_wft_h EQUAL -1)
        set(reactor_h_old
"        public:
            template <typename... Args> requires (sizeof...(Args) > 0)
            Result WaitFor(s32 *out_arg_waiter, Args &&... arg_waiters) {
                const Waiter arg_waiter_array[] = { arg_waiters... };
                return this->WaitForImpl(out_arg_waiter, arg_waiter_array, sizeof...(Args));
            }
        private:
            Result WaitForImpl(s32 *out_arg_waiter, const Waiter *arg_waiters, s32 num_arg_waiters);")

        set(reactor_h_new
"        public:
            template <typename... Args> requires (sizeof...(Args) > 0)
            Result WaitFor(s32 *out_arg_waiter, Args &&... arg_waiters) {
                const Waiter arg_waiter_array[] = { arg_waiters... };
                return this->WaitForImpl(out_arg_waiter, arg_waiter_array, sizeof...(Args));
            }

            template <typename... Args> requires (sizeof...(Args) > 0)
            Result WaitForTimeout(s32 *out_arg_waiter, u64 timeout_ns, Args &&... arg_waiters) {
                const Waiter arg_waiter_array[] = { arg_waiters... };
                return this->WaitForTimeoutImpl(out_arg_waiter, timeout_ns, arg_waiter_array, sizeof...(Args));
            }
        private:
            Result WaitForImpl(s32 *out_arg_waiter, const Waiter *arg_waiters, s32 num_arg_waiters);
            Result WaitForTimeoutImpl(s32 *out_arg_waiter, u64 timeout_ns, const Waiter *arg_waiters, s32 num_arg_waiters);")

        string(REPLACE "${reactor_h_old}" "${reactor_h_new}" h_src "${h_src}")
        string(FIND "${h_src}" "WaitForTimeout" find_wft_h_after)
        if(find_wft_h_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply WaitForTimeout to event_reactor.hpp")
        endif()
        file(WRITE "include/haze/event_reactor.hpp" "${h_src}")
        message(STATUS "[libhaze-patch] applied event_reactor.hpp WaitForTimeout patch")
    else()
        message(STATUS "[libhaze-patch] event_reactor.hpp WaitForTimeout already patched")
    endif()

    file(READ "source/event_reactor.cpp" cpp_src)
    string(FIND "${cpp_src}" "WaitForTimeoutImpl" find_wft_cpp)
    string(FIND "${cpp_src}" "haze::ResultTimeout()" find_timeout_norm)
    if(find_wft_cpp EQUAL -1 OR find_timeout_norm EQUAL -1)
        set(wft_old_ret "            s32 idx = -1;\n            const Result wait_rc = waitObjects(std::addressof(idx), m_waiters, m_num_wait_objects + num_arg_waiters, timeout_ns);\n            if (R_FAILED(wait_rc)) {\n                R_RETURN(wait_rc);\n            }")
        set(wft_norm_ret "            s32 idx = -1;\n            const Result wait_rc = waitObjects(std::addressof(idx), m_waiters, m_num_wait_objects + num_arg_waiters, timeout_ns);\n            if (R_FAILED(wait_rc)) {\n                if (svc::ResultTimedOut::Includes(wait_rc) || wait_rc.GetValue() == 0xEA01) {\n                    R_RETURN(haze::ResultTimeout());\n                }\n                R_RETURN(wait_rc);\n            }")

        if(NOT find_wft_cpp EQUAL -1)
            string(REPLACE "${wft_old_ret}" "${wft_norm_ret}" cpp_src "${cpp_src}")
        else()
            set(reactor_cpp_add
"\n    Result EventReactor::WaitForTimeoutImpl(s32 *out_arg_waiter, u64 timeout_ns, const Waiter *arg_waiters, s32 num_arg_waiters) {\n        HAZE_ASSERT(0 < num_arg_waiters && num_arg_waiters <= svc::ArgumentHandleCountMax);\n        HAZE_ASSERT(m_num_wait_objects + num_arg_waiters <= svc::ArgumentHandleCountMax);\n\n        while (true) {\n            R_TRY(m_result);\n\n            for (s32 i = 0; i < num_arg_waiters; i++) {\n                m_waiters[i + m_num_wait_objects] = arg_waiters[i];\n            }\n\n            s32 idx = -1;\n            const Result wait_rc = waitObjects(std::addressof(idx), m_waiters, m_num_wait_objects + num_arg_waiters, timeout_ns);\n            if (R_FAILED(wait_rc)) {\n                if (svc::ResultTimedOut::Includes(wait_rc) || wait_rc.GetValue() == 0xEA01) {\n                    R_RETURN(haze::ResultTimeout());\n                }\n                R_RETURN(wait_rc);\n            }\n\n            if (idx >= m_num_wait_objects) {\n                *out_arg_waiter = idx - m_num_wait_objects;\n                R_SUCCEED();\n            }\n\n            m_consumers[idx]->ProcessEvent();\n        }\n    }\n")
            string(REPLACE "\n}\n" "${reactor_cpp_add}\n}\n" cpp_src "${cpp_src}")
        endif()
        string(FIND "${cpp_src}" "WaitForTimeoutImpl" find_wft_cpp_after)
        string(FIND "${cpp_src}" "haze::ResultTimeout()" find_timeout_norm_after)
        if(find_wft_cpp_after EQUAL -1 OR find_timeout_norm_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply normalized WaitForTimeoutImpl to event_reactor.cpp")
        endif()
        file(WRITE "source/event_reactor.cpp" "${cpp_src}")
        message(STATUS "[libhaze-patch] applied event_reactor.cpp WaitForTimeoutImpl patch")
    else()
        message(STATUS "[libhaze-patch] event_reactor.cpp WaitForTimeoutImpl already patched")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] event_reactor.hpp or event_reactor.cpp not found")
endif()

# --- 20. include/haze/async_usb_server.hpp & source/async_usb_server.cpp : URB lifecycle & cleanup ---
if(EXISTS "include/haze/async_usb_server.hpp" AND EXISTS "source/async_usb_server.cpp")
    file(READ "include/haze/async_usb_server.hpp" h_src)
    string(FIND "${h_src}" "mutable std::atomic<bool> m_cancelled" find_mutable_cancelled)
    string(FIND "${h_src}" "mutable std::atomic<bool> m_broken" find_mutable_broken)
    set(async_h_dup "            mutable std::atomic<bool> m_in_cleanup{false};\n            mutable std::atomic<bool> m_cancelled{false};\n            mutable std::atomic<bool> m_broken{false};\n            mutable std::atomic<bool> m_in_cleanup{false};\n            mutable std::atomic<bool> m_cancelled{false};\n            mutable std::atomic<bool> m_broken{false};")
    string(FIND "${h_src}" "${async_h_dup}" find_dup_h)
    if(find_mutable_cancelled EQUAL -1 OR find_mutable_broken EQUAL -1 OR NOT find_dup_h EQUAL -1)
        string(FIND "${h_src}" "#include <atomic>" find_atomic_inc)
        if(find_atomic_inc EQUAL -1)
            set(async_h_inc_old "#include <haze/common.hpp>")
            set(async_h_inc_new "#include <atomic>\n#include <haze/common.hpp>")
            string(REPLACE "${async_h_inc_old}" "${async_h_inc_new}" h_src "${h_src}")
        endif()

        set(async_h_prev3 "        private:\n            EventReactor *m_reactor;\n            std::atomic<bool> m_in_cleanup{false};\n            std::atomic<bool> m_cancelled{false};\n            std::atomic<bool> m_broken{false};")
        set(async_h_prev2 "        private:\n            EventReactor *m_reactor;\n            std::atomic<bool> m_in_cleanup{false};")
        set(async_h_clean "        private:\n            EventReactor *m_reactor;")

        set(async_h_fields "        private:\n            EventReactor *m_reactor;\n            mutable std::atomic<bool> m_in_cleanup{false};\n            mutable std::atomic<bool> m_cancelled{false};\n            mutable std::atomic<bool> m_broken{false};")
        set(async_h_methods "\n            void SetCleanup(bool cleanup) const { m_in_cleanup.store(cleanup, std::memory_order_release); }\n            bool IsInCleanup() const { return m_in_cleanup.load(std::memory_order_acquire); }\n            void SetCancelled(bool cancelled) const { m_cancelled.store(cancelled, std::memory_order_release); }\n            bool IsCancelled() const { return m_cancelled.load(std::memory_order_acquire); }\n            void SetBroken(bool broken) const { m_broken.store(broken, std::memory_order_release); }\n            bool IsBroken() const { return m_broken.load(std::memory_order_acquire); }")

        set(async_h_dup "            mutable std::atomic<bool> m_in_cleanup{false};\n            mutable std::atomic<bool> m_cancelled{false};\n            mutable std::atomic<bool> m_broken{false};\n            mutable std::atomic<bool> m_in_cleanup{false};\n            mutable std::atomic<bool> m_cancelled{false};\n            mutable std::atomic<bool> m_broken{false};")
        string(FIND "${h_src}" "${async_h_dup}" find_dup_h)
        string(FIND "${h_src}" "${async_h_prev3}" find_p3_h)
        string(FIND "${h_src}" "${async_h_prev2}" find_p2_h)
        string(FIND "${h_src}" "${async_h_clean}" find_clean_h)

        if(NOT find_dup_h EQUAL -1)
            string(REPLACE "${async_h_dup}" "            mutable std::atomic<bool> m_in_cleanup{false};\n            mutable std::atomic<bool> m_cancelled{false};\n            mutable std::atomic<bool> m_broken{false};" h_src "${h_src}")
        elseif(NOT find_p3_h EQUAL -1)
            string(REPLACE "${async_h_prev3}" "${async_h_fields}" h_src "${h_src}")
        elseif(NOT find_p2_h EQUAL -1)
            string(REPLACE "${async_h_prev2}" "${async_h_fields}" h_src "${h_src}")
        elseif(NOT find_clean_h EQUAL -1)
            string(REPLACE "${async_h_clean}" "${async_h_fields}" h_src "${h_src}")
        endif()

        string(FIND "${h_src}" "bool IsBroken() const" find_is_broken)
        if(find_is_broken EQUAL -1)
            set(async_h_ctor_old "constexpr explicit AsyncUsbServer() : m_reactor() { /* ... */ }")
            set(async_h_ctor_new "constexpr explicit AsyncUsbServer() : m_reactor(), m_in_cleanup(false), m_cancelled(false), m_broken(false) { /* ... */ }${async_h_methods}")
            string(REPLACE "${async_h_ctor_old}" "${async_h_ctor_new}" h_src "${h_src}")
        endif()

        string(FIND "${h_src}" "mutable std::atomic<bool> m_cancelled" find_mutable_cancelled_after)
        string(FIND "${h_src}" "mutable std::atomic<bool> m_broken" find_mutable_broken_after)
        if(find_mutable_cancelled_after EQUAL -1 OR find_mutable_broken_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply mutable atomic SetCleanup to async_usb_server.hpp")
        endif()
        file(WRITE "include/haze/async_usb_server.hpp" "${h_src}")
        message(STATUS "[libhaze-patch] applied async_usb_server.hpp SetCleanup patch")
    else()
        message(STATUS "[libhaze-patch] async_usb_server.hpp SetCleanup already patched")
    endif()

    file(READ "source/async_usb_server.cpp" cpp_src)
    string(FIND "${cpp_src}" "ResultTransferFailed" find_tf_check)
    string(FIND "${cpp_src}" "const Result cancel_rc = g_usb_session.CancelEndpoint" find_ce_check)
    string(FIND "${cpp_src}" "s32 waiter_idx = -1;" find_dup_waiter)
    string(FIND "${cpp_src}" "m_broken.store(true" find_broken_check)
    string(FIND "${cpp_src}" "m_cancelled.store(true" find_cancelled_check)
    string(FIND "${cpp_src}" "haze::ResultTimeout()" find_timeout_check)
    set(retirement_guard "const Result cancel_rc = g_usb_session.CancelEndpoint(ep, urb_id);\n            m_broken.store(true, std::memory_order_release);")
    string(FIND "${cpp_src}" "${retirement_guard}" find_retirement_guard)
    string(FIND "${cpp_src}" "m_broken.load(std::memory_order_acquire) || m_cancelled.load(std::memory_order_acquire)" find_entry_cancel)
    if(NOT find_tf_check EQUAL -1 AND NOT find_ce_check EQUAL -1 AND find_dup_waiter EQUAL -1 AND NOT find_broken_check EQUAL -1 AND NOT find_cancelled_check EQUAL -1 AND NOT find_timeout_check EQUAL -1 AND NOT find_retirement_guard EQUAL -1 AND NOT find_entry_cancel EQUAL -1)
        message(STATUS "[libhaze-patch] async_usb_server.cpp cancel already patched")
    else()
        string(FIND "${cpp_src}" "void log_write(" find_lw_async)
        if(find_lw_async EQUAL -1)
            set(async_incl_old "#include <haze.hpp>")
            set(async_incl_new "#include <haze.hpp>\n\nextern \"C\" {\n    __attribute__((weak)) void log_write(const char* s, ...) {}\n}")
            string(REPLACE "${async_incl_old}" "${async_incl_new}" cpp_src "${cpp_src}")
        endif()

        # Clean shape
        set(async_transfer_clean
"        /* Select the appropriate endpoint and begin a transfer. */
        UsbSessionEndpoint ep = read ? UsbSessionEndpoint_Read : UsbSessionEndpoint_Write;
        R_TRY(g_usb_session.TransferAsync(ep, page, size, std::addressof(urb_id)));

        /* Try to wait for the event. */
        R_TRY(m_reactor->WaitFor(std::addressof(waiter_idx), waiterForEvent(g_usb_session.GetCompletionEvent(ep))));

        /* Return what we transferred. */
        R_RETURN(g_usb_session.GetTransferResult(ep, urb_id, out_size_transferred));")

        # Previous intermediate shape 3 (from 09c04c20: bound cleanup wait)
        set(async_transfer_prev3
"        if (m_broken.load(std::memory_order_acquire)) {
            R_THROW(haze::ResultTransferFailed());
        }

        /* Select the appropriate endpoint and begin a transfer. */
        UsbSessionEndpoint ep = read ? UsbSessionEndpoint_Read : UsbSessionEndpoint_Write;
        R_TRY(g_usb_session.TransferAsync(ep, page, size, std::addressof(urb_id)));

        /* Try to wait for the event. */
        waiter_idx = -1;
        Result wait_rc = ResultSuccess();
        if (m_in_cleanup.load(std::memory_order_acquire) && read) {
            wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 5000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
        } else if (read) {
            while (true) {
                wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 1000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
                if (wait_rc != haze::ResultTimeout() && !svc::ResultTimedOut::Includes(wait_rc) && wait_rc.GetValue() != 0xEA01) {
                    break;
                }
                if (m_reactor) {
                    if (m_reactor->GetResult() == haze::ResultStopRequested()) {
                        wait_rc = haze::ResultStopRequested();
                        break;
                    }
                    if (m_reactor->GetResult() == haze::ResultCancelled()) {
                        wait_rc = haze::ResultCancelled();
                        break;
                    }
                }
                if (m_in_cleanup.load(std::memory_order_acquire) || m_cancelled.load(std::memory_order_acquire)) {
                    wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 5000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
                    break;
                }
            }
        } else {
            wait_rc = m_reactor->WaitFor(std::addressof(waiter_idx), waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
        }

        /* sphaira: reap in-flight URB on cancel and bound cleanup wait */
        if (wait_rc == haze::ResultCancelled() && read) {
            m_cancelled.store(true, std::memory_order_release);
            m_in_cleanup.store(true, std::memory_order_release);
            if (m_reactor && m_reactor->GetResult() == haze::ResultCancelled()) {
                m_reactor->SetResult(ResultSuccess());
            }
            wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 5000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
            if (R_SUCCEEDED(wait_rc)) {
                wait_rc = g_usb_session.GetTransferResult(ep, urb_id, out_size_transferred);
                if (R_SUCCEEDED(wait_rc)) {
                    R_SUCCEED();
                }
            } else {
                const Result cancel_rc = g_usb_session.CancelEndpoint(ep, urb_id);
                m_broken.store(true, std::memory_order_release);
                if (R_FAILED(cancel_rc)) {
                    log_write(\"[LIBHAZE] failed to cancel endpoint %d for urb %u: 0x%08X\\n\", ep, urb_id, cancel_rc.GetValue());
                    R_RETURN(cancel_rc);
                }
                if (wait_rc == haze::ResultStopRequested() || (m_reactor && m_reactor->GetResult() == haze::ResultStopRequested())) {
                    R_THROW(haze::ResultStopRequested());
                }
                log_write(\"[LIBHAZE] host silence during cleanup timeout: 0x%08X\\n\", wait_rc.GetValue());
                R_THROW(haze::ResultTransferFailed());
            }
        }

        if (R_FAILED(wait_rc)) {
            const Result cancel_rc = g_usb_session.CancelEndpoint(ep, urb_id);
            m_broken.store(true, std::memory_order_release);
            if (R_FAILED(cancel_rc)) {
                log_write(\"[LIBHAZE] failed to cancel endpoint %d for urb %u: 0x%08X\\n\", ep, urb_id, cancel_rc.GetValue());
                R_RETURN(cancel_rc);
            }
            if (wait_rc == haze::ResultStopRequested() || (m_reactor && m_reactor->GetResult() == haze::ResultStopRequested())) {
                R_THROW(haze::ResultStopRequested());
            }
            if (m_in_cleanup.load(std::memory_order_acquire)) {
                log_write(\"[LIBHAZE] host silence during cleanup timeout: 0x%08X\\n\", wait_rc.GetValue());
                R_THROW(haze::ResultTransferFailed());
            }
            R_RETURN(wait_rc);
        }

        /* Return what we transferred. */
        R_RETURN(g_usb_session.GetTransferResult(ep, urb_id, out_size_transferred));")

        # Latest verified shape
        set(async_transfer_new
"        if (m_broken.load(std::memory_order_acquire) || m_cancelled.load(std::memory_order_acquire)) {
            R_THROW(haze::ResultTransferFailed());
        }

        /* Select the appropriate endpoint and begin a transfer. */
        UsbSessionEndpoint ep = read ? UsbSessionEndpoint_Read : UsbSessionEndpoint_Write;
        R_TRY(g_usb_session.TransferAsync(ep, page, size, std::addressof(urb_id)));

        /* Try to wait for the event. */
        waiter_idx = -1;
        Result wait_rc = ResultSuccess();
        if (m_in_cleanup.load(std::memory_order_acquire) && read) {
            wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 5000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
        } else if (read) {
            while (true) {
                wait_rc = m_reactor->WaitForTimeout(std::addressof(waiter_idx), 1000000000ULL, waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
                if (wait_rc != haze::ResultTimeout() && !svc::ResultTimedOut::Includes(wait_rc) && wait_rc.GetValue() != 0xEA01) {
                    break;
                }
                if (m_reactor) {
                    if (m_reactor->GetResult() == haze::ResultStopRequested()) {
                        wait_rc = haze::ResultStopRequested();
                        break;
                    }
                    if (m_reactor->GetResult() == haze::ResultCancelled()) {
                        wait_rc = haze::ResultCancelled();
                        break;
                    }
                }
                if (m_cancelled.load(std::memory_order_acquire)) {
                    wait_rc = haze::ResultCancelled();
                    break;
                }
            }
        } else {
            wait_rc = m_reactor->WaitFor(std::addressof(waiter_idx), waiterForEvent(g_usb_session.GetCompletionEvent(ep)));
        }

        /* sphaira: reap in-flight URB on cancel and immediately abort */
        if (wait_rc == haze::ResultCancelled() && read) {
            m_cancelled.store(true, std::memory_order_release);
            m_broken.store(true, std::memory_order_release);
            const Result cancel_rc = g_usb_session.CancelEndpoint(ep, urb_id);
            if (R_FAILED(cancel_rc)) {
                log_write(\"[LIBHAZE] failed to cancel endpoint %d for urb %u: 0x%08X\\n\", ep, urb_id, cancel_rc.GetValue());
                R_RETURN(cancel_rc);
            }
            R_THROW(haze::ResultTransferFailed());
        }

        if (R_FAILED(wait_rc)) {
            const Result cancel_rc = g_usb_session.CancelEndpoint(ep, urb_id);
            m_broken.store(true, std::memory_order_release);
            if (R_FAILED(cancel_rc)) {
                log_write(\"[LIBHAZE] failed to cancel endpoint %d for urb %u: 0x%08X\\n\", ep, urb_id, cancel_rc.GetValue());
                R_RETURN(cancel_rc);
            }
            if (wait_rc == haze::ResultStopRequested() || (m_reactor && m_reactor->GetResult() == haze::ResultStopRequested())) {
                R_THROW(haze::ResultStopRequested());
            }
            if (m_in_cleanup.load(std::memory_order_acquire)) {
                log_write(\"[LIBHAZE] host silence during cleanup timeout: 0x%08X\\n\", wait_rc.GetValue());
                R_THROW(haze::ResultTransferFailed());
            }
            R_RETURN(wait_rc);
        }

        /* Return what we transferred. */
        R_RETURN(g_usb_session.GetTransferResult(ep, urb_id, out_size_transferred));")

        string(REPLACE "${async_transfer_prev3}" "${async_transfer_new}" cpp_src "${cpp_src}")
        string(REPLACE "${async_transfer_clean}" "${async_transfer_new}" cpp_src "${cpp_src}")

        string(FIND "${cpp_src}" "ResultTransferFailed" find_tf_after)
        string(FIND "${cpp_src}" "const Result cancel_rc = g_usb_session.CancelEndpoint" find_ce_after)
        string(FIND "${cpp_src}" "s32 waiter_idx = -1;" find_dup_after)
        string(FIND "${cpp_src}" "m_broken.store(true" find_broken_after)
        string(FIND "${cpp_src}" "m_cancelled.store(true" find_cancelled_after)
        string(FIND "${cpp_src}" "haze::ResultTimeout()" find_timeout_after)
        string(FIND "${cpp_src}" "${retirement_guard}" find_retirement_after)
        string(FIND "${cpp_src}" "m_broken.load(std::memory_order_acquire) || m_cancelled.load(std::memory_order_acquire)" find_entry_after)
        if(find_tf_after EQUAL -1 OR find_ce_after EQUAL -1 OR NOT find_dup_after EQUAL -1 OR find_broken_after EQUAL -1 OR find_cancelled_after EQUAL -1 OR find_timeout_after EQUAL -1 OR find_retirement_after EQUAL -1 OR find_entry_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply cancel patch to async_usb_server.cpp")
        endif()
        file(WRITE "source/async_usb_server.cpp" "${cpp_src}")
        message(STATUS "[libhaze-patch] applied async_usb_server.cpp cancel patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] async_usb_server.hpp or async_usb_server.cpp not found")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/patch_libhaze_recover.cmake")
