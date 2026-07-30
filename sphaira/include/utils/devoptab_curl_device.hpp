#pragma once

#include "utils/devoptab_common.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include <curl/curl.h>
#include <switch.h>
#include <string>

#include <vector>
#include <sys/stat.h>

namespace sphaira::devoptab::common {

struct CurlFileState {
    CURL* curl{};
    PushThreadData* push_data{};
    PullThreadData* pull_data{};
    size_t offset{};
    size_t size{};
    std::string url{};
    bool write_mode{};
};

struct dircache {
    std::string name;
    std::string fullpathname;
    struct stat st;
};

struct CurlDirState {
    std::vector<dircache> entries;
    size_t index{};
};

struct MountCurlDevice : MountDevice {
    using MountDevice::MountDevice;
    virtual ~MountCurlDevice();

    PushThreadData* CreatePushData(CURL* curl, const std::string& url, size_t offset);
    PullThreadData* CreatePullData(CURL* curl, const std::string& url, bool append = false);

    virtual bool Mount();
    virtual void curl_set_common_options(CURL* curl,  const std::string& url);

    // curl_easy_perform that aborts when CancelActiveCurlTransfers() or
    // RequestCurlShutdown() is called. Every synchronous request must go
    // through this: these can otherwise block forever (timeout defaults to
    // 0) with no way for the user to cancel or exit.
    CURLcode curl_perform_cancellable(CURL* curl);
    static size_t write_memory_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static std::string html_decode(const std::string_view& str);
    static std::string url_decode(const std::string& str);
    std::string build_url(const std::string& path, bool is_dir);

    int devoptab_open(void *fileStruct, const char *path, int flags, int mode) override;
    int devoptab_close(void *fd) override;
    ssize_t devoptab_read(void *fd, char *ptr, size_t len) override;
    ssize_t devoptab_write(void *fd, const char *ptr, size_t len) override;
    ssize_t devoptab_seek(void *fd, off_t pos, int dir) override;
    int devoptab_fstat(void *fd, struct stat *st) override;
    int devoptab_diropen(void* fd, const char *path) override;
    int devoptab_dirreset(void* fd) override;
    int devoptab_dirnext(void* fd, char *filename, struct stat *filestat) override;
    int devoptab_dirclose(void* fd) override;
    int devoptab_lstat(const char *path, struct stat *st) override;

    // determines the size of a remote file with a 1-byte ranged GET, for
    // servers that do not implement HEAD (eg dbibackend). Returns -1 on
    // failure. out_is_dir is set when the server redirected to a directory.
    s64 probe_size_via_range(CURL* handle, const std::string& url, bool* out_is_dir = nullptr);
    int devoptab_unlink(const char *path) override;
    int devoptab_rmdir(const char *path) override;
    int devoptab_mkdir(const char *path, int mode) override;
    int devoptab_rename(const char *oldName, const char *newName) override;

protected:
    CURL* curl{};
    CURL* transfer_curl{};
    // serialises use of the two shared easy handles above. diropen/lstat and
    // friends can be called concurrently (ui scan, metadata worker, installer)
    // and a CURL easy handle must never be used from two threads at once.
    Mutex m_handle_mutex{};

private:
    // path extracted from the url.
    std::string m_url_path{};
    CURLU* curlu{};
    CURLSH* m_curl_share{};
    RwLock m_rwlocks[CURL_LOCK_DATA_LAST]{};
    bool m_mounted{};
};

} // namespace sphaira::devoptab::common
