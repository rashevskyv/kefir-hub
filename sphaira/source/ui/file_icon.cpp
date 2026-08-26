#include "ui/file_icon.hpp"

#include "app.hpp"
#include "image.hpp"
#include "nro.hpp"
#include "path_util.hpp"

#include <algorithm>
#include <cstring>

namespace sphaira::ui::file_icon {
namespace {

constexpr std::string_view IMAGE_EXT[] = {"png", "jpg", "jpeg", "bmp", "gif"};
constexpr std::string_view AUDIO_EXT[] = {
    "mp3", "ogg", "flac", "wav", "aac", "ac3", "aif", "asf", "bfwav", "bfsar", "bfstm",
};
constexpr std::string_view VIDEO_EXT[] = {
    "mp4", "mkv", "m3u", "m3u8", "hls", "vob", "avi", "dv", "flv", "m2ts",
    "m2v", "mov", "mpeg", "mpg", "mts", "swf", "ts", "wma", "wmv",
};
constexpr std::string_view INSTALL_EXT[] = {"nsp", "xci", "nsz", "xcz"};
constexpr std::string_view ZIP_EXT[] = {"zip"};

auto ExtOf(const char* name) -> std::string {
    if (const auto ext = std::strrchr(name, '.')) {
        return ext + 1;
    }
    return {};
}

} // namespace

auto IsImageExt(std::string_view ext) -> bool {
    return path::IsAnyOfIC(ext, IMAGE_EXT);
}

auto TypeIcon(std::string_view ext) -> ThemeEntryID {
    if (path::IsAnyOfIC(ext, IMAGE_EXT)) {
        return ThemeEntryID_ICON_IMAGE;
    }
    if (path::IsAnyOfIC(ext, AUDIO_EXT)) {
        return ThemeEntryID_ICON_AUDIO;
    }
    if (path::IsAnyOfIC(ext, VIDEO_EXT)) {
        return ThemeEntryID_ICON_VIDEO;
    }
    if (path::IsAnyOfIC(ext, INSTALL_EXT) || path::EqualsIC(ext, "nro")) {
        return ThemeEntryID_ICON_NRO;
    }
    if (path::IsAnyOfIC(ext, ZIP_EXT)) {
        return ThemeEntryID_ICON_ZIP;
    }
    return ThemeEntryID_ICON_FILE;
}

auto ChooseGrid(int n) -> std::pair<int, int> {
    if (n <= 0) {
        return {1, 1};
    }
    n = std::min(n, kMaxTiles);
    if (n == 1) {
        return {1, 1};
    }
    if (n == 2) {
        return {2, 1};
    }
    if (n == 4) {
        return {2, 2};
    }
    if (n <= 3) {
        return {n, 1};
    }
    if (n <= 12) {
        return {3, (n + 2) / 3};
    }
    return {6, (n + 5) / 6};
}

void DrawContain(NVGcontext* vg, const Vec4& dest, int image, float rounded) {
    if (image <= 0 || dest.w <= 0.f || dest.h <= 0.f) {
        return;
    }
    int iw{}, ih{};
    nvgImageSize(vg, image, &iw, &ih);
    if (iw <= 0 || ih <= 0) {
        return;
    }
    const float scale = std::min(dest.w / static_cast<float>(iw), dest.h / static_cast<float>(ih));
    const float dw = static_cast<float>(iw) * scale;
    const float dh = static_cast<float>(ih) * scale;
    gfx::drawImage(vg, dest.x + (dest.w - dw) / 2.f, dest.y + (dest.h - dh) / 2.f, dw, dh, image, rounded);
}

void DrawTypeIcon(NVGcontext* vg, const Vec4& dest, std::string_view ext) {
    const float pad = std::max(2.f, std::min(dest.w, dest.h) * 0.08f);
    DrawElementContain({dest.x + pad, dest.y + pad, dest.w - pad * 2.f, dest.h - pad * 2.f}, TypeIcon(ext));
}

void DrawFileThumb(NVGcontext* vg, Theme* theme, const Vec4& dest, int image, std::string_view ext) {
    gfx::drawRect(vg, dest, theme->GetColour(ThemeEntryID_BACKGROUND), 5.f);
    if (image > 0) {
        DrawContain(vg, dest, image, 4.f);
    } else {
        DrawTypeIcon(vg, dest, ext);
    }
}

void DrawMosaic(NVGcontext* vg, Theme* theme, const Vec4& dest, Mosaic& mosaic) {
    const auto folder = GetThemeContainRect(dest, ThemeEntryID_ICON_FOLDER);
    DrawElement(folder, ThemeEntryID_ICON_FOLDER);

    const int n = static_cast<int>(mosaic.cells.size());
    if (n <= 0 || folder.w <= 0.f || folder.h <= 0.f) {
        return;
    }

    // Inner body of a typical folder silhouette (tab on top, stroke around the pocket).
    const Vec4 pocket{
        folder.x + folder.w * 0.12f,
        folder.y + folder.h * 0.28f,
        folder.w * 0.76f,
        folder.h * 0.60f
    };
    if (pocket.w <= 1.f || pocket.h <= 1.f) {
        return;
    }

    const auto [cols, rows] = ChooseGrid(n);
    const float gap = 1.5f;
    const float cell_w = (pocket.w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float cell_h = (pocket.h - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);

    nvgSave(vg);
    nvgIntersectScissor(vg, pocket.x, pocket.y, pocket.w, pocket.h);
    for (int i = 0; i < n && i < cols * rows; i++) {
        const int c = i % cols;
        const int r = i / cols;
        const Vec4 cell{
            pocket.x + static_cast<float>(c) * (cell_w + gap),
            pocket.y + static_cast<float>(r) * (cell_h + gap),
            cell_w, cell_h
        };
        gfx::drawRect(vg, cell, theme->GetColour(ThemeEntryID_BACKGROUND), 2.f);
        auto& e = mosaic.cells[static_cast<size_t>(i)];
        if (e.ellipsis) {
            gfx::drawText(vg, cell.x + cell.w / 2.f, cell.y + cell.h / 2.f, std::max(10.f, cell.h * 0.45f),
                theme->GetColour(ThemeEntryID_TEXT_INFO), "…", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            continue;
        }
        if (e.image > 0) {
            DrawContain(vg, cell, e.image, 2.f);
        } else {
            DrawTypeIcon(vg, cell, e.ext);
        }
    }
    nvgRestore(vg);
}

void FreeMosaic(Mosaic& mosaic) {
    auto* vg = App::GetVg();
    for (auto& cell : mosaic.cells) {
        if (cell.image > 0 && vg) {
            nvgDeleteImage(vg, cell.image);
        }
        cell.image = 0;
    }
    mosaic.cells.clear();
    mosaic.listed = false;
}

auto ListFolderPreview(fs::Fs* fs, const fs::FsPath& dir) -> Mosaic {
    Mosaic out;
    out.listed = true;
    if (!fs) {
        return out;
    }
    fs::Dir d;
    if (R_FAILED(fs->OpenDirectory(dir, FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &d))) {
        return out;
    }
    std::vector<FsDirectoryEntry> ents;
    if (R_FAILED(d.ReadAll(ents))) {
        return out;
    }

    std::vector<const FsDirectoryEntry*> images;
    std::vector<const FsDirectoryEntry*> others;
    int total_files{};
    for (const auto& e : ents) {
        if (e.type != FsDirEntryType_File || e.name[0] == '.') {
            continue;
        }
        total_files++;
        const auto ext = ExtOf(e.name);
        if (IsImageExt(ext)) {
            images.push_back(&e);
        } else {
            others.push_back(&e);
        }
    }

    std::vector<const FsDirectoryEntry*> ordered;
    ordered.reserve(images.size() + others.size());
    ordered.insert(ordered.end(), images.begin(), images.end());
    ordered.insert(ordered.end(), others.begin(), others.end());

    const bool more = total_files > kMaxTiles;
    const int take = more ? kMaxTiles - 1 : std::min(total_files, kMaxTiles);
    for (int i = 0; i < take && static_cast<size_t>(i) < ordered.size(); i++) {
        Cell cell;
        cell.path = fs::AppendPath(dir, ordered[static_cast<size_t>(i)]->name);
        cell.ext = ExtOf(ordered[static_cast<size_t>(i)]->name);
        out.cells.push_back(std::move(cell));
    }
    if (more) {
        Cell extra;
        extra.ellipsis = true;
        out.cells.push_back(std::move(extra));
    }
    return out;
}

auto TryLoadCell(Cell& cell) -> bool {
    if (cell.ellipsis || cell.image != 0) {
        return false;
    }
    if (path::EqualsIC(cell.ext, "nro")) {
        const auto icon = nro_get_icon(cell.path);
        if (icon.empty()) {
            cell.image = -1;
            return false;
        }
        auto img = ImageLoadFromMemory(icon);
        if (img.data.empty()) {
            cell.image = -1;
            return false;
        }
        cell.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
        return cell.image > 0;
    }
    if (!IsImageExt(cell.ext)) {
        cell.image = -1;
        return false;
    }
    fs::FsStdio stdio;
    FsTimeStampRaw ts{};
    s64 size{};
    if (R_SUCCEEDED(stdio.FileGetSizeAndTimestamp(cell.path, &ts, &size)) && size > 8 * 1024 * 1024) {
        cell.image = -1;
        return false;
    }
    const auto flags = path::EqualsIC(cell.ext, "jpg") || path::EqualsIC(cell.ext, "jpeg")
        ? ImageFlag_JPEG : ImageFlag_None;
    auto img = ImageLoadFromFile(cell.path, flags);
    if (img.data.empty()) {
        cell.image = -1;
        return false;
    }
    constexpr int kMax = 160;
    if (img.w > kMax || img.h > kMax) {
        const float scale = std::min(kMax / static_cast<float>(img.w), kMax / static_cast<float>(img.h));
        const int nw = std::max(1, static_cast<int>(img.w * scale));
        const int nh = std::max(1, static_cast<int>(img.h * scale));
        auto resized = ImageResize(img.data, img.w, img.h, nw, nh);
        if (!resized.data.empty()) {
            img = std::move(resized);
        }
    }
    cell.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
    return cell.image > 0;
}

} // namespace sphaira::ui::file_icon
