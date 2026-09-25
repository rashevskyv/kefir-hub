# libhaze patch base: sections 1, 2, 3
# 1. include/haze.h: add total to CallbackDataProgress
# 2. include/haze/ptp_responder.hpp: add total parameter to WriteCallbackProgress
# 3. source/ptp_responder.cpp: implement WriteCallbackProgress with total

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
