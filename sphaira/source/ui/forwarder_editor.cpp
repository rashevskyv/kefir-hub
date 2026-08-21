#include "ui/forwarder_editor.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "nro.hpp"
#include "swkbd.hpp"
#include "ui/list.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "ui/widget.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>

namespace sphaira::ui::forwarder {
namespace {

enum class Row {
    Title,
    IncludePlatform,
    Author,
    Version,
    ProfileSelection,
    AddressSpace,
    Screenshot,
    VideoCapture,
    SvcDebug,
    Create,
};

using CropCallback = std::function<void(std::vector<u8> icon, std::string source)>;

class CropEditor final : public Widget {
public:
    CropEditor(ImageResult img, std::string source, CropCallback on_apply)
    : m_raw_data{std::move(img.data)}
    , m_image_w{img.w}
    , m_image_h{img.h}
    , m_source{std::move(source)}
    , m_on_apply{std::move(on_apply)} {
        SetActions(
            std::make_pair(Button::A, Action{"Apply"_i18n, [this](){ Apply(); }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){ SetPop(); }})
        );

        if (!m_raw_data.empty() && m_image_w > 0 && m_image_h > 0) {
            m_image = nvgCreateImageRGBA(App::GetVg(), m_image_w, m_image_h, 0, m_raw_data.data());
        }
    }

    ~CropEditor() override {
        if (m_image > 0) {
            nvgDeleteImage(App::GetVg(), m_image);
        }
    }

    auto WantsChrome() const -> bool override { return false; }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);

        if (m_image_w <= 0 || m_image_h <= 0 || !m_image) {
            return;
        }

        const Vec4 crop_viewport{CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE};
        m_viewport.Update(controller, touch, m_image_w, m_image_h, crop_viewport, gfx::ImageFit::Cover);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);

        if (!m_image || m_image_w <= 0 || m_image_h <= 0) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO), "Failed to load image"_i18n.c_str());
            Widget::Draw(vg, theme);
            return;
        }

        const Vec4 crop_viewport{CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE};
        const auto img_rect = m_viewport.GetImageRect(m_image_w, m_image_h, crop_viewport, gfx::ImageFit::Cover);
        gfx::drawImage(vg, img_rect.x, img_rect.y, img_rect.w, img_rect.h, m_image, 0.f);

        const auto overlay_colour = nvgRGBA(0, 0, 0, 175);
        gfx::drawRect(vg, 0.f, 0.f, SCREEN_WIDTH, CROP_Y, overlay_colour);
        gfx::drawRect(vg, 0.f, CROP_Y + CROP_SIZE, SCREEN_WIDTH, SCREEN_HEIGHT - (CROP_Y + CROP_SIZE), overlay_colour);
        gfx::drawRect(vg, 0.f, CROP_Y, CROP_X, CROP_SIZE, overlay_colour);
        gfx::drawRect(vg, CROP_X + CROP_SIZE, CROP_Y, SCREEN_WIDTH - (CROP_X + CROP_SIZE), CROP_SIZE, overlay_colour);

        nvgBeginPath(vg);
        nvgRect(vg, CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE);
        nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        nvgStrokeWidth(vg, 2.5f);
        nvgStroke(vg);

        gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        gfx::drawRect(vg, 30.f, 646.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        m_scroll_title.Draw(vg, true, 70.f, 55.f, 400.f, 26.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT), "Crop Icon"_i18n);

        float source_bounds[4]{};
        nvgFontSize(vg, 18.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgTextBounds(vg, 0.f, 0.f, m_source.c_str(), nullptr, source_bounds);
        const auto source_width = std::min(520.f, std::max(0.f, source_bounds[2] - source_bounds[0]));
        m_scroll_source.Draw(vg, true, 1210.f - source_width, 55.f, source_width, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), m_source);

        gfx::drawText(vg, 70.f, 675.f, 18.f, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "\uE0E6 + \uE0EB/\uE0EC Zoom   \uE0EB/\uE0EC/\uE0ED/\uE0EE Move", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        Widget::Draw(vg, theme);
    }

private:
    static constexpr float CROP_SIZE = 512.f;
    static constexpr float CROP_X = (SCREEN_WIDTH - CROP_SIZE) / 2.f;
    static constexpr float CROP_Y = (SCREEN_HEIGHT - CROP_SIZE) / 2.f;

    void Apply() {
        if (m_raw_data.empty() || m_image_w <= 0 || m_image_h <= 0) {
            App::Notify("Failed to crop image"_i18n);
            SetPop();
            return;
        }

        const Vec4 crop_viewport{CROP_X, CROP_Y, CROP_SIZE, CROP_SIZE};
        const auto img_rect = m_viewport.GetImageRect(m_image_w, m_image_h, crop_viewport, gfx::ImageFit::Cover);
        if (img_rect.w <= 0.f || img_rect.h <= 0.f) {
            App::Notify("Failed to crop image"_i18n);
            SetPop();
            return;
        }

        const float scale = img_rect.w / static_cast<float>(m_image_w);
        const int sx = std::clamp(static_cast<int>(std::round((crop_viewport.x - img_rect.x) / scale)), 0, m_image_w - 1);
        const int sy = std::clamp(static_cast<int>(std::round((crop_viewport.y - img_rect.y) / scale)), 0, m_image_h - 1);
        const int s_dim = std::clamp(static_cast<int>(std::round(crop_viewport.w / scale)), 1, std::min(m_image_w - sx, m_image_h - sy));

        auto cropped = ImageCrop(m_raw_data, m_image_w, m_image_h, sx, sy, s_dim, s_dim);
        if (cropped.data.empty()) {
            App::Notify("Failed to crop image"_i18n);
            SetPop();
            return;
        }

        auto resized = cropped.w == 256 && cropped.h == 256 ? std::move(cropped) : ImageResize(cropped.data, cropped.w, cropped.h, 256, 256);
        if (resized.data.empty() || resized.w != 256 || resized.h != 256) {
            App::Notify("Failed to resize icon"_i18n);
            SetPop();
            return;
        }

        auto jpg = ImageConvertToJpg(resized.data, 256, 256);
        if (jpg.data.empty()) {
            App::Notify("Failed to encode icon"_i18n);
            SetPop();
            return;
        }

        if (m_on_apply) {
            m_on_apply(std::move(jpg.data), std::move(m_source));
        }
        SetPop();
    }

    std::vector<u8> m_raw_data;
    int m_image_w{};
    int m_image_h{};
    int m_image{};
    std::string m_source;
    CropCallback m_on_apply;
    gfx::ImageViewport m_viewport{};
    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_source{};
};

class Editor final : public Widget {
public:
    explicit Editor(Config config)
    : m_values{std::move(config.values)}
    , m_icon_source{config.icon_source.empty() ? "Default"_i18n : std::move(config.icon_source)}
    , m_steam_query{std::move(config.steam_query)}
    , m_screen_title{config.screen_title.empty() ? "Forwarder Creation"_i18n : std::move(config.screen_title)}
    , m_title_label{config.title_label.empty() ? "App Title"_i18n : std::move(config.title_label)}
    , m_submit_label{config.submit_label.empty() ? "Create Forwarder"_i18n : std::move(config.submit_label)}
    , m_show_author{config.show_author}
    , m_show_version{config.show_version}
    , m_show_platform_title{config.show_platform_title}
    , m_show_forwarder_options{config.show_forwarder_options}
    , m_on_create{std::move(config.on_create)} {
        SetActions(
            std::make_pair(Button::A, Action{"Select"_i18n, [this](){ Activate(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
        );

        m_rows.emplace_back(Row::Title);
        if (m_show_platform_title) {
            m_rows.emplace_back(Row::IncludePlatform);
        }
        if (m_show_author) {
            m_rows.emplace_back(Row::Author);
        }
        if (m_show_version) {
            m_rows.emplace_back(Row::Version);
        }
        if (m_show_forwarder_options) {
            m_rows.emplace_back(Row::ProfileSelection);
            m_rows.emplace_back(Row::AddressSpace);
            m_rows.emplace_back(Row::Screenshot);
            m_rows.emplace_back(Row::VideoCapture);
            m_rows.emplace_back(Row::SvcDebug);
        }
        m_rows.emplace_back(Row::Create);

        const Vec4 list_pos{445.f, 105.f, 780.f, 525.f};
        const Vec4 item_pos{465.f, 115.f, 735.f, 62.f};
        m_list = std::make_unique<List>(1, 7, list_pos, item_pos, Vec2{0.f, 7.f});
        m_list->SetPageJump(false);
        m_list->SetScrollBarPos(1225.f, 115.f, 505.f);

        if (m_values.icon.empty()) {
            m_values.icon = ImageGetDefaultIcon();
        } else {
            auto normalized = steamgriddb::NormalizeIcon(m_values.icon);
            if (!normalized.empty()) {
                m_values.icon = std::move(normalized);
            } else {
                m_values.icon = ImageGetDefaultIcon();
            }
        }
        UpdatePreview();
    }

    ~Editor() override {
        *m_alive = false;
        if (m_preview > 0) {
            nvgDeleteImage(App::GetVg(), m_preview);
        }
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);

        if (touch->is_clicked && touch->in_range(m_icon_box)) {
            if (!m_icon_focused) {
                App::PlaySoundEffect(SoundEffect_Focus);
            }
            m_icon_focused = true;
            ChooseIconSource();
            return;
        }

        if (m_icon_focused) {
            if (controller->GotDown(Button::RIGHT)) {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_icon_focused = false;
            }
            m_list->OnUpdate(nullptr, touch, m_index, m_rows.size(), [this](bool touched, s64 index){
                m_icon_focused = false;
                if (touched && m_index == index) {
                    FireAction(Button::A);
                } else {
                    App::PlaySoundEffect(SoundEffect_Focus);
                    m_index = index;
                }
            });
            return;
        }

        if (controller->GotDown(Button::LEFT)) {
            App::PlaySoundEffect(SoundEffect_Focus);
            m_icon_focused = true;
            return;
        }

        m_list->OnUpdate(controller, touch, m_index, m_rows.size(), [this](bool touched, s64 index){
            if (touched && m_index == index) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_icon_focused = false;
                m_index = index;
            }
        });
    }

    auto WantsChrome() const -> bool override { return false; }

    void Draw(NVGcontext* vg, Theme* theme) override {
        DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);
        m_scroll_screen_title.Draw(vg, true, 70.f, 55.f, 520.f, 30.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), m_screen_title);

        float app_title_bounds[4]{};
        nvgFontSize(vg, 18.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgTextBounds(vg, 0.f, 0.f, m_values.title.c_str(), nullptr, app_title_bounds);
        const auto app_title_width = std::min(520.f, std::max(0.f, app_title_bounds[2] - app_title_bounds[0]));
        const auto app_title_x = 1210.f - app_title_width;
        m_scroll_app_title.Draw(vg, true, app_title_x, 55.f, app_title_width, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), m_values.title);
        gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        gfx::drawRect(vg, 30.f, 646.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

        const bool icon_error = m_submitted && m_values.icon.empty();
        const Vec4 icon_panel{55.f, 110.f, 350.f, 510.f};
        DrawElement(icon_panel, ThemeEntryID_GRID);
        if (m_icon_focused) {
            gfx::drawRectOutline(vg, theme, 4.f, icon_panel);
        }

        const auto preview = m_preview > 0 ? m_preview : App::GetDefaultImage();
        gfx::drawImage(vg, m_icon_box, preview, 8.f);
        if (icon_error) {
            gfx::drawRect(vg, m_icon_box, theme->GetColour(ThemeEntryID_ERROR), 8.f);
        }

        nvgBeginPath(vg);
        nvgRoundedRect(vg, m_icon_box.x, m_icon_box.y, m_icon_box.w, m_icon_box.h, 8.f);
        const auto border_colour = icon_error
            ? theme->GetColour(ThemeEntryID_ERROR)
            : (m_icon_focused ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : theme->GetColour(ThemeEntryID_LINE));
        nvgStrokeColor(vg, border_colour);
        nvgStrokeWidth(vg, m_icon_focused ? 2.5f : 1.5f);
        nvgStroke(vg);

        const auto icon_label_id = icon_error
            ? ThemeEntryID_ERROR
            : (m_icon_focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        gfx::drawText(vg, 230.f, 450.f, 22.f, theme->GetColour(icon_label_id), "App Icon"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        float source_bounds[4]{};
        nvgFontSize(vg, 17.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgTextBounds(vg, 0.f, 0.f, m_icon_source.c_str(), nullptr, source_bounds);
        const auto source_width = std::max(0.f, source_bounds[2] - source_bounds[0]);
        constexpr float max_source_w = 310.f;
        const auto draw_w = std::min(max_source_w, source_width);
        const auto draw_x = 230.f - draw_w / 2.f;
        m_scroll_icon_source.Draw(vg, true, draw_x, 485.f, draw_w, 17.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), m_icon_source);

        gfx::drawTextBox(vg, 85.f, 525.f, 16.f, 290.f, theme->GetColour(ThemeEntryID_TEXT_INFO), "Press A or tap the icon to change it"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        m_list->Draw(vg, theme, m_rows.size(), [this](auto* vg, auto* theme, const Vec4& pos, s64 index){
            const auto row = m_rows[index];
            const auto selected = !m_icon_focused && m_index == index;
            DrawElement(pos, ThemeEntryID_GRID);
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, pos);
                gfx::drawRect(vg, pos.x + 4.f, pos.y + 10.f, 3.f, pos.h - 20.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
            }

            const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            if (row == Row::Create) {
                gfx::drawText(vg, pos.x + pos.w / 2.f, pos.y + pos.h / 2.f, 23.f, colour, m_submit_label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                return;
            }

            const auto label = GetRowLabel(row);
            const auto value = GetRowValue(row);
            constexpr float row_padding = 20.f;
            constexpr float column_gap = 20.f;
            const auto label_x = pos.x + row_padding;
            const auto value_right = pos.x + pos.w - row_padding;
            const auto content_width = std::max(0.f, pos.w - row_padding * 2.f);

            float value_bounds[4]{};
            nvgFontSize(vg, 20.f);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgTextBounds(vg, 0.f, 0.f, value.c_str(), nullptr, value_bounds);
            const auto measured_value_width = std::max(0.f, value_bounds[2] - value_bounds[0]);
            const auto value_width = std::clamp(measured_value_width, 72.f, content_width * 0.45f);
            const auto value_x = value_right - value_width;
            const auto label_width = std::max(0.f, value_x - column_gap - label_x);
            const auto value_draw_x = measured_value_width < value_width
                ? value_right - measured_value_width
                : value_x;

            const bool row_error = m_submitted && ((row == Row::Title && m_values.title.empty()) || (row == Row::Author && m_show_author && m_values.author.empty()));
            const auto label_colour = row_error ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO);

            m_scroll_row_label.Draw(vg, selected, label_x, pos.y + pos.h / 2.f, label_width, 19.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, label_colour, label);
            // a row the current state makes unusable is drawn dim, so it reads
            // as unavailable rather than as a setting that simply lost.
            const auto unavailable = row == Row::VideoCapture && !m_values.options.screenshot;
            m_scroll_row_value.Draw(vg, selected, value_draw_x, pos.y + pos.h / 2.f,
                std::max(0.f, value_right - value_draw_x), 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                unavailable ? theme->GetColour(ThemeEntryID_TEXT_INFO) : colour, value);
        });

        Widget::Draw(vg, theme);
    }

private:
    void Activate() {
        if (m_icon_focused) {
            ChooseIconSource();
            return;
        }

        switch (m_rows[m_index]) {
            case Row::Title:
                EditText(m_values.title, "App Title"_i18n, 1, sizeof(NacpLanguageEntry::name) - 1);
                break;
            case Row::IncludePlatform:
                m_values.include_platform = !m_values.include_platform;
                break;
            case Row::Author:
                EditText(m_values.author, "Author"_i18n, 1, sizeof(NacpLanguageEntry::author) - 1);
                break;
            case Row::Version:
                EditText(m_values.version, "Version (optional)"_i18n, 0, sizeof(NacpStruct::display_version) - 1);
                break;
            case Row::ProfileSelection:
                m_values.options.profile_selection = !m_values.options.profile_selection;
                break;
            case Row::AddressSpace:
                m_values.options.address_space = m_values.options.address_space == ForwarderAddressSpace::Bit36
                    ? ForwarderAddressSpace::Bit39 : ForwarderAddressSpace::Bit36;
                break;
            case Row::Screenshot:
                m_values.options.screenshot = !m_values.options.screenshot;
                break;
            case Row::VideoCapture:
                // recording rides on the capture button: no screenshots, no video.
                if (!m_values.options.screenshot) {
                    App::Notify("Enable screenshots first"_i18n);
                    break;
                }
                m_values.options.video_capture = !m_values.options.video_capture;
                break;
            case Row::SvcDebug:
                switch (m_values.options.svc_debug_mode) {
                    case ForwarderSvcDebugMode::Automatic: m_values.options.svc_debug_mode = ForwarderSvcDebugMode::Enabled; break;
                    case ForwarderSvcDebugMode::Enabled: m_values.options.svc_debug_mode = ForwarderSvcDebugMode::Disabled; break;
                    case ForwarderSvcDebugMode::Disabled: m_values.options.svc_debug_mode = ForwarderSvcDebugMode::Automatic; break;
                }
                break;
            case Row::Create:
                Create();
                break;
        }
    }

    void EditText(std::string& value, const std::string& header, s64 min_length, s64 max_length) {
        std::string output;
        if (R_SUCCEEDED(swkbd::ShowText(output, header.c_str(), value.c_str(), min_length, max_length))
            && (min_length == 0 || !output.empty())) {
            value = std::move(output);
        }
    }

    void ChooseIconSource() {
        App::Push<OptionBox>(
            "Choose Icon Source"_i18n,
            "Local File"_i18n,
            "SteamGridDB"_i18n,
            [weak_alive = std::weak_ptr<bool>(m_alive), this](auto index){
                const auto alive = weak_alive.lock();
                if (!alive || !*alive) {
                    return;
                }
                if (!index) {
                    return;
                }
                if (*index == 0) {
                    SelectLocalIcon();
                } else {
                    SearchSteamGridDb();
                }
            },
            m_preview
        );
    }

    void SelectLocalIcon() {
        App::Push<menu::filepicker::Menu>(
            menu::filepicker::Callback{[weak_alive = std::weak_ptr<bool>(m_alive), this](const fs::FsPath& path){
                const auto alive = weak_alive.lock();
                if (!alive || !*alive) {
                    return false;
                }
                const auto string_path = path.toString();
                const auto extension = string_path.find_last_of('.');
                const auto ext_str = extension != std::string::npos ? string_path.substr(extension + 1) : "";
                const auto is_nro = !strcasecmp(ext_str.c_str(), "nro");
                const auto is_jpeg = !strcasecmp(ext_str.c_str(), "jpg") || !strcasecmp(ext_str.c_str(), "jpeg");

                ImageResult raw_img;
                if (is_nro) {
                    const auto nro_icon_data = nro_get_icon(path);
                    if (nro_icon_data.empty()) {
                        App::Notify("The selected file has no icon"_i18n);
                        return false;
                    }
                    raw_img = ImageLoadIcon(nro_icon_data);
                } else {
                    raw_img = ImageLoadFromFile(path, is_jpeg ? ImageFlag_JPEG : ImageFlag_None);
                }

                if (raw_img.data.empty() || raw_img.w <= 0 || raw_img.h <= 0) {
                    App::Notify("The selected icon is not a valid image"_i18n);
                    return false;
                }

                auto source = string_path;
                if (const auto slash = source.find_last_of('/'); slash != std::string::npos) {
                    source.erase(0, slash + 1);
                }

                App::Push<CropEditor>(
                    std::move(raw_img),
                    std::move(source),
                    [weak_alive, this](std::vector<u8> icon, std::string icon_source){
                        const auto alive_inner = weak_alive.lock();
                        if (!alive_inner || !*alive_inner) {
                            return;
                        }
                        SetIcon(std::move(icon), std::move(icon_source));
                    }
                );
                return true;
            }}
        );
    }

    void SearchSteamGridDb() {
        const auto query = m_steam_query.empty() ? m_values.title : m_steam_query;
        steamgriddb::ShowIconPicker(query, [weak_alive = std::weak_ptr<bool>(m_alive), this](auto icon){
            const auto alive = weak_alive.lock();
            if (!alive || !*alive) {
                return;
            }
            SetIcon(std::move(icon), "SteamGridDB");
        });
    }

    void SetIcon(std::vector<u8> icon, std::string source) {
        m_values.icon = std::move(icon);
        m_icon_source = std::move(source);
        UpdatePreview();
    }

    void UpdatePreview() {
        if (m_preview > 0) {
            nvgDeleteImage(App::GetVg(), m_preview);
            m_preview = 0;
        }
        if (!m_values.icon.empty()) {
            m_preview = nvgCreateImageMem(App::GetVg(), 0, m_values.icon.data(), m_values.icon.size());
        }
    }

    void Create() {
        m_submitted = true;
        if (m_values.icon.empty()) {
            m_values.icon = ImageGetDefaultIcon();
            UpdatePreview();
        }
        if (m_values.title.empty() || m_values.icon.empty()
            || (m_show_author && m_values.author.empty())) {
            App::Notify("The required fields and icon must be non-empty"_i18n);
            return;
        }

        if (m_on_create && m_on_create(m_values)) {
            SetPop();
        }
    }

    auto GetRowLabel(Row row) const -> std::string {
        switch (row) {
            case Row::Title: return m_title_label;
            case Row::IncludePlatform: return "Include platform in title"_i18n;
            case Row::Author: return "Author"_i18n;
            case Row::Version: return "Version (optional)"_i18n;
            case Row::ProfileSelection: return "Profile Selection"_i18n;
            case Row::AddressSpace: return "Address Space"_i18n;
            case Row::Screenshot: return "Screenshots"_i18n;
            case Row::VideoCapture: return "Video Capture"_i18n;
            case Row::Create: return {};
            case Row::SvcDebug: return "svcDebug"_i18n;
        }
        return {};
    }

    auto GetRowValue(Row row) const -> std::string {
        switch (row) {
            case Row::Title: return m_values.title;
            case Row::IncludePlatform: return m_values.include_platform ? "Enabled"_i18n : "Disabled"_i18n;
            case Row::Author: return m_values.author;
            case Row::Version: return m_values.version;
            case Row::ProfileSelection: return m_values.options.profile_selection ? "Enabled"_i18n : "Disabled"_i18n;
            case Row::AddressSpace: return m_values.options.address_space == ForwarderAddressSpace::Bit36 ? "36-bit"_i18n : "39-bit"_i18n;
            case Row::Screenshot: return m_values.options.screenshot ? "Enabled"_i18n : "Disabled"_i18n;
            case Row::VideoCapture:
                if (!m_values.options.screenshot) {
                    return "Off (needs screenshots)"_i18n;
                }
                return m_values.options.video_capture ? "Enabled"_i18n : "Disabled"_i18n;
            case Row::Create: return {};
            case Row::SvcDebug:
                switch (m_values.options.svc_debug_mode) {
                    case ForwarderSvcDebugMode::Automatic: return "Automatic"_i18n;
                    case ForwarderSvcDebugMode::Enabled: return "Enabled"_i18n;
                    case ForwarderSvcDebugMode::Disabled: return "Disabled"_i18n;
                }
                break;
        }
        return {};
    }

private:
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    Values m_values;
    std::string m_icon_source;
    const std::string m_steam_query;
    const std::string m_screen_title;
    const std::string m_title_label;
    const std::string m_submit_label;
    const bool m_show_author;
    const bool m_show_version;
    const bool m_show_platform_title;
    const bool m_show_forwarder_options;
    const CreateCallback m_on_create;
    std::vector<Row> m_rows;
    std::unique_ptr<List> m_list;
    const Vec4 m_icon_box{80.f, 135.f, 300.f, 300.f};
    int m_preview{};
    s64 m_index{};
    bool m_icon_focused{true};
    bool m_submitted{false};
    ScrollingText m_scroll_screen_title{};
    ScrollingText m_scroll_app_title{};
    ScrollingText m_scroll_icon_source{};
    ScrollingText m_scroll_row_label{};
    ScrollingText m_scroll_row_value{};
};

} // namespace

void Show(Config config) {
    App::Push<Editor>(std::move(config));
}

} // namespace sphaira::ui::forwarder
