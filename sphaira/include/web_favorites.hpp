#pragma once

// The favourite sites of Tools → Web browser: parsing, saving, and tidying an address. No libnx: host-tested.
// File format, one site a line: "<address>\t<title>\n".

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::web {

// the system web applet of firmware 20.0.0 through 22.5.0 asks for a system update and opens no page: do not offer it there
inline auto BrowserWorks(unsigned major, unsigned minor, unsigned micro) -> bool {
    const auto v = major * 10000u + minor * 100u + micro;
    return v < 20u * 10000u || v > 22u * 10000u + 5u * 100u;
}

struct Favourite {
    std::string url;
    std::string title;
};

// "  example.com/page " -> "https://example.com/page"; "" and addresses with spaces or control characters -> ""
inline auto NormalizeUrl(std::string_view in) -> std::string {
    while (!in.empty() && (in.front() == ' ' || in.front() == '\t')) in.remove_prefix(1);
    while (!in.empty() && (in.back() == ' ' || in.back() == '\t' || in.back() == '\r' || in.back() == '\n')) in.remove_suffix(1);
    if (in.empty() || in.find_first_of(" \t\r\n") != std::string_view::npos) {
        return {};
    }
    const bool has_scheme = in.starts_with("http://") || in.starts_with("https://");
    // "https://" alone is the keyboard's starting text: nothing was typed
    if (in == "https://" || in == "http://") {
        return {};
    }
    return has_scheme ? std::string{in} : "https://" + std::string{in};
}

// the host of an address, for a title: "https://www.example.com/a/b?q" -> "www.example.com"
inline auto HostOf(std::string_view url) -> std::string {
    const auto scheme = url.find("://");
    if (scheme != std::string_view::npos) {
        url.remove_prefix(scheme + 3);
    }
    const auto end = url.find_first_of("/?#");
    return std::string{url.substr(0, end)};
}

inline auto Parse(std::string_view text) -> std::vector<Favourite> {
    std::vector<Favourite> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto tab = line.find('\t');
        const auto url = NormalizeUrl(line.substr(0, tab));
        if (url.empty()) {
            continue;
        }
        auto title = tab == std::string_view::npos ? std::string{} : std::string{line.substr(tab + 1)};
        out.push_back({url, title.empty() ? HostOf(url) : title});
    }
    return out;
}

inline auto Format(const std::vector<Favourite>& list) -> std::string {
    std::string out;
    for (const auto& f : list) {
        out += f.url + "\t" + f.title + "\n";
    }
    return out;
}

// adds a site (an address already in the list is not added twice); false when the address is empty or already there
inline auto Add(std::vector<Favourite>& list, std::string_view address, std::string title = {}) -> bool {
    const auto url = NormalizeUrl(address);
    if (url.empty() || std::any_of(list.begin(), list.end(), [&](const Favourite& f) { return f.url == url; })) {
        return false;
    }
    // a tab or a line break in a title would break the file
    std::replace_if(title.begin(), title.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
    list.push_back({url, title.empty() ? HostOf(url) : title});
    return true;
}

inline auto Remove(std::vector<Favourite>& list, std::size_t index) -> bool {
    if (index >= list.size()) {
        return false;
    }
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

} // namespace sphaira::web
