# libhaze patch data: sections 7, 8, 9
# 7. include/haze/ptp_data_parser.hpp: UTF-16 to UTF-8 decoding
# 8. include/haze/ptp_data_builder.hpp: UTF-8 to UTF-16 encoding
# 9. source/threaded_file_transfer.cpp: resize read buffer before EOF break

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

# --- 9. source/threaded_file_transfer.cpp : resize read buffer before EOF break ---
if(EXISTS "source/threaded_file_transfer.cpp")
    file(READ "source/threaded_file_transfer.cpp" src)
    string(FIND "${src}" "buf.resize(buf_offset + bytes_read);
        if (!bytes_read) {" find_patched_offset)
    string(FIND "${src}" "buf.resize(bytes_read);
        if (!bytes_read) {" find_patched_no_offset)

    if(NOT find_patched_offset EQUAL -1 OR NOT find_patched_no_offset EQUAL -1)
        message(STATUS "[libhaze-patch] threaded_file_transfer.cpp EOF resize already patched")
    else()
        # Shape A: libhaze with slow-mode / buf_offset (e.g. 81154c1)
        set(tft_offset_old
"        u64 bytes_read{};
        R_TRY(this->Read(buf.data() + buf_offset, read_size, std::addressof(bytes_read)));
        if (!bytes_read) {
            break;
        }

        // resize to actual read size.
        buf.resize(buf_offset + bytes_read);")

        set(tft_offset_new
"        u64 bytes_read{};
        R_TRY(this->Read(buf.data() + buf_offset, read_size, std::addressof(bytes_read)));

        // resize to actual read size.
        buf.resize(buf_offset + bytes_read);
        if (!bytes_read) {
            break;
        }")

        # Shape B: libhaze without buf_offset (e.g. 0be1523)
        set(tft_no_offset_old
"        u64 bytes_read{};
        buf.resize(read_size);
        R_TRY(this->Read(buf.data(), read_size, std::addressof(bytes_read)));
        if (!bytes_read) {
            break;
        }")

        set(tft_no_offset_new
"        u64 bytes_read{};
        buf.resize(read_size);
        R_TRY(this->Read(buf.data(), read_size, std::addressof(bytes_read)));

        // resize to actual read size.
        buf.resize(bytes_read);
        if (!bytes_read) {
            break;
        }")

        string(REPLACE "${tft_offset_old}" "${tft_offset_new}" src "${src}")
        string(REPLACE "${tft_no_offset_old}" "${tft_no_offset_new}" src "${src}")

        string(FIND "${src}" "buf.resize(buf_offset + bytes_read);
        if (!bytes_read) {" find_after_offset)
        string(FIND "${src}" "buf.resize(bytes_read);
        if (!bytes_read) {" find_after_no_offset)

        if(find_after_offset EQUAL -1 AND find_after_no_offset EQUAL -1)
            message(FATAL_ERROR "[libhaze-patch] failed to apply EOF buffer resize patch to threaded_file_transfer.cpp (unexpected shape or partial patch)")
        endif()
        file(WRITE "source/threaded_file_transfer.cpp" "${src}")
        message(STATUS "[libhaze-patch] applied threaded_file_transfer.cpp EOF buffer resize patch")
    endif()
else()
    message(FATAL_ERROR "[libhaze-patch] source/threaded_file_transfer.cpp not found")
endif()
