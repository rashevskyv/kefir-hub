#pragma once

// Case-insensitive string and path bits that had grown five separate copies
// across the tree: EndsWithIC (yati.cpp, dbi_menu.cpp), ExtensionEquals
// (file_viewer.cpp, web_http.cpp), IsExtension and IsSamePath (file_picker.cpp,
// filebrowser_assoc.cpp), PathExtension (file_viewer.cpp, web_http.cpp).
//
// Case-insensitivity is not cosmetic here: FAT32 filenames are case-insensitive,
// so "GAME.NSP" and "game.nsp" are the same file and must compare equal.
//
// Nothing here includes switch.h, so it is testable on the host. fs::FsPath
// converts to std::string_view implicitly, so FsPath callers need no change.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <strings.h> // strncasecmp

namespace sphaira::path {

// Whole-string case-insensitive compare. Only reads the bytes it is given, so a
// string_view that is not null-terminated is fine.
inline auto EqualsIC(std::string_view a, std::string_view b) -> bool {
    return a.length() == b.length() && !strncasecmp(a.data(), b.data(), a.length());
}

// True when `s` starts with `prefix`, ignoring case. An empty prefix always
// matches, matching std::string_view::starts_with.
inline auto StartsWithIC(std::string_view s, std::string_view prefix) -> bool {
    if (s.size() < prefix.size()) {
        return false;
    }
    return !strncasecmp(s.data(), prefix.data(), prefix.size());
}

// True when `path` contains a directory component (excluding trailing filename/leaf)
// that case-insensitively equals `target`.
inline auto HasPathDirComponentIC(std::string_view path, std::string_view target) -> bool {
    if (StartsWithIC(path, "sdmc:")) {
        path.remove_prefix(5);
    }
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
        path.remove_suffix(1);
    }
    const auto last_slash = path.find_last_of("/\\");
    if (last_slash == path.npos) {
        return false;
    }
    std::string_view dir_part = path.substr(0, last_slash);

    size_t start = 0;
    while (start < dir_part.size()) {
        while (start < dir_part.size() && (dir_part[start] == '/' || dir_part[start] == '\\')) {
            start++;
        }
        if (start >= dir_part.size()) break;
        const auto next_slash = dir_part.find_first_of("/\\", start);
        const auto comp = (next_slash == dir_part.npos)
            ? dir_part.substr(start)
            : dir_part.substr(start, next_slash - start);
        if (EqualsIC(comp, target)) {
            return true;
        }
        if (next_slash == dir_part.npos) break;
        start = next_slash + 1;
    }
    return false;
}

// True when `s` ends with `suffix`, ignoring case. An empty suffix always
// matches, matching std::string_view::ends_with.
inline auto EndsWithIC(std::string_view s, std::string_view suffix) -> bool {
    if (s.size() < suffix.size()) {
        return false;
    }
    return !strncasecmp(s.data() + s.size() - suffix.size(), suffix.data(), suffix.size());
}

// The bit after the last '.', without the dot. Empty when there is no
// extension, and -- importantly -- when the only dot is in a *directory*
// component ("/a.b/file" has no extension, not "b/file").
inline auto Extension(std::string_view path) -> std::string_view {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == path.npos || (slash != path.npos && dot < slash)) {
        return {};
    }
    return path.substr(dot + 1);
}

// True when `value` case-insensitively equals any entry in `list`.
inline auto IsAnyOfIC(std::string_view value, std::span<const std::string_view> list) -> bool {
    for (const auto e : list) {
        if (EqualsIC(value, e)) {
            return true;
        }
    }
    return false;
}

// A name that is exactly a 16 digit hex title id ("0100000000001000"), as used
// by the folders under /atmosphere/contents. 0 for anything else, so the id is
// also the "is this a title id" answer.
inline auto ParseTitleIdName(std::string_view name) -> std::uint64_t {
    if (name.length() != 16) {
        return 0;
    }

    std::uint64_t id{};
    const auto end = name.data() + name.length();
    const auto r = std::from_chars(name.data(), end, id, 16);
    return r.ec == std::errc{} && r.ptr == end ? id : 0;
}

// Returns true if the archive entry path is safe for extraction:
// - Non-empty relative path (not starting with '/')
// - Does not contain '\\', ':', or control characters (< 0x20, 0x7F)
// - Does not contain '.' or '..' directory traversal components
inline auto IsSafeArchiveEntry(std::string_view path) -> bool {
    if (path.empty() || path.front() == '/') {
        return false;
    }

    for (const char c : path) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || c == ':') {
            return false;
        }
    }

    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto comp = (end == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, end - start);

        if (comp == "." || comp == "..") {
            return false;
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return true;
}

// Normalizes a ZIP entry path for save-import compatibility:
// - Sphaira/DBI backups may record entries with exactly one leading slash (e.g. "/folder/file", "/dir/")
// - Removes at most one leading slash
// - The resulting relative path must pass the unchanged strict IsSafeArchiveEntry check
inline auto NormalizeSaveArchiveEntry(std::string_view path) -> std::optional<std::string_view> {
    if (path.empty()) {
        return std::nullopt;
    }
    const auto norm = path.front() == '/' ? path.substr(1) : path;
    if (!IsSafeArchiveEntry(norm)) {
        return std::nullopt;
    }
    return norm;
}

// Pure predicate validating the exact DBI zero-byte root directory entry ("//"):
// - Valid only in save/DBI compatibility mode
// - Raw entry name must be exactly "//"
// - Uncompressed size must be exactly zero
// - Must possess directory semantics: when external_fa == 0, minizip relies
//   on the entry name ending with '/' for directory semantics; when attributes
//   are set, POSIX (0x4000) or DOS directory flag (0x10) must not indicate a
//   regular file or non-directory.
inline auto IsDbiRootMarkerEntry(std::string_view raw_name, std::uint64_t uncompressed_size, std::uint32_t external_fa) -> bool {
    if (raw_name != "//") {
        return false;
    }
    if (uncompressed_size != 0) {
        return false;
    }
    const std::uint32_t posix_type = (external_fa >> 16) & 0xF000;
    if (posix_type != 0 && posix_type != 0x4000) {
        return false;
    }
    if (posix_type == 0 && (external_fa & 0xFF) != 0 && (external_fa & 0x10) == 0) {
        return false;
    }
    return true;
}

// Normalizes an absolute SD card path:
// - Must start with '/'
// - Rejects '\', ':', control characters (< 0x20, 0x7F)
// - Collapses repeated slashes
// - Trims trailing slashes (except root "/")
// - Rejects '.' and '..' components (allows dotfiles/dotfolders like .config, ..data)
inline auto NormalizeAbsoluteSdPath(std::string_view path) -> std::optional<std::string> {
    if (path.empty() || path.front() != '/') {
        return std::nullopt;
    }

    for (const char c : path) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || c == ':') {
            return std::nullopt;
        }
    }

    std::string normalized;
    normalized.reserve(path.size());

    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') {
            start++;
        }
        if (start >= path.size()) {
            break;
        }

        const auto end = path.find('/', start);
        const auto comp = (end == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, end - start);

        if (comp == "." || comp == "..") {
            return std::nullopt;
        }

        normalized.push_back('/');
        normalized.append(comp.data(), comp.size());

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    if (normalized.empty()) {
        return "/";
    }

    return normalized;
}

// A folder value from another app's config (DBI: "SavesFolder=sdmc:/switch/DBI/saves/")
// as an absolute SD path. nullopt when it is missing, relative, the SD root or not on the SD.
inline auto SdFolderFromConfigValue(std::string_view value) -> std::optional<std::string> {
    if (StartsWithIC(value, "sdmc:")) {
        value.remove_prefix(5);
    }
    auto normalized = NormalizeAbsoluteSdPath(value);
    if (!normalized || *normalized == "/") {
        return std::nullopt;
    }
    return normalized;
}

// The "Account=" value of a DBI/Kefir Hub ".dbi_save_info.ini": the nickname of the user the
// backup was made for, also when that user is not on this console. Empty when absent.
inline auto IniAccountName(std::string_view ini) -> std::string {
    constexpr std::string_view spaces = " \t\r";
    while (!ini.empty()) {
        const auto eol = ini.find('\n');
        auto line = ini.substr(0, eol);
        ini = eol == std::string_view::npos ? std::string_view{} : ini.substr(eol + 1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        auto key = line.substr(0, eq);
        key.remove_prefix(std::min(key.find_first_not_of(spaces), key.size()));
        key = key.substr(0, key.find_last_not_of(spaces) + 1);
        if (!EqualsIC(key, "Account")) {
            continue;
        }
        auto value = line.substr(eq + 1);
        value.remove_prefix(std::min(value.find_first_not_of(spaces), value.size()));
        return std::string{value.substr(0, value.find_last_not_of(spaces) + 1)};
    }
    return {};
}

// The "Space=" value of a DBI/Kefir Hub ".dbi_save_info.ini": maps User, SdUser, System,
// etc. to concrete FsSaveDataSpaceId. Returns std::nullopt when absent or unrecognized.
inline auto IniSpaceId(std::string_view ini) -> std::optional<uint8_t> {
    constexpr std::string_view spaces = " \t\r";
    while (!ini.empty()) {
        const auto eol = ini.find('\n');
        auto line = ini.substr(0, eol);
        ini = eol == std::string_view::npos ? std::string_view{} : ini.substr(eol + 1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        auto key = line.substr(0, eq);
        key.remove_prefix(std::min(key.find_first_not_of(spaces), key.size()));
        key = key.substr(0, key.find_last_not_of(spaces) + 1);
        if (!EqualsIC(key, "Space")) {
            continue;
        }
        auto value = line.substr(eq + 1);
        value.remove_prefix(std::min(value.find_first_not_of(spaces), value.size()));
        const auto val = value.substr(0, value.find_last_not_of(spaces) + 1);
        if (EqualsIC(val, "User")) return 1;          // FsSaveDataSpaceId_User
        if (EqualsIC(val, "SdUser")) return 4;        // FsSaveDataSpaceId_SdUser
        if (EqualsIC(val, "System")) return 0;        // FsSaveDataSpaceId_System
        if (EqualsIC(val, "SdSystem")) return 2;      // FsSaveDataSpaceId_SdSystem
        if (EqualsIC(val, "Temporary")) return 3;     // FsSaveDataSpaceId_Temporary
        if (EqualsIC(val, "ProperSystem")) return 100; // FsSaveDataSpaceId_ProperSystem
        if (EqualsIC(val, "SafeMode")) return 101;    // FsSaveDataSpaceId_SafeMode
        return std::nullopt;
    }
    return std::nullopt;
}

// Returns true if the content type or filename/URL indicates a ZIP archive.
// - Content type contains "zip" (case-insensitive)
// - Filename or URL path ends with ".zip" (case-insensitive, URL query/fragment ignored)
inline auto IsZipAsset(std::string_view content_type, std::string_view filename, std::string_view url = {}) -> bool {
    if (!content_type.empty()) {
        for (std::size_t i = 0; i + 3 <= content_type.size(); ++i) {
            if ((content_type[i] == 'z' || content_type[i] == 'Z') &&
                (content_type[i + 1] == 'i' || content_type[i + 1] == 'I') &&
                (content_type[i + 2] == 'p' || content_type[i + 2] == 'P')) {
                return true;
            }
        }
    }
    if (EndsWithIC(filename, ".zip")) {
        return true;
    }
    if (!url.empty()) {
        const auto q = url.find_first_of("?#");
        const auto url_path = (q != std::string_view::npos) ? url.substr(0, q) : url;
        if (EndsWithIC(url_path, ".zip")) {
            return true;
        }
    }
    return false;
}

// Validates a filename basename (rejects slashes, backslashes, colons, traversal '..' and control chars)
inline auto IsSafeFilename(std::string_view name) -> bool {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return true;
}
} // namespace sphaira::path

#include "path_util_url.hpp"

namespace sphaira::path {

// Returns true if `path` is within `parent` (or equals `parent`), matching on
// directory component boundaries case-insensitively. Trailing slashes are ignored.
inline auto IsSubpathOf(std::string_view path, std::string_view parent) -> bool {
    while (path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }
    while (parent.size() > 1 && parent.back() == '/') {
        parent.remove_suffix(1);
    }

    if (parent.empty() || path.empty()) {
        return false;
    }

    if (parent == "/") {
        return path.front() == '/';
    }

    if (EqualsIC(path, parent)) {
        return true;
    }

    if (path.size() > parent.size() && StartsWithIC(path, parent) && path[parent.size()] == '/') {
        return true;
    }

    return false;
}

// Validates that an extraction destination path is safe:
// - Must not be empty or contain repeated slashes ("//")
// - Must not contain '\\', ':', or control characters (< 0x20, 0x7F)
// - Must not contain '.' or '..' directory traversal components
// - Must be within base_path (or equal to base_path)
inline auto IsSafeDestinationPath(std::string_view dest_path, std::string_view base_path) -> bool {
    if (dest_path.empty() || dest_path.find("//") != std::string_view::npos) {
        return false;
    }
    for (const char c : dest_path) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\' || c == ':') {
            return false;
        }
    }
    std::size_t start = 0;
    while (start < dest_path.size()) {
        const auto end = dest_path.find('/', start);
        const auto comp = (end == std::string_view::npos)
            ? dest_path.substr(start)
            : dest_path.substr(start, end - start);
        if (comp == "." || comp == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return IsSubpathOf(dest_path, base_path);
}

// Validates destination containment for extraction:
// When save_dbi_compat is enabled (save restore), enforces strict containment:
// - Rejects traversal ('..', '.'), repeated slashes ('//'), '\\', ':', and control chars
// - Must remain within base_path (or equal to base_path)
// When save_dbi_compat is false (ordinary extraction), preserves existing behavior so that
// legitimate device-prefixed destinations (e.g. "ums0:/backups/file.txt") are not rejected.
inline auto IsSafeExtractionDestination(std::string_view dest_path, std::string_view base_path, bool save_dbi_compat) -> bool {
    if (!save_dbi_compat) {
        return true;
    }
    return IsSafeDestinationPath(dest_path, base_path);
}

// True if `path` ends with ".nro", case-insensitively.
inline auto IsNroPath(std::string_view path) -> bool {
    return EndsWithIC(path, ".nro");
}

// Checks whether a mutation on `path` affects the homebrew catalog,
// considering the default "/switch" root and any custom search paths.
// If `is_directory` is true, any directory within a search root affects the catalog.
// If `is_directory` is false, only .nro files within a search root affect the catalog.
inline auto PathAffectsHomebrew(std::string_view path, std::span<const std::string> custom_roots = {}, bool is_directory = false) -> bool {
    if (path.empty()) {
        return false;
    }

    const auto check_root = [&](std::string_view root) -> bool {
        if (!IsSubpathOf(path, root)) {
            return false;
        }
        if (is_directory) {
            return true;
        }
        return IsNroPath(path);
    };

    if (check_root("/switch")) {
        return true;
    }

    for (const auto& root : custom_roots) {
        if (check_root(root)) {
            return true;
        }
    }

    return false;
}

// Returns true only if path is an exact direct child of /config/kefir/nand_transfer/
// and its basename starts with "_restore_".
// Rejects empty paths, root, parent, nested paths, backslashes, colons, traversal components.
inline auto IsSafeNandTransferStagingDir(std::string_view path, std::string_view required_prefix) -> bool {
    if (path.empty()) {
        return false;
    }
    for (const char c : path) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || c == '\\') {
            return false;
        }
    }
    std::string_view p = path;
    if (StartsWithIC(p, "sdmc:")) {
        p = p.substr(5);
    }
    constexpr std::string_view kExpectedPrefix = "/config/kefir/nand_transfer/";
    if (!StartsWithIC(p, kExpectedPrefix)) {
        return false;
    }
    const auto child = p.substr(kExpectedPrefix.size());
    if (child.empty() || child.find('/') != std::string_view::npos) {
        return false;
    }
    if (child == "." || child == "..") {
        return false;
    }
    if (!child.starts_with(required_prefix) || child.size() <= required_prefix.size()) {
        return false;
    }
    return true;
}

inline auto IsSafeRestoreStagingDir(std::string_view path) -> bool {
    return IsSafeNandTransferStagingDir(path, "_restore_");
}

inline auto IsSafeBackupStagingDir(std::string_view path) -> bool {
    return IsSafeNandTransferStagingDir(path, "_staging_");
}

} // namespace sphaira::path
