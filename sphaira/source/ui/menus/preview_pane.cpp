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
#include <algorithm>

namespace sphaira::ui::menu::tools {

namespace {

constexpr float PAD = 20.f;

// the cached picture of a key, fetched on a miss; runs on the loader thread (stb decode, as FetchImage does)
auto LoadPicture(const PreviewInfo& info, std::stop_token token) -> ImageResult {
    const auto path = paths::DATA_ROOT + "/cache/thumbs/" + info.key;
    fs::FsNativeSd sd;
    if (sd.FileExists(path)) {
        auto cached = ImageLoadFromFile(path, ImageFlag_None);
        if (!cached.data.empty()) return cached;
    }
    fs::CreateDirectoryRecursively(fs::FsPath{path.substr(0, path.rfind('/'))});
    const auto result = curl::Api().ToFile(curl::Url{info.url}, curl::Path{path}, curl::StopToken{token});
    if (!result.success) {
        sd.DeleteFile(path); // a failed download must not stay as a "cached" file
        return {};
    }
    return ImageLoadFromFile(path, ImageFlag_None);
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
}

void PreviewPane::Show(PreviewInfo info) {
    if (m_has && info.key == m_info.key && info.title == m_info.title) return;
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
    if (!m_has || m_started || m_info.key.empty() || m_info.url.empty()) return;
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
        gfx::drawImage(vg, area.x + (w - iw) / 2.f, y, iw, ih, m_texture, 6.f);
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
}

} // namespace sphaira::ui::menu::tools
