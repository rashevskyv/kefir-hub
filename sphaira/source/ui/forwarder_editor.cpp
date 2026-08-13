#include "ui/forwarder_editor.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
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
#include <cstring>
#include <memory>
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

        auto normalized = steamgriddb::NormalizeIcon(m_values.icon);
        if (!normalized.empty()) {
            m_values.icon = std::move(normalized);
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
        const auto icon_label_id = icon_error
            ? ThemeEntryID_ERROR
            : (m_icon_focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        gfx::drawText(vg, 230.f, 450.f, 22.f, theme->GetColour(icon_label_id), "App Icon"_i18n.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        gfx::drawText(vg, 230.f, 485.f, 17.f, theme->GetColour(ThemeEntryID_TEXT_INFO), m_icon_source.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
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
        const std::vector<std::string> filters{"nro", "png", "jpg", "jpeg"};
        App::Push<menu::filepicker::Menu>(
            menu::filepicker::Callback{[weak_alive = std::weak_ptr<bool>(m_alive), this](const fs::FsPath& path){
                const auto alive = weak_alive.lock();
                if (!alive || !*alive) {
                    return false;
                }
                std::vector<u8> icon;
                const auto string_path = path.toString();
                const auto extension = string_path.find_last_of('.');
                const auto is_nro = extension != std::string::npos && !strcasecmp(string_path.c_str() + extension + 1, "nro");
                if (is_nro) {
                    icon = nro_get_icon(path);
                } else if (R_FAILED(fs::FsStdio().read_entire_file(path, icon))) {
                    App::Notify("Failed to load icon"_i18n);
                    return false;
                }

                auto normalized = steamgriddb::NormalizeIcon(icon);
                if (normalized.empty()) {
                    App::Notify("The selected icon is not a valid image"_i18n);
                    return false;
                }

                auto source = string_path;
                if (const auto slash = source.find_last_of('/'); slash != std::string::npos) {
                    source.erase(0, slash + 1);
                }
                SetIcon(std::move(normalized), std::move(source));
                return true;
            }},
            filters
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
    ScrollingText m_scroll_row_label{};
    ScrollingText m_scroll_row_value{};
};

} // namespace

void Show(Config config) {
    App::Push<Editor>(std::move(config));
}

} // namespace sphaira::ui::forwarder
