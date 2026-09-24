#include "download.hpp"
#include "download_internal.hpp"
#include "log.hpp"
#include "fs.hpp"
#include <sstream>
#include <algorithm>
#include <vector>

namespace sphaira::curl {
auto ListWebdav(const std::string& url, const std::string& user, const std::string& pass, const std::string& folder, const std::string& bearer, const std::string& pub_key, const std::string& priv_key, u16 port) -> std::vector<std::string> {
    std::vector<std::string> files;

    if (url.starts_with("file://") || url.starts_with("sdmc:/") || url.find("://") == std::string::npos) {
        std::string local_path_str;
        if (url.starts_with("file://")) {
            local_path_str = url.substr(7);
        } else if (url.starts_with("sdmc:/")) {
            local_path_str = url.substr(5);
        } else {
            local_path_str = url;
        }

        if (!folder.empty()) {
            if (!local_path_str.ends_with("/")) {
                local_path_str += "/";
            }
            local_path_str += folder;
        }

        fs::FsNativeSd local_fs;
        std::vector<std::string> local_files;
        fs::Dir dir;
        if (R_SUCCEEDED(local_fs.OpenDirectory(local_path_str, FsDirOpenMode_ReadFiles, &dir))) {
            std::vector<FsDirectoryEntry> entries;
            if (R_SUCCEEDED(dir.ReadAll(entries))) {
                for (const auto& entry : entries) {
                    std::string entry_name = entry.name;
                    if (entry_name.ends_with(".zip")) {
                        local_files.push_back(entry_name);
                    }
                }
            }
            dir.Close();
        }
        return local_files;
    }

    // Construct the full URL
    std::string full_url = url;
    if (!folder.empty()) {
        if (!full_url.ends_with("/")) {
            full_url += "/";
        }
        full_url += folder;
    }

    if (url.starts_with("ftp://") || url.starts_with("ftps://")) {
        Api e;
        e.SetOption(Url{full_url});
        e.SetOption(UserPass{user, pass});
        e.SetOption(Port{port});
        e.SetOption(CustomRequest{"NLST"});

        ApiResult result = ToMemory(e);
        if (!result.success) {
            log_write("[CURL] NLST failed for listing FTP: %s\n", full_url.c_str());
            return files;
        }

        std::string list_str(result.data.begin(), result.data.end());
        std::string line;
        std::istringstream stream(list_str);
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.ends_with(".zip")) {
                files.push_back(line);
            }
        }
        return files;
    }

    Api e;
    e.SetOption(Url{full_url});
    e.SetOption(UserPass{user, pass});
    e.SetOption(Bearer{bearer});
    e.SetOption(PubKey{pub_key});
    e.SetOption(PrivKey{priv_key});
    e.SetOption(Port{port});
    e.SetOption(CustomRequest{"PROPFIND"});
    e.SetOption(Header{
        { "Depth", "1" },
    });

    ApiResult result = ToMemory(e);
    if (!result.success) {
        log_write("[CURL] PROPFIND failed for listing: %s\n", full_url.c_str());
        return files;
    }

    std::string xml(result.data.begin(), result.data.end());
    size_t pos = 0;
    while (true) {
        pos = xml.find("href>", pos);
        if (pos == std::string::npos) {
            break;
        }

        size_t start = pos + 5;
        size_t end = xml.find("</", start);
        if (end == std::string::npos) {
            break;
        }

        std::string href = xml.substr(start, end - start);
        pos = end;

        std::string decoded_href = UnescapeString(href);
        if (decoded_href.ends_with(".zip")) {
            size_t slash_pos = decoded_href.find_last_of('/');
            if (slash_pos != std::string::npos) {
                files.push_back(decoded_href.substr(slash_pos + 1));
            } else {
                files.push_back(decoded_href);
            }
        }
    }

    return files;
}


} // namespace sphaira::curl
