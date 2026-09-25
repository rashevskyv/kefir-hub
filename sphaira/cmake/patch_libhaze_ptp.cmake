# libhaze patch ptp: sections 4, 5, 5b
# 4. source/ptp_responder_ptp_operations.cpp: pass genuine total in GetObject and SendObject
# 5. source/ptp_responder_ptp_operations.cpp: fix storage_id in SendObjectInfo
# 5b. source/ptp_responder_ptp_operations.cpp: expose Kefir Hub responder version in GetDeviceInfo

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

    # --- 5b. source/ptp_responder_ptp_operations.cpp : expose Kefir Hub responder version in GetDeviceInfo ---
    string(FIND "${src}" "std::snprintf(device_version, sizeof(device_version), \"Kefir Hub/%s (HOS/%s)\", SPHAIRA_VERSION, GetFirmwareVersion());" find_dev_ver)
    string(FIND "${src}" "R_TRY(db.AddString(device_version));" find_dev_str)
    string(FIND "${src}" "#include <cstdio>" find_dev_inc)

    if(NOT find_dev_ver EQUAL -1 AND NOT find_dev_str EQUAL -1 AND NOT find_dev_inc EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_ptp_operations.cpp device_version already patched")
    else()
        set(dev_inc_old "#include \"haze/threaded_file_transfer.hpp\"")
        set(dev_inc_new "#include \"haze/threaded_file_transfer.hpp\"
#include <cstdio>")

        set(dev_init_old
"    Result PtpResponder::GetDeviceInfo(PtpDataParser &dp) {
        PtpDataBuilder db(m_buffers->usb_bulk_write_buffer, std::addressof(m_usb_server));

        /* Write the device info data. */")
        set(dev_init_sphaira
"    Result PtpResponder::GetDeviceInfo(PtpDataParser &dp) {
        PtpDataBuilder db(m_buffers->usb_bulk_write_buffer, std::addressof(m_usb_server));
        char device_version[0x40];
        std::snprintf(device_version, sizeof(device_version), \"Sphaira/%s (HOS/%s)\", SPHAIRA_VERSION, GetFirmwareVersion());

        /* Write the device info data. */")
        set(dev_init_new
"    Result PtpResponder::GetDeviceInfo(PtpDataParser &dp) {
        PtpDataBuilder db(m_buffers->usb_bulk_write_buffer, std::addressof(m_usb_server));
        char device_version[0x40];
        std::snprintf(device_version, sizeof(device_version), \"Kefir Hub/%s (HOS/%s)\", SPHAIRA_VERSION, GetFirmwareVersion());

        /* Write the device info data. */")

        set(dev_add_old
"            R_TRY(db.AddString(MtpDeviceManufacturer));
            R_TRY(db.AddString(MtpDeviceModel));
            R_TRY(db.AddString(GetFirmwareVersion()));
            R_TRY(db.AddString(GetSerialNumber()));")
        set(dev_add_new
"            R_TRY(db.AddString(MtpDeviceManufacturer));
            R_TRY(db.AddString(MtpDeviceModel));
            R_TRY(db.AddString(device_version));
            R_TRY(db.AddString(GetSerialNumber()));")

        if(find_dev_inc EQUAL -1)
            string(REPLACE "${dev_inc_old}" "${dev_inc_new}" src "${src}")
        endif()
        string(REPLACE "${dev_init_sphaira}" "${dev_init_new}" src "${src}")
        string(REPLACE "${dev_init_old}" "${dev_init_new}" src "${src}")
        string(REPLACE "${dev_add_old}" "${dev_add_new}" src "${src}")

        string(FIND "${src}" "std::snprintf(device_version, sizeof(device_version), \"Kefir Hub/%s (HOS/%s)\", SPHAIRA_VERSION, GetFirmwareVersion());" find_dev_ver_after)
        string(FIND "${src}" "R_TRY(db.AddString(device_version));" find_dev_str_after)
        string(FIND "${src}" "#include <cstdio>" find_dev_inc_after)

        if(find_dev_ver_after EQUAL -1 OR find_dev_str_after EQUAL -1 OR find_dev_inc_after EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply device_version patch to ptp_responder_ptp_operations.cpp (unexpected shape or partial patch)")
        endif()
        file(WRITE "source/ptp_responder_ptp_operations.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied ptp_responder_ptp_operations.cpp device_version patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] source/ptp_responder_ptp_operations.cpp not found")
endif()
