#include "ui/menus/preview_pane.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "evman.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "image.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "thumb_cache.hpp"
#include "ui/layout.hpp"
#include <algorithm>
#include <cstdint>

namespace sphaira::ui::menu::tools {

namespace {

constexpr float PAD = 20.f;

// Keeps the picture cache (thumbs/<console>/<title>.png; not thumbs/index/) within thumb_cache::CAP_BYTES by
// deleting the oldest files. `keep` is the picture just saved. Runs on the loader thread after a download.
// The common case costs one directory listing per console folder and no stat; the timestamps are read only
// when the cache is over the cap.
void TrimPictureCache(fs::Fs& sd, const std::string& keep, std::stop_token token) {
    const std::string root = paths::DATA_ROOT + "/cache/thumbs";
    std::vector<FsDirectoryEntry> folders;
    {
        fs::Dir top;
        if (R_FAILED(sd.OpenDirectory(fs::FsPath{root}, FsDirOpenMode_ReadDirs, &top))) return;
        top.ReadAll(folders);
    }

    std::vector<thumb_cache::Entry> files;
    std::uint64_t total = 0;
    for (const auto& folder : folders) {
        if (token.stop_requested()) return;
        const std::string name{folder.name};
        if (name.empty() || thumb_cache::IsIndexPath(name)) continue;

        std::vector<FsDirectoryEntry> entries;
        fs::Dir dir;
        if (R_FAILED(sd.OpenDirectory(fs::FsPath{root + "/" + name}, FsDirOpenMode_ReadFiles, &dir))) continue;
        dir.ReadAll(entries);
        for (const auto& entry : entries) {
            const auto size = entry.file_size > 0 ? static_cast<std::uint64_t>(entry.file_size) : 0ull;
            files.push_back(thumb_cache::Entry{root + "/" + name + "/" + entry.name, size, 0});
            total += size;
        }
    }
    if (total <= thumb_cache::CAP_BYTES) return;

    for (auto& file : files) {
        if (token.stop_requested()) return;
        FsTimeStampRaw ts{};
        if (R_SUCCEEDED(sd.GetFileTimeStampRaw(fs::FsPath{file.path}, &ts)) && ts.is_valid) {
            file.mtime = ts.modified;
        }
    }
    for (const auto& path : thumb_cache::PlanTrim(std::move(files), thumb_cache::CAP_BYTES, keep)) {
        if (token.stop_requested()) return;
        sd.DeleteFile(fs::FsPath{path});
    }
}

// the cached picture of a key, fetched on a miss; runs on the loader thread (stb decode, as FetchImage does)
auto LoadPicture(const PreviewInfo& info, std::stop_token token) -> ImageResult {
    const auto path = paths::DATA_ROOT + "/cache/thumbs/" + info.key;
    fs::FsNativeSd sd;
    if (sd.FileExists(path)) {
        auto cached = ImageLoadFromFile(path, ImageFlag_None);
        if (!cached.data.empty()) return cached;
    }
    fs::CreateDirectoryRecursively(fs::FsPath{path.substr(0, path.rfind('/'))});
    const auto url = info.resolve ? info.resolve(token) : info.url;
    if (url.empty()) return {};
    const auto result = curl::Api().ToFile(curl::Url{url}, curl::Path{path}, curl::StopToken{token});
    if (!result.success) {
        sd.DeleteFile(path); // a failed download must not stay as a "cached" file
        return {};
    }
    auto picture = ImageLoadFromFile(path, ImageFlag_None);
    TrimPictureCache(sd, path, token);
    return picture;
}

} // namespace

PreviewPane::~PreviewPane() {
    Cancel();
    DropTexture();
}

void PreviewPane::Cancel() {
    m_stop.request_stop();
    m_loader.reset(); // joins; the stopped transfer ends at once
    m_stop = std::stop_source{};
}

void PreviewPane::DropTexture() {
    if (m_texture) nvgDeleteImage(App::GetVg(), m_texture);
    m_texture = 0;
    m_focused = m_full = false;
}

void PreviewPane::Show(PreviewInfo info) {
    if (m_has && info.key == m_info.key && info.title == m_info.title) {
        m_info.lines = std::move(info.lines); // the same row asked again: the newest text
        return;
    }
    Cancel();
    DropTexture();
    m_info = std::move(info);
    m_has = true;
    m_started = m_failed = false;
    m_since = std::chrono::steady_clock::now();
}

void PreviewPane::Clear() {
    Cancel();
    DropTexture();
    m_info = {};
    m_has = false;
}

void PreviewPane::Update() {
    if (!m_has || m_started || m_info.key.empty() || (m_info.url.empty() && !m_info.resolve)) return;
    if (std::chrono::steady_clock::now() - m_since < std::chrono::milliseconds{DWELL_MS}) return;
    m_started = true;

    const auto token = m_stop.get_token();
    m_loader = std::make_unique<utils::Async>([this, token, info = m_info]() {
        auto picture = LoadPicture(info, token);
        evman::push(evman::CallbackEventData{[this, key = info.key, picture = std::move(picture)]() {
            // the cursor may have moved on while the picture was on its way
            if (!m_has || m_info.key != key) return;
            if (picture.data.empty()) {
                m_failed = true;
                return;
            }
            DropTexture();
            m_texture = nvgCreateImageRGBA(App::GetVg(), picture.w, picture.h, 0, picture.data.data());
            m_w = picture.w;
            m_h = picture.h;
        }, token}, false);
        if (info.after && !token.stop_requested()) {
            auto lines = info.after(token, info.lines);
            if (!lines.empty() && !token.stop_requested()) {
                evman::push(evman::CallbackEventData{[this, key = info.key, title = info.title, lines = std::move(lines)]() {
                    // only for the row the cursor is still on
                    if (m_has && m_info.key == key && m_info.title == title) m_info.lines = lines;
                }, token}, false);
            }
        }
    });
}

void PreviewPane::Draw(NVGcontext* vg, Theme* theme, const Vec4& area) {
    if (!m_has) return;
    float y = area.y;
    const float w = area.w - PAD;
    if (m_texture && m_w > 0 && m_h > 0) {
        // the picture keeps its shape inside a box as wide as the pane
        const float scale = std::min(w / m_w, 240.f / m_h);
        const float iw = m_w * scale, ih = m_h * scale;
        const float ix = area.x + (w - iw) / 2.f;
        gfx::drawImage(vg, ix, y, iw, ih, m_texture, 6.f);
        if (m_focused) gfx::drawRectOutline(vg, theme, 4.f, ix, y, iw, ih);
        y += ih + 16.f;
    } else if (!m_info.key.empty()) {
        const std::string state = m_failed ? "No screenshot"_i18n : (m_started ? "Loading..."_i18n : std::string{});
        gfx::drawText(vg, area.x, y + 8.f, 16.f, theme->GetColour(ThemeEntryID_TEXT_INFO), state.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        y += 32.f;
    }
    gfx::drawText(vg, area.x, y, 20.f, theme->GetColour(ThemeEntryID_TEXT), m_info.title.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    y += 32.f;
    for (const auto& line : m_info.lines) {
        gfx::drawText(vg, area.x, y, 16.f, theme->GetColour(ThemeEntryID_TEXT_INFO), line.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        y += 24.f;
    }

    if (m_full && HasPicture()) {
        // the picture alone, as large as the content band lets it be, with its shape kept
        gfx::drawRect(vg, 0.f, layout::CONTENT_TOP, SCREEN_WIDTH, layout::CONTENT_HEIGHT, nvgRGBA(0, 0, 0, 220));
        const float bw = SCREEN_WIDTH - 2.f * layout::SIDE_X;
        const float bh = layout::CONTENT_HEIGHT - 20.f;
        const float scale = std::min(bw / m_w, bh / m_h);
        const float iw = m_w * scale, ih = m_h * scale;
        gfx::drawImage(vg, (SCREEN_WIDTH - iw) / 2.f, layout::CONTENT_TOP + (layout::CONTENT_HEIGHT - ih) / 2.f, iw, ih, m_texture, 4.f);
    }
}

} // namespace sphaira::ui::menu::tools
