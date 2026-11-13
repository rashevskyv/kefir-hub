#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/resolve_menu.hpp"
#include "ui/menus/dbi/dbi_internal.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "app.hpp"
#include "i18n.hpp"

#include <cstdio>

namespace sphaira::ui::menu::dbi {
namespace {

// patch versions are major << 16; the release names them v2, v4
std::string V(u32 version) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "v%u", version >> 16);
    return buf;
}

std::string RowInfo(const ResolveRow& row) {
    if (row.kind == PendingKind::Reinstall) {
        return "Already installed: "_i18n + V(row.versions.front());
    }
    std::string queue;
    for (const auto v : row.versions) {
        queue += (queue.empty() ? "" : ", ") + V(v);
    }
    const auto console = row.console ? V(*row.console) : "none"_i18n;
    return "On the console: "_i18n + console + "  ·  " + "In the queue: "_i18n + queue;
}

} // namespace

std::vector<std::string> ResolveChoiceLabels(const ResolveRow& row) {
    if (row.kind == PendingKind::Reinstall) {
        return {"Skip"_i18n, "Reinstall"_i18n};
    }
    std::vector<std::string> out{row.console ? "Keep "_i18n + V(*row.console) : "Skip"_i18n};
    for (const auto v : row.versions) {
        out.push_back("Install "_i18n + V(v));
    }
    return out;
}

ResolveMenu::ResolveMenu(std::shared_ptr<ResolveData> data)
: MenuBase{"Skipped during the queue"_i18n, MenuFlag_None}, m_data{std::move(data)} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Change"_i18n, [this](){
            if (m_index >= static_cast<s64>(m_data->rows.size())) return;
            auto& row = m_data->rows[m_index];
            App::Push<PopupList>(row.game, ResolveChoiceLabels(row), [this, i = m_index](std::optional<s64> op) {
                if (op) m_data->rows[i].choice = static_cast<int>(*op);
            }, row.choice);
        }}),
        std::make_pair(Button::X, Action{"Resolve"_i18n, [this](){
            Finish(false);
        }}),
        std::make_pair(Button::B, Action{"Skip all"_i18n, [this](){
            Finish(true);
        }})
    );
    SetTitleSubHeading("The queue did not wait for these questions. Choose for each game, then press X."_i18n);
    m_list = std::make_unique<List>(1, 7, Vec4{75.f, GetY() + 44.f, 1130.f, 575.f}, Vec4{75.f, GetY() + 44.f, 1130.f, 79.f});
}

ResolveMenu::~ResolveMenu() {
    m_data->done = true; // closed some other way: the worker goes on with the choices made
}

void ResolveMenu::Finish(bool skip_all) {
    if (skip_all) {
        for (auto& row : m_data->rows) row.choice = 0;
    }
    m_data->done = true;
    SetPop();
}

void ResolveMenu::Update(Controller* controller, TouchInfo* touch) {
    if (m_data->done) { // the session was cancelled
        SetPop();
        return;
    }
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_data->rows.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            m_index = i;
        }
    }, this);
}

void ResolveMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_data->rows.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& row = m_data->rows[i];
        const auto text_id = m_index == i ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        if (m_index == i) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_data->rows.size() - 1) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }
        gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f - 11.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id), "%s", row.game.c_str());
        gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f + 14.f, 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", RowInfo(row).c_str());
        const auto labels = ResolveChoiceLabels(row);
        const auto choice = std::clamp<int>(row.choice, 0, static_cast<int>(labels.size()) - 1);
        gfx::drawTextArgs(vg, x + w - 20.f, y + h / 2.f, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id), "%s", labels[choice].c_str());
    });
}

void InstallSession::ShowAccountPick(const std::shared_ptr<AccountPick>& pick) {
    int expected = -1;
    if (!pick || !pick->choice.compare_exchange_strong(expected, -2)) return;
    App::Push<PopupList>("Select user to create save for"_i18n, pick->names, [pick](auto op_index) {
        pick->choice = op_index ? static_cast<int>(*op_index) : -3;
    });
}

void InstallSession::ShowPrompts(const std::shared_ptr<PromptData>& prompt, const std::shared_ptr<ResolveData>& resolve) {
    if (resolve && !resolve->shown.exchange(true)) {
        App::Push<ResolveMenu>(resolve);
    }
    if (!prompt) return;
    int expected = -1;
    if (!prompt->choice.compare_exchange_strong(expected, -2)) return;
    std::string msg = prompt->title + "\n\n" + prompt->question;
    // dies with the box (inside its callback), so a box closed some other way is never touched.
    std::shared_ptr<int> alive{new int{}, [prompt](int* p) { delete p; prompt->box = nullptr; }};
    auto box = std::make_unique<OptionBox>(msg, "No"_i18n, "Yes"_i18n, 0, [prompt, alive](std::optional<s64> choice) {
        prompt->choice = choice && *choice == 1 ? 1 : 0;
    });
    prompt->box = box.get();
    App::Push(std::move(box));
}

} // namespace sphaira::ui::menu::dbi

#endif
