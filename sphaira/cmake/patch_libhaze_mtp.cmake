# libhaze patch mtp: section 6
# 6. source/ptp_responder_mtp_operations.cpp: MTP property handling and fixes

# --- 6. source/ptp_responder_mtp_operations.cpp : MTP property handling and fixes ---
if(EXISTS "source/ptp_responder_mtp_operations.cpp")
    file(READ "source/ptp_responder_mtp_operations.cpp" src)

    # 6a. storage_id in SendObjectPropList
    string(FIND "${src}" "/* sphaira: resolve parent object for device root" find_mtp_device_root)
    if(NOT find_mtp_device_root EQUAL -1)
        message(STATUS "[libhaze-patch] ptp_responder_mtp_operations.cpp storage_id already patched")
    else()
        set(mtp_early_lookup
"        R_TRY(rdp.Read(std::addressof(object_size_lsb)));
        R_TRY(rdp.Finalize());

        /* Rewrite requests for creating in storage directories. */
        if (parent_object == PtpGetObjectHandles_RootParent) {
            parent_object = storage_id;
        }

        /* Check if we know about the parent object. If we don't, it's an error. */
        auto * const parentobj = m_object_database.GetObjectById(parent_object);
        R_UNLESS(parentobj != nullptr, haze::ResultInvalidObjectId());

        PtpDataParser dp(m_buffers->usb_bulk_read_buffer, std::addressof(m_usb_server));")

        set(mtp_deferred_lookup
"        R_TRY(rdp.Read(std::addressof(object_size_lsb)));
        R_TRY(rdp.Finalize());

        PtpDataParser dp(m_buffers->usb_bulk_read_buffer, std::addressof(m_usb_server));")

        set(mtp_create_patched
"        /* Ensure we can actually process the new name. */
        const bool is_empty         = m_buffers->filename_string_buffer[0] == '\\x00';
        const bool contains_slashes = std::strchr(m_buffers->filename_string_buffer, '/') != nullptr;
        R_UNLESS(!is_empty && !contains_slashes, haze::ResultInvalidPropertyValue());

        /* Add a new object in the database with the new name. */
        PtpObject *newobj;
        R_TRY(m_object_database.CreateOrFindObject(parentobj->GetName(), m_buffers->filename_string_buffer, parentobj->GetObjectId(), parentobj->GetStorageId(), std::addressof(newobj)));

        /* Create prop list. */
        ObjectPropList prop_list{};
        prop_list.size = ((u64)object_size_msb << 32) | object_size_lsb;
        m_send_prop_list = prop_list;

        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        /* sphaira: fix storage_id to use parent storage ID instead of parent object handle. */
        new_object_info.storage_id       = parentobj->GetStorageId();
        new_object_info.parent_object_id = parent_object == storage_id ? 0 : parent_object;")

        set(mtp_create_unpatched
"        /* Ensure we can actually process the new name. */
        const bool is_empty         = m_buffers->filename_string_buffer[0] == '\\x00';
        const bool contains_slashes = std::strchr(m_buffers->filename_string_buffer, '/') != nullptr;
        R_UNLESS(!is_empty && !contains_slashes, haze::ResultInvalidPropertyValue());

        /* Add a new object in the database with the new name. */
        PtpObject *newobj;
        R_TRY(m_object_database.CreateOrFindObject(parentobj->GetName(), m_buffers->filename_string_buffer, parentobj->GetObjectId(), parentobj->GetStorageId(), std::addressof(newobj)));

        /* Create prop list. */
        ObjectPropList prop_list{};
        prop_list.size = ((u64)object_size_msb << 32) | object_size_lsb;
        m_send_prop_list = prop_list;

        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        new_object_info.storage_id       = parentobj->GetObjectId();
        new_object_info.parent_object_id = parent_object == storage_id ? 0 : parent_object;")

        set(mtp_create_new
"        /* Ensure we can actually process the new name. */
        const bool is_empty         = m_buffers->filename_string_buffer[0] == '\\x00';
        const bool contains_slashes = std::strchr(m_buffers->filename_string_buffer, '/') != nullptr;
        R_UNLESS(!is_empty && !contains_slashes, haze::ResultInvalidPropertyValue());

        /* sphaira: resolve parent object for device root (drag & drop / Send To) or storage directory. */
        const bool is_device_root = (storage_id == 0 || storage_id == PtpGetObjectHandles_AllStorage) &&
                                    (parent_object == 0 || parent_object == PtpGetObjectHandles_RootParent);
        if (is_device_root) {
            bool is_install_package = false;
            if (format_code != PtpObjectFormatCode_Association) {
                const char* ext = std::strrchr(m_buffers->filename_string_buffer, '.');
                if (ext != nullptr) {
                    if (strcasecmp(ext, \".nsp\") == 0 || strcasecmp(ext, \".nsz\") == 0 ||
                        strcasecmp(ext, \".xci\") == 0 || strcasecmp(ext, \".xcz\") == 0) {
                        is_install_package = true;
                    }
                }
            }

            if (is_install_package) {
                const auto it = std::find_if(m_fs_entries.cbegin(), m_fs_entries.cend(), [](const auto& e) {
                    return std::strcmp(e.impl->GetName(), \"install\") == 0;
                });
                R_UNLESS(it != m_fs_entries.cend(), haze::ResultInvalidStorageId());
                parent_object = it->storage_id;
            } else {
                const auto it = std::find_if(m_fs_entries.cbegin(), m_fs_entries.cend(), [](const auto& e) {
                    return e.impl->GetName()[0] == '\\0';
                });
                R_UNLESS(it != m_fs_entries.cend(), haze::ResultInvalidStorageId());
                parent_object = it->storage_id;
            }
        } else if (parent_object == PtpGetObjectHandles_RootParent || parent_object == 0) {
            parent_object = storage_id;
        }

        /* Check if we know about the parent object. If we don't, it's an error. */
        auto * const parentobj = m_object_database.GetObjectById(parent_object);
        R_UNLESS(parentobj != nullptr, haze::ResultInvalidObjectId());

        /* Add a new object in the database with the new name. */
        PtpObject *newobj;
        R_TRY(m_object_database.CreateOrFindObject(parentobj->GetName(), m_buffers->filename_string_buffer, parentobj->GetObjectId(), parentobj->GetStorageId(), std::addressof(newobj)));

        /* Create prop list. */
        ObjectPropList prop_list{};
        prop_list.size = ((u64)object_size_msb << 32) | object_size_lsb;
        m_send_prop_list = prop_list;

        /* Make a new object with the intended name. */
        PtpNewObjectInfo new_object_info;
        /* sphaira: fix storage_id to use parent storage ID instead of parent object handle. */
        new_object_info.storage_id       = parentobj->GetStorageId();
        new_object_info.parent_object_id = (parent_object == storage_id || parent_object == parentobj->GetStorageId()) ? 0 : parent_object;")

        string(REPLACE "${mtp_early_lookup}" "${mtp_deferred_lookup}" src "${src}")
        string(REPLACE "${mtp_create_patched}" "${mtp_create_new}" src "${src}")
        string(REPLACE "${mtp_create_unpatched}" "${mtp_create_new}" src "${src}")
        string(FIND "${src}" "/* sphaira: resolve parent object for device root" find_mtp_storage_after)
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
