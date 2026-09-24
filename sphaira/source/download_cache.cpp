#include "download_internal.hpp"
#include "log.hpp"
#include <yyjson.h>

namespace sphaira::curl {

static auto generate_key_from_path(const fs::FsPath& path) -> std::string {
    const auto key = crc32Calculate(path.s, path.size());
    return std::to_string(key);
}

bool Cache::init() {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    if (m_json) {
        return true;
    }

    auto json_in = yyjson_read_file(JSON_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (json_in) {
        log_write("loading old json doc\n");
        m_json = yyjson_doc_mut_copy(json_in, nullptr);
        yyjson_doc_free(json_in);
        m_root = yyjson_mut_doc_get_root(m_json);
    } else {
        log_write("creating new json doc\n");
        m_json = yyjson_mut_doc_new(nullptr);
        m_root = yyjson_mut_obj(m_json);
        yyjson_mut_doc_set_root(m_json, m_root);
    }

    return m_json && m_root;
}

void Cache::exit() {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    if (!m_json) {
        return;
    }

    if (!yyjson_mut_write_file(JSON_PATH, m_json, YYJSON_WRITE_NOFLAG, nullptr, nullptr)) {
        log_write("failed to write etag json: %s\n", JSON_PATH.s);
    }

    yyjson_mut_doc_free(m_json);
    m_json = nullptr;
    m_root = nullptr;
}

void Cache::get(const fs::FsPath& path, curl::Header& header) {
    const auto [etag, last_modified] = get_internal(path);
    if (!etag.empty()) {
        header.m_map.emplace("if-none-match", etag);
    }

    if (!last_modified.empty()) {
        header.m_map.emplace("if-modified-since", last_modified);
    }
}

void Cache::set(const fs::FsPath& path, const curl::Header& value) {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    std::string etag_str;
    std::string last_modified_str;

    if (auto it = value.Find(ETAG_STR); it != value.m_map.end()) {
        etag_str = it->second;
    }
    if (auto it = value.Find(LAST_MODIFIED_STR); it != value.m_map.end()) {
        last_modified_str = it->second;
    }

    if (!etag_str.empty() || !last_modified_str.empty()) {
        set_internal(path, Value{etag_str, last_modified_str});
    }
}

auto Cache::get_internal(const fs::FsPath& path) -> Value {
    if (!fs::FsNativeSd().FileExists(path)) {
        return {};
    }

    const auto kkey = generate_key_from_path(path);
    const auto it = m_cache.find(kkey);
    if (it != m_cache.end()) {
        return it->second;
    }

    auto hash_key = yyjson_mut_obj_getn(m_root, kkey.c_str(), kkey.length());
    if (!hash_key) {
        return {};
    }

    auto etag_key = yyjson_mut_obj_get(hash_key, ETAG_STR);
    auto last_modified_key = yyjson_mut_obj_get(hash_key, LAST_MODIFIED_STR);

    const auto etag_value = yyjson_mut_get_str(etag_key);
    const auto etag_value_len = yyjson_mut_get_len(etag_key);
    const auto last_modified_value = yyjson_mut_get_str(last_modified_key);
    const auto last_modified_value_len = yyjson_mut_get_len(last_modified_key);

    if ((!etag_value || !etag_value_len) && (!last_modified_value || !last_modified_value_len)) {
        return {};
    }

    std::string etag;
    std::string last_modified;
    if (etag_value && etag_value_len) {
        etag.assign(etag_value, etag_value_len);
    }
    if (last_modified_value && last_modified_value_len) {
        last_modified.assign(last_modified_value, last_modified_value_len);
    }

    const Value ret{etag, last_modified};
    m_cache.insert_or_assign(it, kkey, ret);
    return ret;
}

void Cache::set_internal(const fs::FsPath& path, const Value& value) {
    const auto kkey = generate_key_from_path(path);

    // check if we already have this entry
    const auto it = m_cache.find(kkey);
    if (it != m_cache.end() && it->second == value) {
        log_write("already has etag, not updating, path: %s key: %s\n", path.s, kkey.c_str());
        return;
    }

    if (it != m_cache.end()) {
        log_write("updating etag, path: %s key: %s\n", path.s, kkey.c_str());
    } else {
        log_write("setting new etag, path: %s key: %s\n", path.s, kkey.c_str());
    }

    // insert new entry into cache, this will never fail.
    const auto& [jkey, jvalue] = *m_cache.insert_or_assign(it, kkey, value);
    const auto& [etag, last_modified] = jvalue;

    // check if we need to add a new entry to root or simply update the value.
    auto hash_key = yyjson_mut_obj_getn(m_root, kkey.c_str(), kkey.length());
    if (!hash_key) {
        hash_key = yyjson_mut_obj_add_obj(m_json, m_root, jkey.c_str());
    }

    if (!hash_key) {
        log_write("failed to set new cache key obj, path: %s key: %s\n", path.s, jkey.c_str());
    } else {
        const auto update_entry = [this, &hash_key](const char* tag, const std::string& value) {
            if (value.empty()) {
                // workaround for appstore accepting etags but not returning them.
                yyjson_mut_obj_remove_str(hash_key, tag);
                return true;
            } else {
                auto key = yyjson_mut_obj_get(hash_key, tag);
                if (!key) {
                    return yyjson_mut_obj_add_str(m_json, hash_key, tag, value.c_str());
                } else {
                    return yyjson_mut_set_str(key, value.c_str());
                }
            }
        };

        if (!update_entry("etag", etag)) {
            log_write("failed to set new etag, path: %s key: %s\n", path.s, jkey.c_str());
        }

        if (!update_entry("last-modified", last_modified)) {
            log_write("failed to set new last-modified, path: %s key: %s\n", path.s, jkey.c_str());
        }
    }
}

Cache g_cache;

} // namespace sphaira::curl
