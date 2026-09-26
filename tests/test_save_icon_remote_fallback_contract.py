#!/usr/bin/env python3
"""
Sphaira v0.13.889: Save Tile Title ID Remote Icon Fallback & UI Glow Contract
Concise, high-signal regression checks verifying:
1. Version declaration in CMakeLists.txt (0.13.889).
2. save_menu_detail:
   - Local-first icon priority (NXTC / local result checked before cache/network using ImageLoadFromMemory).
   - Strict dimension and boundary validation via ImageLoadIcon for remote fallback paths:
     in-memory session cache decode, disk cache inspection, and download response validation.
   - NanoVG image creation failure check (img > 0 checked before assigning e.image).
   - Separation of GPU texture allocation failure from corrupt cache (valid disk file preserved if nvgCreateImageRGBA fails).
   - Compressed JPEG session cache (std::unordered_map<u64, std::vector<u8>> s_session_icon_cache)
     retaining compact JPEG bytes (~25 KiB) rather than decoded RGBA (256 KiB).
   - Single SD cache inspection per session (s_checked_cache_ids) with removal of unnecessary s_downloaded_ids / second SD read.
   - Single network fetch per ID with early return when !need_cache_read to prevent duplicate downloads.
   - Corrupt local cache recovery: deletes bad file without marking ID missing, allowing remote repair.
   - Download callback decodes and validates JPEG via ImageLoadIcon before caching or marking available.
   - Main loop execution: no unnecessary mutex locking.
   - ToMemoryAsync enqueue and cache write failure handling.
3. save_menu_draw:
   - HbMenu duplicate title suppression: empty name passed to DrawEntry.
   - UI title above glow: DrawHbMenuTitle called after DrawCategoryBorder.
   - Softened and narrowed yellow glow for backup tiles.
4. scrolling_text & grid_menu_base:
   - scrolling_text.cpp global empty guard reverted.
   - grid_menu_base.cpp guards banner title drawing with if (name && *name).
"""

import os
import sys

def check(condition: bool, msg: str) -> None:
    if not condition:
        print(f"FAIL: {msg}")
        sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_file(*parts: str) -> str:
    path = os.path.join(REPO_ROOT, *parts)
    with open(path, "r", encoding="utf-8") as f:
        return f.read()

def test_version_bump() -> None:
    print("[1] Verifying version bump in CMakeLists.txt...")
    cmake_src = read_file("sphaira", "CMakeLists.txt")
    check("set(sphaira_VERSION 0.13.889)" in cmake_src,
          "sphaira/CMakeLists.txt must define sphaira_VERSION 0.13.889")
    print("  -> Version bump PASSED.")

def test_save_menu_detail_contract() -> None:
    print("[2] Verifying save_menu_detail fallback, cache, and lifecycle contracts...")

    detail_hpp = read_file("sphaira", "include", "ui", "menus", "save", "save_menu_detail.hpp")
    check("auto IsValidGameTitleId(u64 id) -> bool;" in detail_hpp,
          "save_menu_detail.hpp must declare IsValidGameTitleId")
    check("auto FormatTitleIdHex(u64 id) -> std::string;" in detail_hpp,
          "save_menu_detail.hpp must declare FormatTitleIdHex")
    check("auto IsValidBoundedJpeg(std::span<const u8> data) -> bool;" in detail_hpp,
          "save_menu_detail.hpp must declare IsValidBoundedJpeg")
    check("auto BuildRemoteIconUrl(u64 app_id) -> std::string;" in detail_hpp,
          "save_menu_detail.hpp must declare BuildRemoteIconUrl")
    check("auto GetTitleIconCachePath(u64 app_id) -> fs::FsPath;" in detail_hpp,
          "save_menu_detail.hpp must declare GetTitleIconCachePath")
    check("ClearIconSessionCache" not in detail_hpp,
          "save_menu_detail.hpp must not declare unused ClearIconSessionCache")

    detail_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_menu_detail.cpp")
    check("ClearIconSessionCache" not in detail_cpp,
          "save_menu_detail.cpp must not define unused ClearIconSessionCache")

    # Order of operations: local/NXTC must be checked before SD cache and before network
    idx_local = detail_cpp.index("result && !result->icon.empty()")
    idx_cache_check = detail_cpp.index("need_cache_read")
    idx_sd_read = detail_cpp.index("sd.FileExists(cache_path)")
    idx_remote = detail_cpp.index("curl::Api().ToMemoryAsync")
    check(idx_local < idx_cache_check <= idx_sd_read < idx_remote,
          "LoadControlImage must inspect local/NXTC first, then SD cache, then remote async")

    # Local/NXTC path remains unchanged using ImageLoadFromMemory
    check("ImageLoadFromMemory(result->icon, ImageFlag_JPEG)" in detail_cpp,
          "Local/NXTC path must retain ImageLoadFromMemory unchanged")

    # Remote fallback paths must use ImageLoadIcon for bounded 256x256 icon validation
    check("const auto image = ImageLoadIcon(it->second);" in detail_cpp,
          "In-memory session cache lookup must decode via ImageLoadIcon")
    check("const auto image = ImageLoadIcon(icon_data);" in detail_cpp,
          "Disk cache inspection must decode via ImageLoadIcon")
    check("const auto decoded = ImageLoadIcon(res.data);" in detail_cpp,
          "Download callback must decode and validate completed JPEG via ImageLoadIcon")

    # Session caching: stores compressed JPEG bytes per Title ID (~25 KiB), not decoded RGBA (256 KiB)
    check("std::unordered_map<u64, std::vector<u8>> s_session_icon_cache;" in detail_cpp,
          "s_session_icon_cache must store compressed JPEG bytes (std::vector<u8>) per Title ID")

    # Checking NanoVG texture creation failure before reporting entry received an icon
    check("if (img > 0) {" in detail_cpp,
          "LoadControlImage must verify img > 0 before assigning e.image and returning true")

    # NanoVG texture creation failure must not delete a valid disk cache file
    disk_cache_block = detail_cpp[detail_cpp.index("sd.FileExists(cache_path)"):detail_cpp.index("sd.DeleteFile(cache_path)")]
    check("return false;" in disk_cache_block and "DeleteFile" not in disk_cache_block,
          "GPU texture allocation failure on valid disk icon must return false without deleting cache file")

    # Removal of s_downloaded_ids and second SD read (s_session_icon_cache used directly on next draw)
    check("s_downloaded_ids" not in detail_cpp,
          "s_downloaded_ids must be removed because completed downloads populate s_session_icon_cache directly")

    # No unnecessary mutexes since OnComplete runs on the main event loop
    check("<mutex>" not in detail_cpp and "s_mutex" not in detail_cpp,
          "save_menu_detail.cpp must not use mutexes since OnComplete runs on main event loop")

    # Single cache inspection per session tracking
    check("s_checked_cache_ids" in detail_cpp,
          "save_menu_detail.cpp must track checked cache IDs to avoid per-frame SD access")
    check("s_missing_session_ids" in detail_cpp,
          "save_menu_detail.cpp must track missing session IDs to suppress redundant SD/network checks")
    check("s_in_flight_ids" in detail_cpp,
          "save_menu_detail.cpp must track in-flight requests")

    # Simplified need_cache_read with early return to prevent duplicate network downloads
    check("const bool need_cache_read = !s_checked_cache_ids.contains(id);" in detail_cpp,
          "need_cache_read must be based on !s_checked_cache_ids.contains(id)")
    check("if (!need_cache_read) {\n        return false;\n    }" in detail_cpp,
          "save_menu_detail.cpp must return false when need_cache_read is false to prevent duplicate downloads")

    # Corrupt local cache recovery: delete bad file without marking missing
    check("sd.DeleteFile(cache_path);" in detail_cpp,
          "save_menu_detail.cpp must delete corrupt local cache file to allow recovery")
    corrupt_block = detail_cpp[detail_cpp.index("sd.DeleteFile(cache_path);"):detail_cpp.index("sd.DeleteFile(cache_path);") + 150]
    check("s_missing_session_ids.insert(id)" not in corrupt_block,
          "Corrupt local cache file must NOT mark the ID as missing; remote download must proceed")

    # Enqueue failure handling:
    check("const bool enqueued = curl::Api().ToMemoryAsync" in detail_cpp,
          "save_menu_detail.cpp must capture return value of ToMemoryAsync")
    check("if (!enqueued)" in detail_cpp and "s_in_flight_ids.erase(id);" in detail_cpp,
          "save_menu_detail.cpp must clear in-flight state and record missing if enqueue fails")

    # Cache write failure handling:
    check("if (R_FAILED(fs_write.write_entire_file(cache_path, res.data)))" in detail_cpp,
          "save_menu_detail.cpp must check for write_entire_file failure")

    # Remote URL endpoint:
    check('return "https://api.nlib.cc/nx/" + FormatTitleIdHex(app_id) + "/icon/256";' in detail_cpp,
          "BuildRemoteIconUrl must construct exact api.nlib.cc endpoint")

    print("  -> save_menu_detail fallback & cache contracts PASSED.")

def test_save_menu_draw_and_scrolling_contract() -> None:
    print("[3] Verifying save_menu_draw & scrolling text contracts...")

    draw_cpp = read_file("sphaira", "source", "ui", "menus", "save", "save_menu_draw.cpp")

    # Suppress duplicate banner title: pass empty string in HbMenu
    check('const char* entry_name = (m_layout.Get() == grid::LayoutType_HbMenu) ? "" : e.GetName();' in draw_cpp,
          "save_menu_draw.cpp must pass empty name to DrawEntry in HbMenu to avoid duplicate title")

    # UI title must be drawn after DrawCategoryBorder (above glow)
    idx_border = draw_cpp.index("DrawCategoryBorder(vg, theme, v, e);")
    idx_title = draw_cpp.index("DrawHbMenuTitle(vg, v, selected, e.GetName());")
    check(idx_border < idx_title,
          "DrawHbMenuTitle must be called after DrawCategoryBorder so title is above the glow")

    # Narrowed and softened glow
    check("col = nvgRGBA(0xF2, 0xC5, 0x22, 160);" in draw_cpp,
          "save_menu_draw.cpp must use softened yellow glow for backup tiles")
    check("const float thickness = (m_layout.Get() == grid::LayoutType_List) ? 2.f : 6.f;" in draw_cpp,
          "save_menu_draw.cpp must use 6.f thickness instead of 12.f for tile glow")

    # scrolling_text.cpp: global empty string guard was reverted in favor of grid_menu_base
    st_cpp = read_file("sphaira", "source", "ui", "scrolling_text.cpp")
    check("text_entry.empty()" not in st_cpp,
          "scrolling_text.cpp must not contain empty text check (reverted in favor of grid_menu_base)")

    # grid_menu_base.cpp: empty/null guard on banner draw
    gmb_cpp = read_file("sphaira", "source", "ui", "menus", "grid_menu_base.cpp")
    check("if (name && *name)" in gmb_cpp,
          "grid_menu_base.cpp must guard HbMenu title draw with non-empty check")

    print("  -> save_menu_draw & scrolling text contracts PASSED.")

def main() -> None:
    print("=== Sphaira v0.13.889: Save Tile Remote Icon Fallback & UI Glow Regression Suite ===")
    test_version_bump()
    test_save_menu_detail_contract()
    test_save_menu_draw_and_scrolling_contract()
    print("ALL REGRESSION CONTRACT CHECKS PASSED.")

if __name__ == "__main__":
    main()
