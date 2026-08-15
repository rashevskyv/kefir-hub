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

# --- 1. include/haze.h : CallbackDataProgress --------------------------------
if(EXISTS "include/haze.h")
    file(READ "include/haze.h" src)
    string(FIND "${src}" "long long total;" find_total)
    string(FIND "${src}" "sphaira: report total size in progress callback" find_marker)
    if(NOT find_total EQUAL -1 AND NOT find_marker EQUAL -1)
        message(STATUS "[libhaze-patch] haze.h already patched")
    else()
        set(haze_h_old
"typedef struct {
    long long offset;
    long long size;
} CallbackDataProgress;")
        set(haze_h_new
"typedef struct {
    long long offset;
    long long size;
    /* sphaira: report total size in progress callback (0 if unknown). */
    long long total;
} CallbackDataProgress;")
        string(REPLACE "${haze_h_old}" "${haze_h_new}" src "${src}")
        string(FIND "${src}" "long long total;" find_total_after)
        string(FIND "${src}" "sphaira: report total size in progress callback" find_marker_after)
        if(find_total_after EQUAL -1 OR find_marker_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply total field patch to haze.h (unexpected shape or partial patch)")
        endif()
        file(WRITE "include/haze.h" "${src}")
        message(STATUS "[libhaze-patch] applied haze.h total field patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] include/haze.h not found")
endif()

# --- 2. include/haze/ptp_responder.hpp : WriteCallbackProgress decl ----------
if(EXISTS "include/haze/ptp_responder.hpp")
    file(READ "include/haze/ptp_responder.hpp" src)
    string(FIND "${src}" "void WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total);" find_sig)
    string(FIND "${src}" "sphaira: progress callback with explicit total size" find_marker)
    if(NOT find_sig EQUAL -1 AND NOT find_marker EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder.hpp already patched")
    else()
        set(resp_h_old
"            void WriteCallbackRename(CallbackType type, const char* name, const char* newname);
            void WriteCallbackProgress(CallbackType type, s64 offset, s64 size);")
        set(resp_h_new
"            void WriteCallbackRename(CallbackType type, const char* name, const char* newname);
            /* sphaira: progress callback with explicit total size. */
            void WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total);")
        string(REPLACE "${resp_h_old}" "${resp_h_new}" src "${src}")
        string(FIND "${src}" "void WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total);" find_sig_after)
        string(FIND "${src}" "sphaira: progress callback with explicit total size" find_marker_after)
        if(find_sig_after EQUAL -1 OR find_marker_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply WriteCallbackProgress decl patch to ptp_responder.hpp (unexpected shape or partial patch)")
        endif()
        file(WRITE "include/haze/ptp_responder.hpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder.hpp WriteCallbackProgress decl patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] include/haze/ptp_responder.hpp not found")
endif()

# --- 3. source/ptp_responder.cpp : WriteCallbackProgress impl ----------------
if(EXISTS "source/ptp_responder.cpp")
    file(READ "source/ptp_responder.cpp" src)
    string(FIND "${src}" "void PtpResponder::WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total) {" find_sig)
    string(FIND "${src}" "data.progress.total = total;" find_field)
    string(FIND "${src}" "sphaira: progress callback with explicit total size" find_marker)
    if(NOT find_sig EQUAL -1 AND NOT find_field EQUAL -1 AND NOT find_marker EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder.cpp already patched")
    else()
        set(resp_cpp_old
"    #if 0
    void PtpResponder::WriteCallbackSession(CallbackType type) {}
    void PtpResponder::WriteCallbackFile(CallbackType type, const char* name) {}
    void PtpResponder::WriteCallbackRename(CallbackType type, const char* name, const char* newname) {}
    void PtpResponder::WriteCallbackProgress(CallbackType type, s64 offset, s64 size) {}
    #else")
        set(resp_cpp_new
"    #if 0
    void PtpResponder::WriteCallbackSession(CallbackType type) {}
    void PtpResponder::WriteCallbackFile(CallbackType type, const char* name) {}
    void PtpResponder::WriteCallbackRename(CallbackType type, const char* name, const char* newname) {}
    void PtpResponder::WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total) {}
    #else")
        set(resp_cpp_body_old
"    void PtpResponder::WriteCallbackProgress(CallbackType type, s64 offset, s64 size) {
        if (!m_callback) {
            return;
        }
        CallbackData data{type};
        data.progress.offset = offset;
        data.progress.size = size;
        m_callback(&data);
    }")
        set(resp_cpp_body_new
"    void PtpResponder::WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total) {
        if (!m_callback) {
            return;
        }
        /* sphaira: progress callback with explicit total size. */
        CallbackData data{type};
        data.progress.offset = offset;
        data.progress.size = size;
        data.progress.total = total;
        m_callback(&data);
    }")
        string(REPLACE "${resp_cpp_old}" "${resp_cpp_new}" src "${src}")
        string(REPLACE "${resp_cpp_body_old}" "${resp_cpp_body_new}" src "${src}")
        string(FIND "${src}" "void PtpResponder::WriteCallbackProgress(CallbackType type, s64 offset, s64 size, s64 total) {" find_sig_after)
        string(FIND "${src}" "data.progress.total = total;" find_field_after)
        string(FIND "${src}" "sphaira: progress callback with explicit total size" find_marker_after)
        if(find_sig_after EQUAL -1 OR find_field_after EQUAL -1 OR find_marker_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply WriteCallbackProgress impl patch to ptp_responder.cpp (unexpected shape or partial patch)")
        endif()
        file(WRITE "source/ptp_responder.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder.cpp WriteCallbackProgress impl patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] source/ptp_responder.cpp not found")
endif()

# --- 4. source/ptp_responder_ptp_operations.cpp : pass genuine total ---------
if(EXISTS "source/ptp_responder_ptp_operations.cpp")
    file(READ "source/ptp_responder_ptp_operations.cpp" src)
    string(FIND "${src}" "WriteCallbackProgress(CallbackType_ReadProgress, off, size, file_size);" find_read_cb)
    string(FIND "${src}" "WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);" find_write_cb)
    string(FIND "${src}" "sphaira: pass genuine total to progress callback (read transfer)" find_read_marker)
    string(FIND "${src}" "sphaira: pass genuine total to progress callback (write transfer)" find_write_marker)
    string(FIND "${src}" "data_header.length != 0xFFFFFFFFU" find_sentinel_check)
    string(FIND "${src}" "m_send_prop_list->size <= (u64)INT64_MAX" find_safe_cast)

    if(NOT find_read_cb EQUAL -1 AND NOT find_write_cb EQUAL -1 AND NOT find_read_marker EQUAL -1 AND NOT find_write_marker EQUAL -1 AND NOT find_sentinel_check EQUAL -1 AND NOT find_safe_cast EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_ptp_operations.cpp already patched")
    else()
        # Read operation replacement
        set(ops_read_old
"            [this, &db](const void* data, s64 off, s64 size) -> Result {
                /* Write to output. */
                R_TRY(db.AddBuffer((const u8*)data, size));
                WriteCallbackProgress(CallbackType_ReadProgress, off, size);
                R_SUCCEED();
            }, mode")
        set(ops_read_new
"            /* sphaira: pass genuine total to progress callback (read transfer). */
            [this, &db, file_size](const void* data, s64 off, s64 size) -> Result {
                /* Write to output. */
                R_TRY(db.AddBuffer((const u8*)data, size));
                WriteCallbackProgress(CallbackType_ReadProgress, off, size, file_size);
                R_SUCCEED();
            }, mode")

        # Write operation size calculation replacement (upstream version)
        set(ops_write_old
"        /* Dummy file size for the threaded transfer. */
        auto file_size = 4_GB;
        u64 offset = 0;

        if (m_send_prop_list) {
            file_size = m_send_prop_list->size;
        } else {
            if (data_header.length > sizeof(PtpUsbBulkContainer)) {
                /* Got the real file size. */
                file_size = data_header.length - sizeof(PtpUsbBulkContainer);
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), file_size));
            } else {
                /* Truncate the file after locking for write. */
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), 0));
            }
        }")

        # Write operation size calculation replacement (previous intermediate version, if any)
        set(ops_write_prev
"        /* Dummy file size for the threaded transfer. */
        auto file_size = 4_GB;
        /* sphaira: pass genuine total to progress callback (0 if sentinel). */
        s64 total_size = 0;
        u64 offset = 0;

        if (m_send_prop_list) {
            file_size = m_send_prop_list->size;
            total_size = (s64)m_send_prop_list->size;
        } else {
            if (data_header.length > sizeof(PtpUsbBulkContainer)) {
                /* Got the real file size. */
                file_size = data_header.length - sizeof(PtpUsbBulkContainer);
                total_size = (s64)file_size;
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), file_size));
            } else {
                /* Truncate the file after locking for write. */
                R_TRY(Fs(obj).SetFileSize(std::addressof(file), 0));
            }
        }")

        set(ops_write_new
"        /* Dummy file size for the threaded transfer. */
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
            if (data_header.length > sizeof(PtpUsbBulkContainer)) {
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
        }")

        # Write callback lambda replacement
        set(ops_write_cb_old
"            [this, &file, &obj, &offset](const void* data, s64 off, s64 size) -> Result {
                /* Write to the file. */
                R_TRY(Fs(obj).WriteFile(std::addressof(file), off, data, size, 0));
                WriteCallbackProgress(CallbackType_WriteProgress, off, size);
                offset += size;
                R_SUCCEED();
            }, mode")
        set(ops_write_cb_new
"            [this, &file, &obj, &offset, total_size](const void* data, s64 off, s64 size) -> Result {
                /* Write to the file. */
                R_TRY(Fs(obj).WriteFile(std::addressof(file), off, data, size, 0));
                WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);
                offset += size;
                R_SUCCEED();
            }, mode")

        string(REPLACE "${ops_read_old}" "${ops_read_new}" src "${src}")
        string(REPLACE "${ops_write_prev}" "${ops_write_new}" src "${src}")
        string(REPLACE "${ops_write_old}" "${ops_write_new}" src "${src}")
        string(REPLACE "${ops_write_cb_old}" "${ops_write_cb_new}" src "${src}")

        string(FIND "${src}" "WriteCallbackProgress(CallbackType_ReadProgress, off, size, file_size);" find_read_cb_after)
        string(FIND "${src}" "WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);" find_write_cb_after)
        string(FIND "${src}" "sphaira: pass genuine total to progress callback (read transfer)" find_read_marker_after)
        string(FIND "${src}" "sphaira: pass genuine total to progress callback (write transfer)" find_write_marker_after)
        string(FIND "${src}" "data_header.length != 0xFFFFFFFFU" find_sentinel_check_after)
        string(FIND "${src}" "m_send_prop_list->size <= (u64)INT64_MAX" find_safe_cast_after)

        if(find_read_cb_after EQUAL -1 OR find_write_cb_after EQUAL -1 OR find_read_marker_after EQUAL -1 OR find_write_marker_after EQUAL -1 OR find_sentinel_check_after EQUAL -1 OR find_safe_cast_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply genuine total patch to ptp_responder_ptp_operations.cpp (unexpected shape or partial patch)")
        endif()
        file(WRITE "source/ptp_responder_ptp_operations.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_ptp_operations.cpp genuine total patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] source/ptp_responder_ptp_operations.cpp not found")
endif()
