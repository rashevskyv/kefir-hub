#include "web_screenshots.hpp"
#include "web_http.hpp"
#include "web_pages.hpp"
#include "title_info.hpp"
#include "app.hpp"
#include "fs.hpp"
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace sphaira {

using namespace webpages;
using namespace web::detail;

namespace {

struct ScreenshotEntry {
    std::string path;
    std::string filename;
    std::string timestamp; // YYYY-MM-DD HH:MM:SS
    std::string game_name;
    std::string title_id; // 016X, empty when unknown
    s64 file_size{};
    std::string raw_timestamp; // YYYYMMDDHHMMSS00 (for sorting)
    bool is_video{};
};

// screenshots are stored in the album of the *booted* nand, which on emummc is the
// redirected nintendo folder from emummc.ini, eg /emuMMC/SD00/Nintendo/Album.
auto GetAlbumRoot() -> const std::string& {
    static const std::string root = [] {
        const auto nintendo = App::GetEmummcNintendoPath();
        return nintendo.empty() ? std::string{"/Nintendo/Album"} : nintendo + "/Album";
    }();

    return root;
}

// the hex after the '-' in an album filename is an obfuscated id, *not* the title id.
// caps:a is what maps a file back to the application that took it; its datetime is
// documented as being exactly the "YYYYMMDDHHMMSSII" part of the filename.
auto GetAlbumTitleIds() -> const std::unordered_map<std::string, u64>& {
    static const auto map = [] {
        std::unordered_map<std::string, u64> out;
        if (R_FAILED(capsaInitialize())) {
            return out;
        }
        ON_SCOPE_EXIT(capsaExit());

        for (const auto storage : {CapsAlbumStorage_Nand, CapsAlbumStorage_Sd}) {
            u64 count{};
            if (R_FAILED(capsaGetAlbumFileCount(storage, &count)) || !count) {
                continue;
            }

            std::vector<CapsAlbumEntry> entries(count);
            u64 total{};
            if (R_FAILED(capsaGetAlbumFileList(storage, &total, entries.data(), count))) {
                continue;
            }

            for (u64 i = 0; i < std::min(total, count); i++) {
                const auto& dt = entries[i].file_id.datetime;
                char key[17]{};
                std::snprintf(key, sizeof(key), "%04u%02u%02u%02u%02u%02u%02u",
                    dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.id);
                out.emplace(key, entries[i].file_id.application_id);
            }
        }

        return out;
    }();

    return map;
}

bool TryParseScreenshotEntry(const FsDirectoryEntry& d_entry, const std::string& full_path, ScreenshotEntry& se) {
    std::string ext = d_entry.name;
    if (const auto dot = ext.find_last_of('.'); dot != std::string::npos) {
        ext = ext.substr(dot + 1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext != "jpg" && ext != "png" && ext != "mp4" && ext != "jpeg") {
        return false;
    }

    se.path = full_path;
    se.filename = d_entry.name;
    se.file_size = d_entry.file_size;
    se.is_video = (ext == "mp4");

    std::string stem = d_entry.name;
    if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }

    bool parsed = false;
    if (stem.size() >= 33 && stem[16] == '-') {
        bool all_digits = true;
        for (int i = 0; i < 16; i++) {
            if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            se.raw_timestamp = stem.substr(0, 16);
            se.timestamp = stem.substr(0, 4) + "-" + stem.substr(4, 2) + "-" + stem.substr(6, 2) + " " +
                           stem.substr(8, 2) + ":" + stem.substr(10, 2) + ":" + stem.substr(12, 2);

            const auto& ids = GetAlbumTitleIds();
            if (const auto it = ids.find(se.raw_timestamp); it != ids.end()) {
                char id_buf[17]{};
                std::snprintf(id_buf, sizeof(id_buf), "%016lX", it->second);
                se.title_id = id_buf;

                auto info = title::Get(it->second);
                if (info && !title::IsPlaceholderName(info->lang.name)) {
                    se.game_name = info->lang.name;
                } else {
                    se.game_name = "Title " + se.title_id;
                    se.title_id.clear();
                }
            } else {
                se.game_name = "Unknown Game";
            }
            parsed = true;
        }
    }

    if (!parsed) {
        se.raw_timestamp = "";
        se.timestamp = "Unknown Date";
        se.game_name = "Unknown Game";
    }

    return true;
}

bool CompareScreenshotEntries(const ScreenshotEntry& lhs, const ScreenshotEntry& rhs) {
    if (lhs.raw_timestamp.empty() && !rhs.raw_timestamp.empty()) return false;
    if (!lhs.raw_timestamp.empty() && rhs.raw_timestamp.empty()) return true;
    return lhs.raw_timestamp > rhs.raw_timestamp;
}

void ScanScreenshots(std::vector<ScreenshotEntry>& out_entries) {
    fs::FsNativeSd fs;
    if (!fs.DirExists(GetAlbumRoot())) {
        return;
    }

    struct StackEntry {
        std::string path;
        int depth;
    };
    std::vector<StackEntry> stack;
    stack.push_back({GetAlbumRoot(), 0});

    while (!stack.empty()) {
        auto entry = stack.back();
        stack.pop_back();

        if (entry.depth > 64) {
            continue;
        }

        fs::Dir dir;
        std::vector<FsDirectoryEntry> entries;
        if (R_SUCCEEDED(fs.OpenDirectory(entry.path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
            dir.ReadAll(entries);
            for (const auto& d_entry : entries) {
                std::string child = entry.path;
                if (child.empty() || child.back() != '/') {
                    child += '/';
                }
                child += d_entry.name;

                if (d_entry.type == FsDirEntryType_Dir) {
                    stack.push_back({child, entry.depth + 1});
                } else {
                    ScreenshotEntry se;
                    if (TryParseScreenshotEntry(d_entry, child, se)) {
                        out_entries.push_back(se);
                    }
                }
            }
        }
    }

    std::sort(out_entries.begin(), out_entries.end(), CompareScreenshotEntries);
}

bool IsValidAlbumPath(const std::string& path) {
    if (path.empty() || path[0] != '/') {
        return false;
    }
    for (char c : path) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '/') {
            return false;
        }
    }
    return true;
}

void ScanFolders(const std::string& parent_path, std::vector<std::string>& out_folders) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(parent_path, FsDirOpenMode_ReadDirs, &dir))) {
        dir.ReadAll(entries);
        for (const auto& entry : entries) {
            if (entry.type == FsDirEntryType_Dir) {
                bool all_digits = true;
                for (size_t i = 0; entry.name[i] != '\0'; ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(entry.name[i]))) {
                        all_digits = false;
                        break;
                    }
                }
                if (all_digits) {
                    out_folders.push_back(entry.name);
                }
            }
        }
    }
    std::sort(out_folders.begin(), out_folders.end(), std::greater<std::string>());
}

void ScanFolderFiles(const std::string& folder_path, std::vector<ScreenshotEntry>& out_entries) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    std::vector<FsDirectoryEntry> entries;
    if (R_SUCCEEDED(fs.OpenDirectory(folder_path, FsDirOpenMode_ReadFiles, &dir))) {
        dir.ReadAll(entries);
        for (const auto& d_entry : entries) {
            std::string child = folder_path;
            if (child.back() != '/') {
                child += '/';
            }
            child += d_entry.name;

            ScreenshotEntry se;
            if (TryParseScreenshotEntry(d_entry, child, se)) {
                out_entries.push_back(se);
            }
        }
    }

    std::sort(out_entries.begin(), out_entries.end(), CompareScreenshotEntries);
}

void AppendSelectionBar(std::string& body) {
    body += "<div class=\"bar\">";
    body += "<button id=\"select-all-btn\" onclick=\"toggleSelectAll()\"><span class=\"icon\">✓</span> <span class=\"text\">Select All</span></button>";
    body += "<button id=\"delete-selected\" onclick=\"deleteSelected()\" disabled><span class=\"icon\">🗑</span> <span class=\"text\">Delete Selected</span> <span class=\"count\">(0)</span></button>";
    body += "</div>";
}

void AppendScreenshotCard(std::string& body, const ScreenshotEntry& entry) {
    const auto encoded_path = UrlEncode(entry.path);
    const auto escaped_game = HtmlEscape(entry.game_name);
    const auto escaped_time = HtmlEscape(entry.timestamp);
    const auto escaped_filename = HtmlEscape(entry.filename);

    body += "<div class=\"card\">";
    body += "<input type=\"checkbox\" class=\"file-checkbox\" data-path=\"" + encoded_path + "\" onclick=\"event.stopPropagation(); updateSelectCount();\">";
    if (entry.is_video) {
        body += "<video controls preload=\"none\" src=\"/view?path=" + encoded_path + "\"></video>";
    } else {
        body += "<a href=\"/view?path=" + encoded_path + "\"><img loading=\"lazy\" src=\"/view?path=" + encoded_path + "\" alt=\"" + escaped_game + "\"></a>";
    }
    body += "<div class=\"info\">";
    body += "<div class=\"game-name\" title=\"" + escaped_game + "\">" + escaped_game + "</div>";
    if (!entry.title_id.empty()) {
        body += "<div class=\"title-id\">" + entry.title_id + "</div>";
    }
    body += "<div class=\"timestamp\">" + escaped_time + "</div>";

    char size_buf[32]{};
    if (entry.file_size >= 1024 * 1024) {
        std::snprintf(size_buf, sizeof(size_buf), "%.2f MB", static_cast<double>(entry.file_size) / (1024.0 * 1024.0));
    } else {
        std::snprintf(size_buf, sizeof(size_buf), "%.1f KB", static_cast<double>(entry.file_size) / 1024.0);
    }
    body += "<div class=\"size\">" + std::string(size_buf) + " (" + escaped_filename + ")</div>";
    body += "<div class=\"actions\">";
    body += "<button class=\"btn del-btn\" onclick=\"deleteItem(event,'" + encoded_path + "')\">Delete</button>";
    body += "</div></div></div>";
}

} // namespace

auto BuildScreenshotGalleryPage(const std::string& query) -> std::string {
    std::string req_path = GetQueryValue(query, "path");
    
    bool browse_mode = query.find("path=") != std::string::npos;
    if (browse_mode && !req_path.empty() && !IsValidAlbumPath(req_path)) {
        req_path = "/";
    }

    std::string body;
    body.reserve(16384);

    body += "<!doctype html><html><head><meta charset=\"utf-8\">";
    body += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    body += "<title>Kefir Hub Album</title>";
    body += "<style>";
    body += "body{margin:0;font:16px system-ui,-apple-system,Segoe UI,sans-serif;background:#101114;color:#f7f7f7}";
    body += "header{position:sticky;top:0;background:#17191d;padding:16px 18px;border-bottom:1px solid #333;z-index:10}";
    body += "h1{font-size:20px;margin:0 0 6px}.subtitle{color:#b9c2cc;font-size:14px}";
    body += ".crumbs{margin-top:8px}.crumbs a{color:#38bdf8;text-decoration:none}";
    body += ".crumbs a:hover{text-decoration:underline}";
    body += ".tabs{display:flex;gap:4px;padding:0 18px;margin-top:16px;border-bottom:1px solid #333}";
    body += ".tab{padding:8px 16px;text-decoration:none;color:#98a1aa;border:1px solid transparent;border-bottom:none;border-radius:6px 6px 0 0;font-size:14px;font-weight:500;transition:all 0.15s}";
    body += ".tab:hover{color:#fff;background:#17191d}";
    body += ".tab.active{color:#fff;background:#1c1c1c;border-color:#333}";
    body += ".album-crumbs{padding:12px 18px 0;font-size:14px;color:#8a939e}";
    body += ".album-crumbs a{color:#38bdf8;text-decoration:none}";
    body += ".album-crumbs a:hover{text-decoration:underline}";
    body += ".folder-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:16px;padding:18px}";
    body += ".folder-card{display:flex;flex-direction:column;align-items:center;justify-content:center;background:#1c1c1c;border:1px solid #333;border-radius:10px;padding:20px;text-decoration:none;color:#fff;box-shadow:0 4px 6px rgba(0,0,0,0.3);transition:all 0.15s}";
    body += ".folder-card:hover{transform:translateY(-2px);background:#252a32;border-color:#4f535c}";
    body += ".folder-icon{width:48px;height:48px;margin-bottom:12px;color:#ffca28}";
    body += ".folder-name{font-weight:600;font-size:15px}";
    body += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:16px;padding:18px}";
    body += ".card{position:relative;display:flex;flex-direction:column;background:#1c1c1c;border:1px solid #333;border-radius:10px;overflow:hidden;box-shadow:0 4px 6px rgba(0,0,0,0.3);transition:transform 0.15s,border-color 0.15s}";
    body += ".card:hover{transform:translateY(-2px)}";
    body += ".card.selected{border-color:#38bdf8}";
    body += ".bar{display:flex;gap:8px;padding:14px 18px 0;flex-wrap:wrap}";
    body += ".bar button{border:1px solid #4f535c;background:#252a32;color:#fff;border-radius:8px;padding:8px 14px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.15s}";
    body += ".bar button:hover:not(:disabled){background:#303640;border-color:#656b77}";
    body += ".bar button:disabled{opacity:0.45;cursor:default}";
    body += "#delete-selected{border-color:rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171}";
    body += "#delete-selected:hover:not(:disabled){background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}";
    body += ".file-checkbox{-webkit-appearance:none;appearance:none;position:absolute;top:12px;left:12px;width:20px;height:20px;margin:0;z-index:5;background:rgba(10,12,15,0.65);border:2px solid rgba(255,255,255,0.5);border-radius:4px;outline:none;cursor:pointer;transition:all 0.15s}";
    body += ".file-checkbox:hover{border-color:#38bdf8;background:rgba(56,189,248,0.25)}";
    body += ".file-checkbox:checked{background:#38bdf8;border-color:#38bdf8}";
    body += ".file-checkbox:checked::after{content:'';position:absolute;left:6px;top:2px;width:4px;height:9px;border:solid #0f0f12;border-width:0 2px 2px 0;transform:rotate(45deg)}";
    body += "img,video{display:block;width:100%;aspect-ratio:16/9;object-fit:cover;background:#050505;border-bottom:1px solid #2d2d2d}";
    body += ".info{padding:12px;display:flex;flex-direction:column;gap:6px;flex-grow:1}";
    body += ".game-name{font-weight:600;color:#fff;font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
    body += ".title-id{color:#7c8794;font-size:11px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:0.4px}";
    body += ".timestamp{color:#8a939e;font-size:12px}";
    body += ".size{color:#6c757d;font-size:11px}";
    body += ".actions{display:flex;gap:8px;margin-top:auto;padding-top:10px}";
    body += "a.btn,button.btn{flex:1;text-align:center;text-decoration:none;border:1px solid #4f535c;background:#252a32;color:#fff;border-radius:6px;padding:8px 0;font-size:12px;font-weight:500;cursor:pointer;transition:all 0.15s}";
    body += "a.btn:hover,button.btn:hover{background:#303640;border-color:#656b77}";
    body += "button.del-btn{border-color:rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171}";
    body += "button.del-btn:hover{background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}";
    body += ".empty{grid-column:1/-1;text-align:center;padding:50px 20px;color:#98a1aa;font-style:italic}";
    body += ".header-top{display:flex;justify-content:space-between;align-items:center;gap:16px}";
    body += "a.header-link-btn button{border:1px solid rgba(255,255,255,0.15);background:#1e293b;color:#fff;border-radius:8px;padding:8px 16px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s}";
    body += "a.header-link-btn button:hover{background:#334155;border-color:rgba(255,255,255,0.25)}";
    body += "@media (max-width: 600px) {";
    body += "  header{padding:12px 16px}";
    body += "  .header-top{flex-direction:column;align-items:flex-start;gap:6px}";
    body += "  .crumbs{text-align:left;max-width:100%}";
    body += "  a.header-link-btn button{padding:8px 12px}";
    body += "  a.header-link-btn button .text{display:none}";
    body += "  .folder-grid{grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:8px;padding:12px}";
    body += "  .grid{grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:8px;padding:12px}";
    body += "  .grid .info{padding:6px;gap:2px}";
    body += "  .game-name{font-size:11px}";
    body += "  .title-id{font-size:8px;letter-spacing:0}";
    body += "  .timestamp{font-size:9px}";
    body += "  .size{font-size:8px}";
    body += "  .folder-card{padding:10px}";
    body += "  .folder-icon{width:32px;height:32px;margin-bottom:6px}";
    body += "  .folder-name{font-size:11px}";
    body += "  .bar{padding:10px 12px 0;gap:6px}";
    body += "  .bar button{padding:6px 10px;font-size:12px}";
    body += "  .file-checkbox{top:6px;left:6px}";
    body += "}";
    body += "</style></head><body><header><div class=\"header-top\"><h1>Kefir Hub Album</h1><a href=\"/files?path=/\" class=\"header-link-btn\" style=\"text-decoration:none;\"><button><span class=\"icon\">📁</span> <span class=\"text\">File Browser</span></button></a></div></header>";

    body += "<div class=\"tabs\">";
    body += "<a href=\"/album\" class=\"tab" + std::string(!browse_mode ? " active" : "") + "\">All Screenshots</a>";
    body += "<a href=\"/album?path=/\" class=\"tab" + std::string(browse_mode ? " active" : "") + "\">Browse by Date</a>";
    body += "</div>";

    if (!browse_mode) {
        std::vector<ScreenshotEntry> entries;
        ScanScreenshots(entries);

        if (!entries.empty()) {
            AppendSelectionBar(body);
        }

        body += "<main class=\"grid\">";
        if (entries.empty()) {
            body += "<div class=\"empty\">No screenshots or videos found in " + HtmlEscape(GetAlbumRoot()) + "</div>";
        } else {
            for (const auto& entry : entries) {
                AppendScreenshotCard(body, entry);
            }
        }
        body += "</main>";
    } else {
        body += "<div class=\"album-crumbs\"><a href=\"/album?path=/\">Album</a>";
        std::vector<std::string> crumb_parts;
        size_t start_pos = 1;
        while (start_pos < req_path.size()) {
            const auto slash = req_path.find('/', start_pos);
            const auto part = req_path.substr(start_pos, slash == std::string::npos ? std::string::npos : slash - start_pos);
            if (!part.empty()) {
                crumb_parts.push_back(part);
            }
            if (slash == std::string::npos) break;
            start_pos = slash + 1;
        }

        std::string current_accum;
        for (size_t i = 0; i < crumb_parts.size(); i++) {
            current_accum += "/" + crumb_parts[i];
            if (i == crumb_parts.size() - 1) {
                body += " / <span>" + HtmlEscape(crumb_parts[i]) + "</span>";
            } else {
                body += " / <a href=\"/album?path=" + UrlEncode(current_accum) + "\">" + HtmlEscape(crumb_parts[i]) + "</a>";
            }
        }
        body += "</div>";

        int depth = 0;
        if (req_path != "/" && !req_path.empty()) {
            depth = std::count(req_path.begin(), req_path.end(), '/');
        }

        if (depth < 3) {
            std::string parent_disk_path = GetAlbumRoot() + (req_path == "/" ? "" : req_path);
            std::vector<std::string> folders;
            ScanFolders(parent_disk_path, folders);

            body += "<main class=\"folder-grid\">";
            if (folders.empty()) {
                body += "<div class=\"empty\">No folders found</div>";
            } else {
                for (const auto& folder : folders) {
                    std::string child_path = (req_path == "/" ? "" : req_path) + "/" + folder;
                    body += "<a href=\"/album?path=" + UrlEncode(child_path) + "\" class=\"folder-card\">";
                    body += "<div class=\"folder-icon\">";
                    body += "<svg viewBox=\"0 0 24 24\" width=\"100%\" height=\"100%\" fill=\"currentColor\"><path d=\"M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z\"/></svg>";
                    body += "</div>";
                    body += "<div class=\"folder-name\">" + HtmlEscape(folder) + "</div>";
                    body += "</a>";
                }
            }
            body += "</main>";
        } else {
            std::string disk_path = GetAlbumRoot() + req_path;
            std::vector<ScreenshotEntry> entries;
            ScanFolderFiles(disk_path, entries);

            if (!entries.empty()) {
                AppendSelectionBar(body);
            }

            body += "<main class=\"grid\">";
            if (entries.empty()) {
                body += "<div class=\"empty\">No screenshots found for this day</div>";
            } else {
                for (const auto& entry : entries) {
                    AppendScreenshotCard(body, entry);
                }
            }
            body += "</main>";
        }
    }

    body += "<script>";
    body += CONFIRM_MODAL_JS;
    body += "document.addEventListener('keydown',function(e){";
    body += "const m=document.getElementById('confirm-modal');";
    body += "if(m&&m.style.display==='flex') return;";
    body += "if(e.key==='Backspace'){";
    body += "e.preventDefault();";
    body += "const urlParams=new URLSearchParams(window.location.search);";
    body += "const path=urlParams.get('path');";
    body += "if(path&&path!=='/'){";
    body += "const parts=path.split('/').filter(Boolean);";
    body += "parts.pop();";
    body += "const parentPath='/'+parts.join('/');";
    body += "window.location.href=window.location.pathname+'?path='+encodeURIComponent(parentPath);";
    body += "}";
    body += "}";
    body += "});";
    body += "async function deleteItem(e,path){";
    body += "e.preventDefault();e.stopPropagation();";
    body += "if(!await showConfirmDialog('Delete '+decodeURIComponent(path.replace(/\\+/g,'%20')).split('/').pop()+'?')) return;";
    body += "const res=await fetch('/delete?path='+path,{method:'DELETE'});";
    body += "if(res.ok){ location.reload(); }else{ alert('Delete failed: '+await res.text()); }";
    body += "}";

    body += "function updateSelectCount(){";
    body += "const all=document.querySelectorAll('.file-checkbox');";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "for(const cb of all){const card=cb.closest('.card');if(card)card.classList.toggle('selected',cb.checked);}";
    body += "const btn=document.getElementById('delete-selected');";
    body += "if(btn){btn.disabled=checked.length===0;const c=btn.querySelector('.count');if(c)c.textContent='('+checked.length+')';}";
    body += "const selectAllBtn=document.getElementById('select-all-btn');";
    body += "if(selectAllBtn){const txt=selectAllBtn.querySelector('.text');";
    body += "if(txt)txt.textContent=(all.length&&checked.length===all.length)?'Deselect All':'Select All';}";
    body += "}";
    body += "function toggleSelectAll(){";
    body += "const all=document.querySelectorAll('.file-checkbox');";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "const target=checked.length<all.length;";
    body += "for(const cb of all)cb.checked=target;";
    body += "updateSelectCount();";
    body += "}";
    body += "async function deleteSelected(){";
    body += "const checked=Array.from(document.querySelectorAll('.file-checkbox:checked'));";
    body += "if(!checked.length)return;";
    body += "if(!await showConfirmDialog('Delete '+checked.length+' selected screenshots?'))return;";
    body += "const btn=document.getElementById('delete-selected');";
    body += "const label=btn?btn.querySelector('.text'):null;";
    body += "if(btn)btn.disabled=true;";
    body += "let done=0;let failed=0;";
    body += "for(const cb of checked){";
    body += "done++;";
    body += "if(label)label.textContent='Deleting ('+done+'/'+checked.length+')';";
    body += "const res=await fetch('/delete?path='+cb.getAttribute('data-path'),{method:'DELETE'});";
    body += "if(!res.ok)failed++;";
    body += "}";
    body += "if(failed)alert('Failed to delete '+failed+' of '+checked.length+' items');";
    body += "location.reload();";
    body += "}";
    body += "document.addEventListener('keydown',function(e){";
    body += "if(typeof isLightboxOpen==='function'&&isLightboxOpen())return;";
    body += "const m=document.getElementById('confirm-modal');if(m&&m.style.display==='flex')return;";
    body += "if((e.ctrlKey||e.metaKey)&&(e.key==='a'||e.key==='A')){";
    body += "if(document.querySelector('.file-checkbox')){e.preventDefault();toggleSelectAll();}";
    body += "}else if(e.key==='Delete'){";
    body += "if(document.querySelector('.file-checkbox:checked')){e.preventDefault();deleteSelected();}";
    body += "}else if(e.key==='Escape'){";
    body += "const checked=document.querySelectorAll('.file-checkbox:checked');";
    body += "if(checked.length){e.preventDefault();for(const cb of checked)cb.checked=false;updateSelectCount();}";
    body += "}";
    body += "});";
    body += "document.addEventListener('DOMContentLoaded',updateSelectCount);";
    body += "</script>";

    AppendConfirmModal(body);

    AppendLightbox(body);

    body += "</body></html>";
    return body;
}

} // namespace sphaira
