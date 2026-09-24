#include "download_internal.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ranges>

namespace sphaira::curl {
void SetCommonCurlOptions(CURL* curl, const Api& e) {
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_USERAGENT, APP_USER_AGENT);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_FAILONERROR, 1L);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_NOPROGRESS, 0L);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_SHARE, g_curl_share);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_BUFFERSIZE, 1024*512);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_UPLOAD_BUFFERSIZE, 1024*512);

    // enable all forms of compression supported by libcurl.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_ACCEPT_ENCODING, "");

    // for smb / ftp, try and use ssl if possible.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_TRY);

    // in most cases, this will use CURLAUTH_BASIC.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY);

    // keep sending credentials when the server redirects, e.g. an
    // http:// source that 301s to https://.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);

    // enable TE is server supports it.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_TRANSFER_ENCODING, 1L);

    // set flags.
    if (e.GetFlags() & Flag_NoBody) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_NOBODY, 1L);
    }

    // set custom request.
    if (!e.GetCustomRequest().empty()) {
        log_write("[CURL] setting custom request: %s\n", e.GetCustomRequest().c_str());
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_CUSTOMREQUEST, e.GetCustomRequest().c_str());
    }

    // set oath2 bearer.
    if (!e.GetBearer().empty()) {
        // CURLOPT_XOAUTH2_BEARER only supplies the token. Explicitly selecting
        // bearer auth makes libcurl send the Authorization header on the first
        // request instead of waiting for an auth challenge.
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BEARER);
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_XOAUTH2_BEARER, e.GetBearer().c_str());
    }

    // set ssh pub/priv key file.
    if (!e.GetPubKey().empty()) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_SSH_PUBLIC_KEYFILE, e.GetPubKey().c_str());
    }
    if (!e.GetPrivKey().empty()) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_SSH_PRIVATE_KEYFILE, e.GetPrivKey().c_str());
    }

    // set auth.
    if (!e.GetUserPass().m_user.empty()) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_USERPWD, e.GetUserPass().m_user.c_str());
    }
    if (!e.GetUserPass().m_pass.empty()) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_PASSWORD, e.GetUserPass().m_pass.c_str());
    }

    // set port, if valid.
    if (e.GetPort()) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_PORT, (long)e.GetPort());
    }

    // progress calls.
    if (e.GetOnProgress()) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_XFERINFODATA, &e);
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallbackFunc2);
    } else {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallbackFunc1);
    }

}
auto DownloadInternal(CURL* curl, const Api& e) -> ApiResult {
    // check if stop has been requested or curl is unavailable before starting download
    if (!g_running || !curl || e.GetToken().stop_requested()) {
        return {};
    }

    App::SetAutoSleepDisabled(true);
    ON_SCOPE_EXIT(App::SetAutoSleepDisabled(false));

    fs::FsPath tmp_buf;
    const bool has_file = !e.GetPath().empty() && e.GetPath() != "";
    const bool has_post = !e.GetFields().empty() && e.GetFields() != "";
    const auto encoded_url = EncodeUrl(e.GetUrl());

    DataStruct chunk;
    Header header_in = e.GetHeader();
    Header header_out;
    DownloadHeaderContext header_context{&header_out, &chunk};
    fs::FsNativeSd fs;

    if (has_file) {
        GetDownloadTempPath(tmp_buf);
        fs.CreateDirectoryRecursivelyWithPath(tmp_buf);

        if (auto rc = fs.CreateFile(tmp_buf, 0, 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
            log_write("failed to create file: %s\n", tmp_buf.s);
            return {};
        }

        if (R_FAILED(fs.OpenFile(tmp_buf, FsOpenMode_Write|FsOpenMode_Append, &chunk.f))) {
            log_write("failed to open file: %s\n", tmp_buf.s);
            return {};
        }

        // only add etag if the dst file still exists.
        if ((e.GetFlags() & Flag_Cache) && fs::FileExists(&fs.m_fs, e.GetPath())) {
            g_cache.get(e.GetPath(), header_in);
        }
    }

    // reserve the first chunk
    chunk.data.reserve(CHUNK_SIZE);

    curl_easy_reset(curl);
    SetCommonCurlOptions(curl, e);

    CURL_EASY_SETOPT_LOG(curl, CURLOPT_URL, encoded_url.c_str());
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_HEADERFUNCTION, download_header_callback);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_HEADERDATA, &header_context);

    if (has_post) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_POSTFIELDS, e.GetFields().c_str());
        log_write("setting post field: %s\n", e.GetFields().c_str());
    }

    struct curl_slist* list = NULL;
    ON_SCOPE_EXIT(if (list) { curl_slist_free_all(list); } );

    for (const auto& [key, value] : header_in.m_map) {
        if (value.empty()) {
            continue;
        }

        // create header key value pair.
        const auto header_str = key + ": " + value;

        // try to append header chunk.
        auto temp = curl_slist_append(list, header_str.c_str());
        if (temp) {
            log_write("adding header: %s\n", header_str.c_str());
            list = temp;
        } else {
            log_write("failed to append header\n");
        }
    }

    if (list) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_HTTPHEADER, list);
    }

    // write calls.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_WRITEFUNCTION, has_file ? WriteFileCallback : WriteMemoryCallback);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_WRITEDATA, &chunk);

    // Retry interrupted HTTP file downloads. Only bytes already persisted in the
    // temporary file are used as the resume point; any in-memory tail is discarded.
    const bool is_http = e.GetUrl().rfind("http://", 0) == 0 || e.GetUrl().rfind("https://", 0) == 0;
    const bool is_get = !has_post && e.GetCustomRequest().empty() && !(e.GetFlags() & Flag_NoBody);
    const bool can_retry_http = has_file && is_http && is_get;
    const bool can_resume = can_retry_http && !(e.GetFlags() & Flag_Cache);
    const auto max_attempts = can_retry_http ? DOWNLOAD_MAX_ATTEMPTS : 1;
    CURLcode res = CURLE_OK;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        header_out.m_map.clear();
        chunk.offset = 0;
        chunk.resume_rejected = false;
        chunk.resume_offset = can_resume ? chunk.file_offset : 0;

        if (!can_resume && chunk.file_offset > 0) {
            if (const auto rc = chunk.f.SetSize(0); R_FAILED(rc)) {
                log_write("[CURL] failed to truncate temporary download before retry: 0x%X\n", rc);
                res = CURLE_WRITE_ERROR;
                break;
            }
            chunk.file_offset = 0;
        }

        CURL_EASY_SETOPT_LOG(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(chunk.resume_offset));
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            break;
        }

        if (chunk.resume_rejected) {
            log_write("[CURL] server rejected resume at %lld; restarting download\n", static_cast<long long>(chunk.resume_offset));
            if (const auto rc = chunk.f.SetSize(0); R_FAILED(rc)) {
                log_write("[CURL] failed to truncate rejected partial download: 0x%X\n", rc);
                res = CURLE_WRITE_ERROR;
                break;
            }
            chunk.file_offset = 0;
        }

        const bool retryable = chunk.resume_rejected || IsRetryableDownloadError(res);
        if (!g_running || e.GetToken().stop_requested() || !retryable || attempt == max_attempts) {
            break;
        }

        log_write("[CURL] download attempt %d failed: %s; retrying\n", attempt, curl_easy_strerror(res));
        svcSleepThread(DOWNLOAD_RETRY_DELAY_NS * attempt);
    }

    bool success = res == CURLE_OK;

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (has_file) {
        ON_SCOPE_EXIT( fs.DeleteFile(tmp_buf) );
        if (res == CURLE_OK && chunk.offset) {
            if (const auto rc = chunk.f.Write(chunk.file_offset, chunk.data.data(), chunk.offset, FsWriteOption_None); R_FAILED(rc)) {
                log_write("[CURL] failed to flush final download chunk: 0x%X\n", rc);
                res = CURLE_WRITE_ERROR;
                success = false;
            }
        }

        chunk.f.Close();

        if (res == CURLE_OK) {
            if (http_code == 304) {
                log_write("cached download: %s\n", e.GetUrl().c_str());
            } else {
                log_write("un-cached download: %s code: %lu\n", e.GetUrl().c_str(), http_code);
                if (e.GetFlags() & Flag_Cache) {
                    g_cache.set(e.GetPath(), header_out);
                }

                // enable to log received headers.
                #if 0
                log_write("\n\nLOGGING HEADER\n");
                    for (auto [a, b] : header_out.m_map) {
                        log_write("\t%s: %s\n", a.c_str(), b.c_str());
                    }
                log_write("\n\n");
                #endif

                fs.DeleteFile(e.GetPath());
                fs.CreateDirectoryRecursivelyWithPath(e.GetPath());
                if (R_FAILED(fs.RenameFile(tmp_buf, e.GetPath()))) {
                    success = false;
                }
            }
        }
        chunk.data.clear();
    } else {
        // empty data if we failed
        if (res != CURLE_OK) {
            chunk.data.clear();
        }
    }

    log_write("Downloaded %s code: %ld %s\n", e.GetUrl().c_str(), http_code, curl_easy_strerror(res));
    return {success, http_code, header_out, chunk.data, e.GetPath()};
}

auto UploadInternal(CURL* curl, const Api& e) -> ApiResult {
    // check if stop has been requested or curl is unavailable before starting upload
    if (!g_running || !curl || e.GetToken().stop_requested()) {
        return {};
    }

    if (e.GetUrl().starts_with("webdav://") || e.GetUrl().starts_with("webdavs://")) {
        if (!WebdavCreateFolder(curl, e)) {
            log_write("[CURL] failed to create webdav folder, aborting\n");
            return {};
        }
    }

    const auto& info = e.GetUploadInfo();
    const auto url = e.GetUrl() + "/" + info.m_name;
    const auto encoded_url = EncodeUrl(url);
    const bool has_file = !e.GetPath().empty() && e.GetPath() != "";

    UploadStruct chunk{};
    DataStruct chunk_out{};
    SeekCustomData seek_data{};
    Header header_in = e.GetHeader();
    Header header_out;
    fs::FsNativeSd fs{};

    if (has_file) {
        if (R_FAILED(fs.OpenFile(e.GetPath(), FsOpenMode_Read, &chunk.f))) {
            log_write("failed to open file: %s\n", e.GetPath().s);
            return {};
        }

        chunk.f.GetSize(&chunk.size);
        log_write("got chunk size: %zd\n", chunk.size);
    } else {
        if (info.m_callback) {
            chunk.size = info.m_size;
            log_write("setting upload size: %zu\n", chunk.size);
        } else {
            chunk.size = info.m_data.size();
            chunk.data = info.m_data;
        }
    }

    if (url.starts_with("file://")) {
        const auto folder_path = fs::AppendPath("/", url.substr(std::strlen("file://")));
        log_write("creating local folder: %s\n", folder_path.s);
        // create the folder as libcurl doesn't seem to manually create it.
        fs.CreateDirectoryRecursivelyWithPath(folder_path);
        // remove the path so that libcurl can upload over it.
        fs.DeleteFile(folder_path);
    }

    // reserve the first chunk
    chunk_out.data.reserve(CHUNK_SIZE);

    curl_easy_reset(curl);
    SetCommonCurlOptions(curl, e);

    CURL_EASY_SETOPT_LOG(curl, CURLOPT_URL, encoded_url.c_str());
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_HEADERFUNCTION, header_callback);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_HEADERDATA, &header_out);

    CURL_EASY_SETOPT_LOG(curl, CURLOPT_UPLOAD, 1L);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)chunk.size);

    // instruct libcurl to create ftp folders if they don't yet exist.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);

    struct curl_slist* list = NULL;
    ON_SCOPE_EXIT(if (list) { curl_slist_free_all(list); } );

    for (const auto& [key, value] : header_in.m_map) {
        if (value.empty()) {
            continue;
        }

        // create header key value pair.
        const auto header_str = key + ": " + value;

        // try to append header chunk.
        auto temp = curl_slist_append(list, header_str.c_str());
        if (temp) {
            log_write("adding header: %s\n", header_str.c_str());
            list = temp;
        } else {
            log_write("failed to append header\n");
        }
    }

    if (list) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_HTTPHEADER, list);
    }

    // set callback for reading more data.
    if (info.m_callback) {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_READFUNCTION, ReadCustomCallback);
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_READDATA, &info);

        if (e.GetOnUploadSeek()) {
            seek_data.cb = e.GetOnUploadSeek();
            seek_data.size = chunk.size;
            CURL_EASY_SETOPT_LOG(curl, CURLOPT_SEEKFUNCTION, SeekCustomCallback);
            CURL_EASY_SETOPT_LOG(curl, CURLOPT_SEEKDATA, &seek_data);
        }
    } else {
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_READFUNCTION, has_file ? ReadFileCallback : ReadMemoryCallback);
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_READDATA, &chunk);

        // allow for seeking upon uploads, may be used for ftp and http.
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_SEEKFUNCTION, SeekCallback);
        CURL_EASY_SETOPT_LOG(curl, CURLOPT_SEEKDATA, &chunk);
    }

    // write calls.
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    CURL_EASY_SETOPT_LOG(curl, CURLOPT_WRITEDATA, &chunk_out);

    // perform upload and cleanup after and report the result.
    const auto res = curl_easy_perform(curl);
    bool success = res == CURLE_OK;

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (has_file) {
        chunk.f.Close();
    }

    log_write("Uploaded %s code: %ld %s\n", url.c_str(), http_code, curl_easy_strerror(res));
    return {success, http_code, header_out, chunk_out.data};
}

auto WebdavCreateFolder(CURL* curl, const Api& e) -> bool {
    // if using webdav, extract the file path and create the directories.
    // https://github.com/WebDAVDevs/webdav-request-samples/blob/master/webdav_curl.md
    if (e.GetUrl().starts_with("webdav://") || e.GetUrl().starts_with("webdavs://")) {
        log_write("[CURL] found webdav url\n");

        const auto info = e.GetUploadInfo();
        if (info.m_name.empty()) {
            return true;
        }

        const auto& file_path = info.m_name;
        log_write("got file path: %s\n", file_path.c_str());

        const auto file_loc = file_path.find_last_of('/');
        if (file_loc == file_path.npos) {
            log_write("failed to find last slash\n");
            return true;
        }

        const auto path_view = file_path.substr(0, file_loc);
        log_write("got folder path: %s\n", path_view.c_str());

        auto e2 = e;
        e2.SetOption(Path{});
        e2.SetOption(Url{e.GetUrl() + "/" + path_view});
        e2.SetOption(Flags{e.GetFlags() | Flag_NoBody});
        e2.SetOption(CustomRequest{"PROPFIND"});
        e2.SetOption(Header{
            { "Depth", "0" },
        });

        // test to see if the directory exists first.
        const auto exist_result = DownloadInternal(curl, e2);
        if (exist_result.success) {
            log_write("[CURL] folder already exist: %s\n", path_view.c_str());
            return true;
        } else {
            log_write("[CURL] folder does NOT exist, manually creating: %s\n", path_view.c_str());
        }

        // make the request to create the folder.
        std::string folder;
        for (const auto dir : std::views::split(path_view, '/')) {
            if (dir.empty()) {
                continue;
            }

            folder += "/" + std::string{dir.data(), dir.size()};
            e2.SetOption(Url{e.GetUrl() + folder});
            e2.SetOption(Header{});
            e2.SetOption(CustomRequest{"MKCOL"});

            const auto result = DownloadInternal(curl, e2);
            if (result.code == 201) {
                log_write("[CURL] created webdav directory\n");
            } else if (result.code == 405) {
                log_write("[CURL] webdav directory already exists: %ld\n", result.code);
            } else {
                log_write("[CURL] failed to create webdav directory: %ld\n", result.code);
                return false;
            }
        }
    } else {
        log_write("[CURL] not a webdav url: %s\n", e.GetUrl().c_str());
    }

    return true;
}


auto ToMemory(const Api& e) -> ApiResult {
    if (!g_running || !e.GetPath().empty()) {
        return {};
    }
    SCOPED_MUTEX(&g_mutex_single);
    if (!g_running || !g_curl_single) {
        return {};
    }
    return DownloadInternal(g_curl_single, e);
}

auto ToFile(const Api& e) -> ApiResult {
    if (!g_running || e.GetPath().empty()) {
        return {};
    }
    SCOPED_MUTEX(&g_mutex_single);
    if (!g_running || !g_curl_single) {
        return {};
    }
    return DownloadInternal(g_curl_single, e);
}

auto FromMemory(const Api& e) -> ApiResult {
    if (!g_running || !e.GetPath().empty()) {
        return {};
    }
    SCOPED_MUTEX(&g_mutex_single);
    if (!g_running || !g_curl_single) {
        return {};
    }
    return UploadInternal(g_curl_single, e);
}

auto FromFile(const Api& e) -> ApiResult {
    if (!g_running || e.GetPath().empty()) {
        return {};
    }
    SCOPED_MUTEX(&g_mutex_single);
    if (!g_running || !g_curl_single) {
        return {};
    }
    return UploadInternal(g_curl_single, e);
}

auto Probe(const Api& api, ProbeType type) -> ApiResult {
    auto request = api;
    request.SetOption(Path{});
    request.SetOption(Flags{request.GetFlags() & ~Flag_NoBody});
    request.SetOption(CustomRequest{});
    request.SetOption(Header{});

    if (type == ProbeType::Webdav) {
        request.SetOption(CustomRequest{"PROPFIND"});
        request.SetOption(Header{{"Depth", "0"}});
    } else if (type == ProbeType::Ftp) {
        request.SetOption(CustomRequest{"NLST"});
    }

    auto result = ToMemory(request);
    if (!result.success) {
        return result;
    }

    if (type == ProbeType::Webdav) {
        std::string response{result.data.begin(), result.data.end()};
        std::transform(response.begin(), response.end(), response.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        result.success = result.code == 207 ||
            (result.code >= 200 && result.code < 300 && response.find("multistatus") != std::string::npos);
    } else if (type == ProbeType::Http) {
        result.success = result.code >= 200 && result.code < 300;
    }

    return result;
}


} // namespace sphaira::curl
