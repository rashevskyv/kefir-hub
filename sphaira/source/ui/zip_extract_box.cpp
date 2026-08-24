#include "ui/zip_extract_box.hpp"
#include "ui/layout.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace sphaira::ui {
namespace {

void DrawChevron(NVGcontext* vg, float x, float y, NVGcolor colour) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, x - 8.f, y - 8.f);
    nvgLineTo(vg, x, y);
    nvgLineTo(vg, x - 8.f, y + 8.f);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 3.f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

constexpr float TITLE_X = 70.f;
constexpr float TITLE_Y = 28.f;
constexpr float HINT_Y = 94.f;
constexpr float TREE_TOP = 150.f;
constexpr float TREE_ROW = 46.f;
constexpr float ACTION_ROW = 54.f;
constexpr float LIST_X = 70.f;
constexpr float LIST_W = 1140.f;

} // namespace

ZipExtractBox::ZipExtractBox(std::string title, std::vector<std::string> entry_names, ExtractCb extract, BrowseCb browse)
: m_title{title.empty() ? "Extract Options"_i18n : std::move(title)}
, m_extract{std::move(extract)}
, m_browse{std::move(browse)} {
    std::vector<std::string_view> views;
    views.reserve(entry_names.size());
    for (const auto& n : entry_names) {
        views.emplace_back(n);
    }

    m_nro = zip_extract::FindSingleNro(views);
    m_nodes = zip_extract::BuildZipTree(views);
    m_checked.assign(m_nodes.size(), 1);
    m_named_dest = zip_extract::NewFolderDest(zip_extract::kDownloadsDir, m_title);

    if (m_nro) {
        const auto dest = zip_extract::NroInstallDest(*m_nro);
        const auto slash = dest.find_last_of('/');
        const auto folder = (slash != std::string::npos && slash > 0) ? dest.substr(0, slash) : std::string{zip_extract::kSwitchDir};
        m_hint = "This archive contains one app. The app will be installed to "_i18n + folder + ".";
        m_actions.emplace_back("Install the app to /switch"_i18n);
    } else {
        m_hint = "Tick what to extract. Unpack the files into a folder, or into a new folder named after the archive."_i18n;
    }
    m_actions.emplace_back("Extract files to /downloads"_i18n);
    m_actions.emplace_back("Extract into "_i18n + m_named_dest);
    m_actions.emplace_back("Extract files to..."_i18n);
    m_actions.emplace_back("Extract into new folder..."_i18n);

    m_focus_actions = true;

    const float actions_h = ACTION_ROW * static_cast<float>(m_actions.size());
    m_actions_top = layout::FOOTER_LINE_Y - actions_h;
    m_tree_top = TREE_TOP;
    const float tree_h = std::max(ACTION_ROW, m_actions_top - m_tree_top - 8.f);
    const auto tree_page = std::max<s64>(1, static_cast<s64>(tree_h / TREE_ROW));

    const Vec4 tree_pos{0.f, m_tree_top, SCREEN_WIDTH, tree_h};
    const Vec4 tree_row{LIST_X, m_tree_top, LIST_W, TREE_ROW};
    m_tree_list = std::make_unique<List>(1, tree_page, tree_pos, tree_row);
    m_tree_list->SetWrap(false);
    m_tree_list->SetScrollBarPos(1250.f, m_tree_top + 10.f, tree_h - 20.f);

    const Vec4 action_pos{0.f, m_actions_top, SCREEN_WIDTH, actions_h};
    const Vec4 action_row{LIST_X, m_actions_top, LIST_W, ACTION_ROW};
    m_action_list = std::make_unique<List>(1, static_cast<s64>(m_actions.size()), action_pos, action_row);
    m_action_list->SetWrap(false);

    this->SetActions(
        std::make_pair(Button::A, Action{"Toggle"_i18n, [this](){ OnA(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
        std::make_pair(Button::Y, Action{"All / none"_i18n, [this](){ AllOrNone(); }})
    );
    UpdateAHint();

    SetPos(0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT);
}

void ZipExtractBox::UpdateAHint() {
    SetAction(Button::A, Action{m_focus_actions ? "Select"_i18n : "Toggle"_i18n, [this](){ OnA(); }});
}

void ZipExtractBox::SetFocusActions(bool actions) {
    if (actions && m_actions.empty()) {
        return;
    }
    if (!actions && m_nodes.empty()) {
        return;
    }
    if (m_focus_actions == actions) {
        return;
    }
    m_focus_actions = actions;
    UpdateAHint();
    App::PlaySoundEffect(SoundEffect_Scroll);
}

void ZipExtractBox::SyncDirChecks() {
    if (m_nodes.empty()) {
        return;
    }
    for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i) {
        if (!m_nodes[static_cast<std::size_t>(i)].is_dir) {
            continue;
        }
        const int depth = m_nodes[static_cast<std::size_t>(i)].depth;
        bool any_file = false;
        bool all = true;
        for (std::size_t j = static_cast<std::size_t>(i) + 1; j < m_nodes.size() && m_nodes[j].depth > depth; ++j) {
            if (m_nodes[j].is_dir) {
                continue;
            }
            any_file = true;
            if (!m_checked[j]) {
                all = false;
            }
        }
        if (any_file) {
            m_checked[static_cast<std::size_t>(i)] = all ? 1 : 0;
        }
    }
}

void ZipExtractBox::Toggle(s64 index) {
    if (index < 0 || static_cast<std::size_t>(index) >= m_nodes.size()) {
        return;
    }
    const auto i = static_cast<std::size_t>(index);
    const bool next = !m_checked[i];
    const int depth = m_nodes[i].depth;
    m_checked[i] = next ? 1 : 0;
    for (std::size_t j = i + 1; j < m_nodes.size() && m_nodes[j].depth > depth; ++j) {
        m_checked[j] = next ? 1 : 0;
    }
    SyncDirChecks();
}

void ZipExtractBox::AllOrNone() {
    bool any_off = false;
    for (std::size_t i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i].is_dir && !m_checked[i]) {
            any_off = true;
            break;
        }
    }
    const char next = any_off ? 1 : 0;
    std::fill(m_checked.begin(), m_checked.end(), next);
}

auto ZipExtractBox::SelectedFiles() const -> std::vector<std::string> {
    return zip_extract::SelectedFilePrefixes(m_nodes, m_checked);
}

void ZipExtractBox::OnA() {
    if (m_focus_actions) {
        RunExtractAction(m_action_index);
    } else {
        Toggle(m_tree_index);
    }
}

void ZipExtractBox::RunExtractAction(s64 index) {
    auto i = index;
    if (m_nro) {
        if (i == 0) {
            auto extract = m_extract;
            auto nro = *m_nro;
            SetPop();
            if (extract) {
                extract("/", std::move(nro), {});
            }
            return;
        }
        i--;
    }

    auto files = SelectedFiles();
    if (files.empty()) {
        App::Push<OptionBox>("Select at least one file to extract."_i18n, "OK"_i18n);
        return;
    }

    auto extract = m_extract;
    auto browse = m_browse;
    auto named = m_named_dest;
    SetPop();
    if (i == 0) {
        if (extract) {
            extract(zip_extract::kDownloadsDir.data(), {}, std::move(files));
        }
    } else if (i == 1) {
        if (extract) {
            extract(named.c_str(), {}, std::move(files));
        }
    } else if (i == 2) {
        if (browse) {
            browse(false, std::move(files));
        }
    } else if (i == 3) {
        if (browse) {
            browse(true, std::move(files));
        }
    }
}

auto ZipExtractBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (touch && (touch->is_clicked || touch->is_touching || touch->is_scroll)) {
        if (m_tree_list && touch->in_range(m_tree_list->GetPos()) && m_focus_actions) {
            SetFocusActions(false);
        } else if (m_action_list && touch->in_range(m_action_list->GetPos()) && !m_focus_actions) {
            SetFocusActions(true);
        }
    }

    if (m_focus_actions) {
        if (!m_nodes.empty() && controller->GotDown(Button::UP) && m_action_index == 0) {
            SetFocusActions(false);
            m_tree_index = static_cast<s64>(m_nodes.size() - 1);
            m_tree_list->EnsureVisible(m_tree_index, static_cast<s64>(m_nodes.size()));
            return;
        }
        m_action_list->OnUpdate(controller, touch, m_action_index, m_actions.size(), [this](bool tapped, auto i) {
            m_action_index = i;
            if (tapped) {
                RunExtractAction(i);
            }
        }, this);
        if (m_tree_list) {
            m_tree_list->OnUpdateTouchOnly(touch, static_cast<s64>(m_nodes.size()));
        }
    } else {
        if (!m_nodes.empty() && controller->GotDown(Button::DOWN) && m_tree_index == static_cast<s64>(m_nodes.size() - 1)) {
            SetFocusActions(true);
            m_action_index = 0;
            return;
        }
        m_tree_list->OnUpdate(controller, touch, m_tree_index, m_nodes.size(), [this](bool tapped, auto i) {
            m_tree_index = i;
            if (tapped) {
                Toggle(i);
            }
        }, this);
        if (m_action_list) {
            m_action_list->OnUpdateTouchOnly(touch, static_cast<s64>(m_actions.size()));
        }
    }
}

auto ZipExtractBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));
    gfx::drawText(vg, TITLE_X, TITLE_Y, 24.f, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    gfx::drawRect(vg, 30.f, layout::HEADER_LINE_Y, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawTextBox(vg, TITLE_X, HINT_Y, 18.f, LIST_W, theme->GetColour(ThemeEntryID_TEXT_INFO), m_hint.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);
    gfx::drawRect(vg, 30.f, m_actions_top - 1.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, layout::FOOTER_LINE_Y, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    if (m_tree_list && !m_nodes.empty()) {
        m_tree_list->Draw(vg, theme, m_nodes.size(), [this](auto* vg, auto* theme, auto v, auto i) {
            const auto& [x, y, w, h] = v;
            const auto selected = m_tree_index == i;
            const auto focused = selected && !m_focus_actions;
            if (focused) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else if (selected) {
                gfx::drawRect(vg, x, y, w, h, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 4.f);
            } else if (i + 1 != static_cast<s64>(m_nodes.size())) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }

            const auto mid_y = y + (h / 2.f);
            const auto box_y = y + (h - gfx::CHECKBOX_SIZE) / 2.f;
            gfx::drawCheckbox(vg, theme, x + 12.f, box_y, gfx::CHECKBOX_SIZE, m_checked[static_cast<std::size_t>(i)] != 0);

            const auto indent = 22.f * static_cast<float>(m_nodes[static_cast<std::size_t>(i)].depth);
            const auto text_x = x + 12.f + gfx::CHECKBOX_SIZE + 12.f + indent;
            const auto text_w = w - (text_x - x) - 20.f;
            const auto colour = theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            m_tree_scroll.Draw(vg, focused, text_x, mid_y, text_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, m_nodes[static_cast<std::size_t>(i)].label);
        });
    }

    m_action_list->Draw(vg, theme, m_actions.size(), [this](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto selected = m_action_index == i;
        const auto focused = selected && m_focus_actions;
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (selected) {
            gfx::drawRect(vg, x, y, w, h, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 4.f);
        } else if (i + 1 != static_cast<s64>(m_actions.size())) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const auto mid_y = y + (h / 2.f);
        DrawChevron(vg, x + w - 24.f, mid_y, theme->GetColour(ThemeEntryID_TEXT));
        const auto colour = theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        m_action_scroll.Draw(vg, focused, x + 15.f, mid_y, w - 60.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, m_actions[static_cast<std::size_t>(i)]);
    });

    Widget::Draw(vg, theme);
}

auto ZipExtractBox::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto ZipExtractBox::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

} // namespace sphaira::ui
