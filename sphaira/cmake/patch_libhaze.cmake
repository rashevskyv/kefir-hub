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
    string(FIND "${src}" "data_header.length >= sizeof(PtpUsbBulkContainer)" find_zero_byte)

    if(NOT find_read_cb EQUAL -1 AND NOT find_write_cb EQUAL -1 AND NOT find_read_marker EQUAL -1 AND NOT find_write_marker EQUAL -1 AND NOT find_sentinel_check EQUAL -1 AND NOT find_safe_cast EQUAL -1 AND NOT find_zero_byte EQUAL -1)
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

        set(ops_write_prev2
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
        string(REPLACE "${ops_write_prev2}" "${ops_write_new}" src "${src}")
        string(REPLACE "${ops_write_prev}" "${ops_write_new}" src "${src}")
        string(REPLACE "${ops_write_old}" "${ops_write_new}" src "${src}")
        string(REPLACE "${ops_write_cb_old}" "${ops_write_cb_new}" src "${src}")

        string(FIND "${src}" "WriteCallbackProgress(CallbackType_ReadProgress, off, size, file_size);" find_read_cb_after)
        string(FIND "${src}" "WriteCallbackProgress(CallbackType_WriteProgress, off, size, total_size);" find_write_cb_after)
        string(FIND "${src}" "sphaira: pass genuine total to progress callback (read transfer)" find_read_marker_after)
        string(FIND "${src}" "sphaira: pass genuine total to progress callback (write transfer)" find_write_marker_after)
        string(FIND "${src}" "data_header.length != 0xFFFFFFFFU" find_sentinel_check_after)
        string(FIND "${src}" "m_send_prop_list->size <= (u64)INT64_MAX" find_safe_cast_after)
        string(FIND "${src}" "data_header.length >= sizeof(PtpUsbBulkContainer)" find_zero_byte_after)

        if(find_read_cb_after EQUAL -1 OR find_write_cb_after EQUAL -1 OR find_read_marker_after EQUAL -1 OR find_write_marker_after EQUAL -1 OR find_sentinel_check_after EQUAL -1 OR find_safe_cast_after EQUAL -1 OR find_zero_byte_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply genuine total patch to ptp_responder_ptp_operations.cpp (unexpected shape or partial patch)")
        endif()
        file(WRITE "source/ptp_responder_ptp_operations.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_ptp_operations.cpp genuine total patch")
    endif()

    # --- 5. source/ptp_responder_ptp_operations.cpp : fix storage_id in SendObjectInfo ---
    string(FIND "${src}" "new_object_info.storage_id       = parentobj->GetStorageId();" find_ptp_storage_id)
    if(NOT find_ptp_storage_id EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_ptp_operations.cpp storage_id already patched")
    else()
        set(ptp_storage_old
"        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        new_object_info.storage_id       = parentobj->GetObjectId();
        new_object_info.parent_object_id = parent_object == storage_id ? 0 : parent_object;")
        set(ptp_storage_new
"        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        /* sphaira: fix storage_id to use parent storage ID instead of parent object handle. */
        new_object_info.storage_id       = parentobj->GetStorageId();
        new_object_info.parent_object_id = parent_object == storage_id ? 0 : parent_object;")
        string(REPLACE "${ptp_storage_old}" "${ptp_storage_new}" src "${src}")
        string(FIND "${src}" "new_object_info.storage_id       = parentobj->GetStorageId();" find_ptp_storage_after)
        if(find_ptp_storage_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply storage_id patch to ptp_responder_ptp_operations.cpp")
        endif()
        file(WRITE "source/ptp_responder_ptp_operations.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_ptp_operations.cpp storage_id patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] source/ptp_responder_ptp_operations.cpp not found")
endif()

# --- 6. source/ptp_responder_mtp_operations.cpp : MTP property handling and fixes ---
if(EXISTS "source/ptp_responder_mtp_operations.cpp")
    file(READ "source/ptp_responder_mtp_operations.cpp" src)

    # 6a. storage_id in SendObjectPropList
    string(FIND "${src}" "new_object_info.storage_id       = parentobj->GetStorageId();" find_mtp_storage_id)
    if(NOT find_mtp_storage_id EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_mtp_operations.cpp storage_id already patched")
    else()
        set(mtp_storage_old
"        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        new_object_info.storage_id       = parentobj->GetObjectId();
        new_object_info.parent_object_id = parent_object == storage_id ? 0 : parent_object;")
        set(mtp_storage_new
"        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        /* sphaira: fix storage_id to use parent storage ID instead of parent object handle. */
        new_object_info.storage_id       = parentobj->GetStorageId();
        new_object_info.parent_object_id = parent_object == storage_id ? 0 : parent_object;")
        string(REPLACE "${mtp_storage_old}" "${mtp_storage_new}" src "${src}")
        string(FIND "${src}" "new_object_info.storage_id       = parentobj->GetStorageId();" find_mtp_storage_after)
        if(find_mtp_storage_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply storage_id patch to ptp_responder_mtp_operations.cpp")
        endif()
        message(STATUS "[libhaze-patch] applied ptp_responder_mtp_operations.cpp storage_id patch")
    endif()

    # 6b. GetObjectPropDesc missing break
    string(FIND "${src}" "/* sphaira: fix missing break after U128 */" find_prop_desc_break)
    if(NOT find_prop_desc_break EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_mtp_operations.cpp GetObjectPropDesc break already patched")
    else()
        set(prop_desc_old
"                case PtpObjectPropertyCode_PersistentUniqueObjectIdentifier:
                    {
                        R_TRY(db.Add(PtpDataTypeCode_U128));
                        R_TRY(db.Add(PtpPropertyGetSetFlag_Get));
                        R_TRY(db.Add<u128>(0));
                    }
                case PtpObjectPropertyCode_ObjectSize:")
        set(prop_desc_new
"                case PtpObjectPropertyCode_PersistentUniqueObjectIdentifier:
                    {
                        R_TRY(db.Add(PtpDataTypeCode_U128));
                        R_TRY(db.Add(PtpPropertyGetSetFlag_Get));
                        R_TRY(db.Add<u128>(0));
                    }
                    /* sphaira: fix missing break after U128 */
                    break;
                case PtpObjectPropertyCode_ObjectSize:")
        string(REPLACE "${prop_desc_old}" "${prop_desc_new}" src "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_mtp_operations.cpp GetObjectPropDesc break patch")
    endif()

    # 6c. GetObjectPropList property_code == 0
    string(FIND "${src}" "/* sphaira: allow property_code == 0 */" find_prop_list_zero)
    if(NOT find_prop_list_zero EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_mtp_operations.cpp GetObjectPropList property_code == 0 already patched")
    else()
        set(prop_list_old
"        /* Ensure we have a valid property code. */
        R_UNLESS(property_code == -1 || IsSupportedObjectPropertyCode(PtpObjectPropertyCode(property_code)), haze::ResultUnknownPropertyCode());")
        set(prop_list_new
"        /* Ensure we have a valid property code. */
        /* sphaira: allow property_code == 0 */
        R_UNLESS(property_code == -1 || property_code == 0 || IsSupportedObjectPropertyCode(PtpObjectPropertyCode(property_code)), haze::ResultUnknownPropertyCode());")
        string(REPLACE "${prop_list_old}" "${prop_list_new}" src "${src}")

        set(should_inc_old
"        const auto ShouldIncludeProperty = [&] (PtpObjectPropertyCode code) {
            /* If all properties were requested, or it was the requested property, we should include the property. */
            return property_code == -1 || code == property_code;
        };")
        set(should_inc_new
"        const auto ShouldIncludeProperty = [&] (PtpObjectPropertyCode code) {
            /* If all properties were requested, or it was the requested property, we should include the property. */
            return property_code == -1 || property_code == 0 || code == property_code;
        };")
        string(REPLACE "${should_inc_old}" "${should_inc_new}" src "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_mtp_operations.cpp GetObjectPropList property_code == 0 patch")
    endif()

    # 6d. SendObjectPropList consume all properties
    string(FIND "${src}" "/* sphaira: consume all property types */" find_send_prop_loop)
    if(NOT find_send_prop_loop EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_mtp_operations.cpp SendObjectPropList consume loop already patched")
    else()
        set(send_prop_loop_old
"            switch (obj_property) {
                case PtpObjectPropertyCode_ObjectFileName:
                    {
                        R_UNLESS(type == PtpDataTypeCode_String, haze::ResultUnknownPropertyCode());
                        R_TRY((dp.ReadString(m_buffers->filename_string_buffer)));
                    }
                    break;
                default:
                    R_THROW(haze::ResultUnknownPropertyCode());
            }")
        set(send_prop_loop_new
"            /* sphaira: consume all property types */
            if (obj_property == PtpObjectPropertyCode_ObjectFileName ||
                (m_buffers->filename_string_buffer[0] == '\\x00' && obj_property == PtpObjectPropertyCode_Name)) {
                if (type == PtpDataTypeCode_String) {
                    R_TRY(dp.ReadString(m_buffers->filename_string_buffer));
                } else {
                    char dummy[256];
                    R_TRY(dp.ReadString(dummy));
                }
            } else {
                switch (type) {
                    case PtpDataTypeCode_S8:
                    case PtpDataTypeCode_U8:
                        {
                            u8 dummy;
                            R_TRY(dp.Read(std::addressof(dummy)));
                        }
                        break;
                    case PtpDataTypeCode_S16:
                    case PtpDataTypeCode_U16:
                        {
                            u16 dummy;
                            R_TRY(dp.Read(std::addressof(dummy)));
                        }
                        break;
                    case PtpDataTypeCode_S32:
                    case PtpDataTypeCode_U32:
                        {
                            u32 dummy;
                            R_TRY(dp.Read(std::addressof(dummy)));
                        }
                        break;
                    case PtpDataTypeCode_S64:
                    case PtpDataTypeCode_U64:
                        {
                            u64 dummy;
                            R_TRY(dp.Read(std::addressof(dummy)));
                        }
                        break;
                    case PtpDataTypeCode_S128:
                    case PtpDataTypeCode_U128:
                        {
                            u8 dummy[16];
                            u32 read_bytes;
                            R_TRY(dp.ReadBuffer(dummy, sizeof(dummy), std::addressof(read_bytes)));
                        }
                        break;
                    case PtpDataTypeCode_String:
                        {
                            char dummy[256];
                            R_TRY(dp.ReadString(dummy));
                        }
                        break;
                    default:
                        if (type & PtpDataTypeCode_ArrayMask) {
                            u32 count = 0;
                            R_TRY(dp.Read(std::addressof(count)));
                            const u32 elem_type = type & ~PtpDataTypeCode_ArrayMask;
                            u32 elem_size = 1;
                            if (elem_type == PtpDataTypeCode_U16 || elem_type == PtpDataTypeCode_S16) elem_size = 2;
                            else if (elem_type == PtpDataTypeCode_U32 || elem_type == PtpDataTypeCode_S32) elem_size = 4;
                            else if (elem_type == PtpDataTypeCode_U64 || elem_type == PtpDataTypeCode_S64) elem_size = 8;
                            else if (elem_type == PtpDataTypeCode_U128 || elem_type == PtpDataTypeCode_S128) elem_size = 16;
                            for (u32 a = 0; a < count; a++) {
                                u8 dummy_buf[16];
                                u32 read_bytes;
                                R_TRY(dp.ReadBuffer(dummy_buf, elem_size, std::addressof(read_bytes)));
                            }
                        }
                        break;
                }
            }")
        string(REPLACE "${send_prop_loop_old}" "${send_prop_loop_new}" src "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_mtp_operations.cpp SendObjectPropList consume loop patch")
    endif()

    # 6e. SetObjectPropValue allow Name
    string(FIND "${src}" "/* sphaira: allow Name in SetObjectPropValue */" find_set_prop_name)
    if(NOT find_set_prop_name EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_mtp_operations.cpp SetObjectPropValue Name already patched")
    else()
        set(set_prop_old
"        /* Ensure we have a valid property code before continuing. */
        R_UNLESS(property_code == PtpObjectPropertyCode_ObjectFileName, haze::ResultUnknownPropertyCode());")
        set(set_prop_new
"        /* Ensure we have a valid property code before continuing. */
        /* sphaira: allow Name in SetObjectPropValue */
        R_UNLESS(property_code == PtpObjectPropertyCode_ObjectFileName || property_code == PtpObjectPropertyCode_Name, haze::ResultUnknownPropertyCode());")
        string(REPLACE "${set_prop_old}" "${set_prop_new}" src "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_mtp_operations.cpp SetObjectPropValue Name patch")
    endif()

    file(WRITE "source/ptp_responder_mtp_operations.cpp" "${src}")
else()
    message(FATAL_ERROR "[libhaze-patch] source/ptp_responder_mtp_operations.cpp not found")
endif()

# --- 7. include/haze/ptp_data_parser.hpp : UTF-16 to UTF-8 decoding ----------
if(EXISTS "include/haze/ptp_data_parser.hpp")
    file(READ "include/haze/ptp_data_parser.hpp" src)
    string(FIND "${src}" "/* sphaira: decode UTF-16 to UTF-8 */" find_utf16_decode)
    if(NOT find_utf16_decode EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_data_parser.hpp UTF-16 decode already patched")
    else()
        set(parser_read_string_old
"            /* NOTE: out_string must contain room for 256 bytes. */
            /* The result will be null-terminated on successful completion. */
            Result ReadString(char *out_string) {
                u8 len;
                R_TRY(this->Read(std::addressof(len)));

                /* Read characters one by one. */
                for (size_t i = 0; i < len; i++) {
                    u16 chr;
                    R_TRY(this->Read(std::addressof(chr)));

                    *out_string++ = static_cast<char>(chr);
                }

                /* Write null terminator. */
                *out_string++ = '\\x00';

                R_SUCCEED();
            }")
        set(parser_read_string_new
"            /* NOTE: out_string must contain room for 256 bytes. */
            /* The result will be null-terminated on successful completion. */
            /* sphaira: decode UTF-16 to UTF-8 */
            Result ReadString(char *out_string) {
                u8 len;
                R_TRY(this->Read(std::addressof(len)));

                char *out = out_string;
                char * const out_end = out_string + 255;

                for (size_t i = 0; i < len; i++) {
                    u16 chr;
                    R_TRY(this->Read(std::addressof(chr)));

                    if (chr == 0) {
                        continue;
                    }

                    if (chr < 0x80) {
                        if (out < out_end) *out++ = static_cast<char>(chr);
                    } else if (chr < 0x800) {
                        if (out + 1 < out_end) {
                            *out++ = static_cast<char>(0xC0 | (chr >> 6));
                            *out++ = static_cast<char>(0x80 | (chr & 0x3F));
                        }
                    } else {
                        if (out + 2 < out_end) {
                            *out++ = static_cast<char>(0xE0 | (chr >> 12));
                            *out++ = static_cast<char>(0x80 | ((chr >> 6) & 0x3F));
                            *out++ = static_cast<char>(0x80 | (chr & 0x3F));
                        }
                    }
                }

                *out = '\\x00';
                R_SUCCEED();
            }")
        string(REPLACE "${parser_read_string_old}" "${parser_read_string_new}" src "${src}")
        file(WRITE "include/haze/ptp_data_parser.hpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_data_parser.hpp UTF-16 decode patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] include/haze/ptp_data_parser.hpp not found")
endif()

# --- 8. include/haze/ptp_data_builder.hpp : UTF-8 to UTF-16 encoding ----------
if(EXISTS "include/haze/ptp_data_builder.hpp")
    file(READ "include/haze/ptp_data_builder.hpp" src)
    string(FIND "${src}" "/* sphaira: encode UTF-8 to UTF-16 */" find_utf8_encode)
    if(NOT find_utf8_encode EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_data_builder.hpp UTF-8 encode already patched")
    else()
        set(builder_add_string_old
"            template <typename T>
            Result AddString(const T *str) {
                /* Use one less than the maximum string length for maximum length with null terminator. */
                const u8 len = static_cast<u8>(std::min<s32>(util::Strlen(str), PtpStringMaxLength - 1));

                if (len > 0) {
                    /* Length is padded by null terminator for non-empty strings. */
                    R_TRY(this->Add<u8>(len + 1));

                    for (size_t i = 0; i < len; i++) {
                        R_TRY(this->Add<u16>(str[i]));
                    }

                    R_TRY(this->Add<u16>(0));
                } else {
                    R_TRY(this->Add<u8>(len));
                }

                R_SUCCEED();
            }")
        set(builder_add_string_new
"            /* sphaira: encode UTF-8 to UTF-16 */
            template <typename T>
            Result AddString(const T *str) {
                if (!str || !*str) {
                    R_TRY(this->Add<u8>(0));
                    R_SUCCEED();
                }

                u16 utf16[256];
                size_t u16_len = 0;
                const u8* p = reinterpret_cast<const u8*>(str);
                while (*p && u16_len < static_cast<size_t>(PtpStringMaxLength - 1)) {
                    u32 codepoint = 0;
                    if (*p < 0x80) {
                        codepoint = *p++;
                    } else if ((*p & 0xE0) == 0xC0) {
                        if ((p[1] & 0xC0) != 0x80) break;
                        codepoint = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
                        p += 2;
                    } else if ((*p & 0xF0) == 0xE0) {
                        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) break;
                        codepoint = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
                        p += 3;
                    } else if ((*p & 0xF8) == 0xF0) {
                        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) break;
                        codepoint = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
                        p += 4;
                    } else {
                        p++;
                        continue;
                    }

                    if (codepoint < 0x10000) {
                        utf16[u16_len++] = static_cast<u16>(codepoint);
                    } else if (codepoint <= 0x10FFFF && u16_len + 1 < static_cast<size_t>(PtpStringMaxLength - 1)) {
                        codepoint -= 0x10000;
                        utf16[u16_len++] = static_cast<u16>(0xD800 + (codepoint >> 10));
                        utf16[u16_len++] = static_cast<u16>(0xDC00 + (codepoint & 0x3FF));
                    }
                }

                const u8 count = static_cast<u8>(u16_len + 1);
                R_TRY(this->Add<u8>(count));
                for (size_t i = 0; i < u16_len; i++) {
                    R_TRY(this->Add<u16>(utf16[i]));
                }
                R_TRY(this->Add<u16>(0));

                R_SUCCEED();
            }")
        string(REPLACE "${builder_add_string_old}" "${builder_add_string_new}" src "${src}")
        file(WRITE "include/haze/ptp_data_builder.hpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_data_builder.hpp UTF-8 encode patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] include/haze/ptp_data_builder.hpp not found")
endif()
