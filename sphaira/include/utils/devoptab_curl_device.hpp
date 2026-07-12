#pragma once

#include "utils/devoptab_common.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include <curl/curl.h>
#include <switch.h>
#include <string>

namespace sphaira::devoptab::common {

struct MountCurlDevice : MountDevice {
    using MountDevice::MountDevice;
    virtual ~MountCurlDevice();

    PushThreadData* CreatePushData(CURL* curl, const std::string& url, size_t offset);
    PullThreadData* CreatePullData(CURL* curl, const std::string& url, bool append = false);

    virtual bool Mount();
    virtual void curl_set_common_options(CURL* curl,  const std::string& url);
    static size_t write_memory_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t write_data_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t read_data_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static std::string html_decode(const std::string_view& str);
    static std::string url_decode(const std::string& str);
    std::string build_url(const std::string& path, bool is_dir);

protected:
    CURL* curl{};
    CURL* transfer_curl{};

private:
    // path extracted from the url.
    std::string m_url_path{};
    CURLU* curlu{};
    CURLSH* m_curl_share{};
    RwLock m_rwlocks[CURL_LOCK_DATA_LAST]{};
    bool m_mounted{};
};

} // namespace sphaira::devoptab::common
