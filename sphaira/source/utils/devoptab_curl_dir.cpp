#include "utils/devoptab_curl_device.hpp"
#include "log.hpp"
#include "defines.hpp"
#include <yyjson.h>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

namespace sphaira::devoptab::common {

namespace {

std::string to_lower_copy(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// finds the next xml tag with the given local name, ignoring any namespace
// prefix: "<d:response", "<D:response", "<ns0:response" and "<response" all
// match "response". haystack and name must be lowercase.
size_t find_xml_tag(const std::string& haystack, size_t pos, std::string_view name, bool closing = false) {
    while ((pos = haystack.find('<', pos)) != std::string::npos) {
        auto p = pos + 1;
        if (closing) {
            if (p >= haystack.size() || haystack[p] != '/') {
                pos = p;
                continue;
            }
            p++;
        }

        const auto end = haystack.find_first_of(" \t\r\n/>", p);
        if (end == std::string::npos) {
            break;
        }

        std::string_view tag{haystack.data() + p, end - p};
        if (const auto colon = tag.rfind(':'); colon != std::string_view::npos) {
            tag = tag.substr(colon + 1);
        }

        if (tag == name) {
            return pos;
        }

        pos = end;
    }

    return std::string::npos;
}

bool parse_sphaira_directory_json(const std::vector<char>& data, const char* dir_path, std::vector<dircache>& out_entries) {
    if (data.empty()) {
        return false;
    }

    yyjson_doc* doc = yyjson_read(data.data(), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        return false;
    }

    yyjson_val* path_val = yyjson_obj_get(root, "path");
    if (!yyjson_is_str(path_val)) {
        return false;
    }

    yyjson_val* entries_val = yyjson_obj_get(root, "entries");
    if (!yyjson_is_arr(entries_val)) {
        return false;
    }

    std::vector<dircache> entries;
    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(entries_val, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            return false;
        }
        yyjson_val* name_val = yyjson_obj_get(item, "name");
        yyjson_val* type_val = yyjson_obj_get(item, "type");
        yyjson_val* size_val = yyjson_obj_get(item, "size");

        const char* name_str = yyjson_get_str(name_val);
        if (!yyjson_is_str(name_val) || !name_str || !*name_str || std::strcmp(name_str, ".") == 0 || std::strcmp(name_str, "..") == 0) {
            return false;
        }
        if ((!yyjson_is_int(type_val) && !yyjson_is_uint(type_val)) || (!yyjson_is_int(size_val) && !yyjson_is_uint(size_val))) {
            return false;
        }

        int entry_type = yyjson_is_int(type_val) ? yyjson_get_int(type_val) : (int)yyjson_get_uint(type_val);
        s64 entry_size;
        if (yyjson_is_int(size_val)) {
            entry_size = (s64)yyjson_get_sint(size_val);
        } else {
            entry_size = (s64)yyjson_get_uint(size_val);
        }
        bool is_dir = (entry_type == FsDirEntryType_Dir);

        dircache entry{};
        entry.name = name_str;
        entry.fullpathname = std::string(dir_path) + (std::string(dir_path).ends_with('/') ? "" : "/") + name_str;
        entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        entry.st.st_size = is_dir ? 0 : entry_size;
        entry.st.st_nlink = 1;
        entries.push_back(entry);
    }

    out_entries = std::move(entries);
    return true;
}

} // namespace

int MountCurlDevice::devoptab_diropen(void* fd, const char *path) {
    SCOPED_MUTEX(&m_handle_mutex);
    auto* state = static_cast<CurlDirState*>(fd);
    new (state) CurlDirState();

    const bool is_http = config.url.starts_with("http://") || config.url.starts_with("https://");

    if (is_http) {
        if (m_sphaira_state == SphairaShareState::Unknown) {
            // First directory open on HTTP source: probe Sphaira's /list endpoint
            std::string logical_path = path ? path : "/";
            if (!m_url_path.empty()) {
                auto base = m_url_path;
                if (base.ends_with('/')) {
                    base.pop_back();
                }
                if (logical_path.starts_with('/')) {
                    logical_path = base + logical_path;
                } else {
                    logical_path = base + '/' + logical_path;
                }
            }
            if (logical_path.empty() || !logical_path.starts_with('/')) {
                logical_path = '/' + logical_path;
            }

            char* escaped = curl_easy_escape(curl, logical_path.c_str(), logical_path.length());
            std::string query = "path=" + std::string(escaped ? escaped : "");
            if (escaped) {
                curl_free(escaped);
            }

            curl_url_set(curlu, CURLUPART_PATH, "/list", 0);
            curl_url_set(curlu, CURLUPART_QUERY, query.c_str(), 0);
            char* probe_url_c{};
            const auto q_rc = curl_url_get(curlu, CURLUPART_URL, &probe_url_c, 0);
            curl_url_set(curlu, CURLUPART_QUERY, nullptr, 0);

            if (q_rc == CURLUE_OK && probe_url_c) {
                std::string probe_url = probe_url_c;
                curl_free(probe_url_c);

                log_write("[CURL] probing Sphaira /list endpoint: %s\n", probe_url.c_str());

                std::vector<char> response_data;
                curl_easy_reset(curl);
                curl_set_common_options(curl, probe_url);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

                CURLcode res = curl_perform_cancellable(curl);
                long code{};
                if (res == CURLE_OK) {
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
                }

                if (res == CURLE_OK && code == 200 && parse_sphaira_directory_json(response_data, path, state->entries)) {
                    m_sphaira_state = SphairaShareState::Detected;
                    log_write("[CURL] detected Sphaira share via /list probe, loaded %zu entries\n", state->entries.size());
                    return 0;
                }

                log_write("[CURL] Sphaira /list probe not matched (res=%d, code=%ld), trying WebDAV/HTML fallback\n", res, code);
                m_sphaira_state = SphairaShareState::NotSphaira;
            } else {
                m_sphaira_state = SphairaShareState::NotSphaira;
            }
        } else if (m_sphaira_state == SphairaShareState::Detected) {
            std::string list_url = build_url(path, true);
            log_write("[CURL] diropen Sphaira list url: %s\n", list_url.c_str());

            std::vector<char> response_data;
            curl_easy_reset(curl);
            curl_set_common_options(curl, list_url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

            CURLcode res = curl_perform_cancellable(curl);
            long code{};
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            }

            if (res != CURLE_OK || code != 200) {
                log_write("[CURL] diropen Sphaira list failed (res=%d, code=%ld, path=%s)\n", res, code, path);
                return -EIO;
            }

            if (!parse_sphaira_directory_json(response_data, path, state->entries)) {
                log_write("[CURL] diropen Sphaira list invalid JSON (path=%s)\n", path);
                return -EIO;
            }

            return 0;
        }
    }

    std::string full_url = build_url(path, true);
    log_write("[CURL] diropen url: %s\n", full_url.c_str());

    std::vector<char> response_data;

    curl_easy_reset(curl);
    curl_set_common_options(curl, full_url);

    bool is_ftp = full_url.starts_with("ftp://") || full_url.starts_with("ftps://");

    // the header list must outlive curl_easy_perform() below.
    struct curl_slist* list = nullptr;
    ON_SCOPE_EXIT(curl_slist_free_all(list));

    if (!is_ftp) {
        // for ftp, curl issues a LIST for urls with a trailing slash,
        // which gives us entry types and sizes (unlike NLST).
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        list = curl_slist_append(nullptr, "Depth: 1");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_perform_cancellable(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    if (is_ftp && res != CURLE_OK) {
        log_write("[CURL] diropen perform failed: %s\n", curl_easy_strerror(res));
        return -EIO;
    }

    std::string data_str(response_data.begin(), response_data.end());
    std::string lc = to_lower_copy(data_str);

    bool webdav = false;
    if (!is_ftp) {
        webdav = res == CURLE_OK && (code == 207 || (code >= 200 && code < 300 && lc.find("multistatus") != std::string::npos));
        if (!webdav) {
            // not a webdav server (or PROPFIND was rejected): fall back to
            // fetching the plain html directory index.
            log_write("[CURL] diropen PROPFIND unavailable (res=%d, code=%ld), trying html index\n", res, code);

            response_data.clear();
            curl_easy_reset(curl);
            curl_set_common_options(curl, full_url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

            res = curl_perform_cancellable(curl);
            if (res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            }
            if (res != CURLE_OK || code < 200 || code >= 300) {
                log_write("[CURL] diropen html index failed (res=%d, code=%ld)\n", res, code);
                return -EIO;
            }

            data_str.assign(response_data.begin(), response_data.end());
            lc = to_lower_copy(data_str);
        }
    }

    if (is_ftp) {
        std::string line;
        std::istringstream stream(data_str);
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || line.starts_with("total ")) {
                continue;
            }

            std::string name;
            bool is_dir = false;
            s64 size = 0;

            if (line[0] == 'd' || line[0] == '-' || line[0] == 'l') {
                // unix LIST: "drwxr-xr-x 2 owner group 4096 Jan 16 19:00 name with spaces"
                is_dir = line[0] == 'd';

                std::istringstream ls(line);
                std::string tok, size_tok;
                int field = 0;
                while (field < 8 && (ls >> tok)) {
                    if (field == 4) {
                        size_tok = tok;
                    }
                    field++;
                }
                if (field != 8) {
                    continue;
                }

                std::getline(ls, name);
                name.erase(0, name.find_first_not_of(' '));
                size = std::strtoll(size_tok.c_str(), nullptr, 10);

                // symlink: strip the " -> target" part, guess type by extension.
                if (line[0] == 'l') {
                    if (const auto arrow = name.find(" -> "); arrow != std::string::npos) {
                        name.resize(arrow);
                    }
                    is_dir = name.find('.') == std::string::npos;
                }
            } else if (std::isdigit(static_cast<unsigned char>(line[0]))) {
                // dos LIST: "01-16-26  07:39PM  <DIR>  name" / "01-16-26 07:39PM 123456 name"
                std::istringstream ls(line);
                std::string date, time, third;
                if (!(ls >> date >> time >> third)) {
                    continue;
                }

                if (third == "<DIR>") {
                    is_dir = true;
                } else {
                    size = std::strtoll(third.c_str(), nullptr, 10);
                }

                std::getline(ls, name);
                name.erase(0, name.find_first_not_of(' '));
            } else {
                // unknown format (likely an NLST-style plain name).
                name = line;
                is_dir = name.find('.') == std::string::npos;
            }

            if (name.empty() || name == "." || name == "..") {
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;
            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_size = size;
            entry.st.st_nlink = 1;
            state->entries.push_back(entry);
        }

        // remember the listing so lstat can answer for each name in it.
        const auto dir_key = ftp_key(path ? path : "/");
        for (auto it = m_ftp_stat.begin(); it != m_ftp_stat.end();) {
            const auto slash = it->first.find_last_of('/');
            it = ftp_key(it->first.substr(0, slash)) == dir_key ? m_ftp_stat.erase(it) : std::next(it);
        }
        for (const auto& e : state->entries) {
            m_ftp_stat[ftp_key(e.fullpathname)] = e.st;
        }
        m_ftp_listed.insert(dir_key);
    } else if (webdav) {
        size_t pos = 0;
        while ((pos = find_xml_tag(lc, pos, "response")) != std::string::npos) {
            auto resp_end = find_xml_tag(lc, pos + 1, "response", true);
            if (resp_end == std::string::npos) {
                resp_end = data_str.size();
            }
            const auto next_pos = resp_end;

            const auto href_pos = find_xml_tag(lc, pos, "href");
            if (href_pos == std::string::npos || href_pos >= resp_end) {
                pos = next_pos;
                continue;
            }

            const auto href_start = data_str.find('>', href_pos) + 1;
            const auto href_end = data_str.find('<', href_start);
            if (href_end == std::string::npos) {
                break;
            }

            const std::string href = data_str.substr(href_start, href_end - href_start);
            std::string decoded_href = url_decode(href);

            if (decoded_href.ends_with('/')) {
                decoded_href.pop_back();
            }
            const auto last_slash = decoded_href.find_last_of('/');
            const std::string name = (last_slash != std::string::npos) ? decoded_href.substr(last_slash + 1) : decoded_href;

            if (name.empty() || name == "." || name == "..") {
                pos = next_pos;
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;

            bool is_dir = href.ends_with('/');
            const auto rt_pos = find_xml_tag(lc, pos, "resourcetype");
            if (rt_pos != std::string::npos && rt_pos < resp_end) {
                auto rt_end = find_xml_tag(lc, rt_pos, "resourcetype", true);
                if (rt_end == std::string::npos || rt_end > resp_end) {
                    rt_end = resp_end;
                }
                if (lc.find("collection", rt_pos) < rt_end) {
                    is_dir = true;
                }
            }

            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_nlink = 1;

            const auto cl_pos = find_xml_tag(lc, pos, "getcontentlength");
            if (cl_pos != std::string::npos && cl_pos < resp_end) {
                const auto cl_start = data_str.find('>', cl_pos) + 1;
                const auto cl_end = data_str.find('<', cl_start);
                if (cl_end != std::string::npos) {
                    entry.st.st_size = std::strtoll(data_str.substr(cl_start, cl_end - cl_start).c_str(), nullptr, 10);
                }
            }

            std::string current_dir_url = full_url;
            if (current_dir_url.ends_with('/')) current_dir_url.pop_back();
            std::string item_url = build_url(entry.fullpathname, is_dir);
            if (item_url.ends_with('/')) item_url.pop_back();

            if (item_url != current_dir_url) {
                state->entries.push_back(entry);
            }

            pos = next_pos;
        }
    } else {
        // plain http server: parse <a href="..."> links from the index page.
        // extract the path component of the directory url so that absolute
        // links ("/Games/") can be matched against it.
        std::string cur_path = "/";
        if (const auto scheme_end = full_url.find("://"); scheme_end != std::string::npos) {
            if (const auto path_start = full_url.find('/', scheme_end + 3); path_start != std::string::npos) {
                cur_path = full_url.substr(path_start);
            }
        }
        if (!cur_path.ends_with('/')) {
            cur_path += '/';
        }

        size_t pos = 0;
        while ((pos = lc.find("<a", pos)) != std::string::npos) {
            if (pos + 2 >= lc.size() || !std::isspace(static_cast<unsigned char>(lc[pos + 2]))) {
                pos += 2;
                continue;
            }

            const auto tag_pos = pos;
            const auto tag_end = data_str.find('>', tag_pos);
            if (tag_end == std::string::npos) {
                break;
            }
            pos = tag_end;

            const auto href_attr = lc.find("href", tag_pos);
            if (href_attr == std::string::npos || href_attr > tag_end) {
                continue;
            }
            const auto eq = data_str.find('=', href_attr);
            if (eq == std::string::npos || eq > tag_end) {
                continue;
            }
            const auto vstart = data_str.find_first_not_of(" \t\r\n", eq + 1);
            if (vstart == std::string::npos || vstart > tag_end) {
                continue;
            }

            std::string href;
            if (data_str[vstart] == '"' || data_str[vstart] == '\'') {
                const auto vend = data_str.find(data_str[vstart], vstart + 1);
                if (vend == std::string::npos || vend > tag_end) {
                    continue;
                }
                href = data_str.substr(vstart + 1, vend - vstart - 1);
            } else {
                const auto vend = data_str.find_first_of(" \t\r\n>", vstart);
                href = data_str.substr(vstart, vend - vstart);
            }

            // drop sort links ("?C=N;O=D") and fragments.
            if (const auto cut = href.find_first_of("?#"); cut != std::string::npos) {
                href.resize(cut);
            }

            if (href.starts_with("http://") || href.starts_with("https://")) {
                // absolute link: only accept it if it points inside this directory.
                if (!href.starts_with(full_url)) {
                    continue;
                }
                href = href.substr(full_url.size());
            } else if (href.starts_with("//")) {
                continue;
            } else if (const auto colon = href.find(':'); colon != std::string::npos && colon < href.find('/')) {
                // skip other schemes (mailto:, javascript:, ...).
                continue;
            }

            if (href.starts_with('/')) {
                // absolute path: must be inside the current directory.
                if (!href.starts_with(cur_path)) {
                    continue;
                }
                href = href.substr(cur_path.size());
            }

            if (href.empty() || href == ".." || href.starts_with("../") || href == "./") {
                continue;
            }

            const bool is_dir = href.ends_with('/');
            if (is_dir) {
                href.pop_back();
            }

            const std::string name = url_decode(href);
            // only direct children of this directory.
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) {
                continue;
            }

            const auto exists = std::find_if(state->entries.begin(), state->entries.end(), [&](auto& e) {
                return e.name == name;
            }) != state->entries.end();
            if (exists) {
                continue;
            }

            dircache entry{};
            entry.name = name;
            entry.fullpathname = std::string(path) + (std::string(path).ends_with('/') ? "" : "/") + name;
            entry.st.st_mode = is_dir ? (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH) : (S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            entry.st.st_nlink = 1;
            state->entries.push_back(entry);
        }
    }

    return 0;
}

int MountCurlDevice::devoptab_dirnext(void* fd, char *filename, struct stat *filestat) {
    auto* state = static_cast<CurlDirState*>(fd);
    if (state->index >= state->entries.size()) {
        return -1;
    }

    const auto& entry = state->entries[state->index++];
    std::strncpy(filename, entry.name.c_str(), NAME_MAX - 1);
    filename[NAME_MAX - 1] = '\0';
    std::memcpy(filestat, &entry.st, sizeof(struct stat));
    return 0;
}

int MountCurlDevice::devoptab_dirclose(void* fd) {
    auto* state = static_cast<CurlDirState*>(fd);
    state->~CurlDirState();
    return 0;
}

int MountCurlDevice::devoptab_dirreset(void* fd) {
    auto* state = static_cast<CurlDirState*>(fd);
    state->index = 0;
    return 0;
}


} // namespace sphaira::devoptab::common
