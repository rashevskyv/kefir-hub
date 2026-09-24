#include "ui/menus/gc_menu_internal.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "download.hpp"
#include "dumper.hpp"
#include "image.hpp"
#include "title_info.hpp"
#include "storage_ratio.hpp"
#include "utils/utils.hpp"

#include <cstring>
#include <algorithm>

namespace sphaira::ui::menu::gc {

auto ApplicationEntry::GetSize(const std::vector<GcCollections>& entries) const -> s64 {
    s64 size{};
    for (auto& e : entries) {
        for (auto& collection : e) {
            size += collection.size;
        }
    }
    return size;
}

auto ApplicationEntry::GetSize() const -> s64 {
    s64 size{};
    size += GetSize(application);
    size += GetSize(patch);
    size += GetSize(add_on);
    size += GetSize(data_patch);
    return size;
}

Menu::Menu(u32 flags) : MenuBase{"GameCard"_i18n, flags} {
    this->SetActions(
        std::make_pair(Button::A, Action{"OK"_i18n, [this](){
            if (m_option_index == 2) {
                SetPop();
            } else {
                if (!m_mounted) {
                    return;
                }

                if (m_option_index == 0) {
                    if (!App::GetInstallEnable()) {
                        App::ShowEnableInstallPrompt();
                    } else {
                        log_write("[GC] doing install A\n");
                        App::Push<ui::ProgressBox>(m_icon, "Installing "_i18n, m_entries[m_entry_index].lang_entry.name, [this](auto pbox) -> Result {
                            auto source = std::make_unique<GcSource>(m_entries[m_entry_index], m_fs.get());
                            return yati::InstallFromCollections(pbox, source.get(), source->m_collections, source->m_config);
                        }, [this](Result rc){
                            App::PushErrorBox(rc, "Gc install failed!"_i18n);

                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Gc install success!"_i18n);
                            }
                        });
                    }
                } else {
                    auto options = std::make_unique<Sidebar>("Select content to dump"_i18n, Sidebar::Side::RIGHT);
                    ON_SCOPE_EXIT(App::Push(std::move(options)));

                    const auto add = [&](const std::string& name, u32 flags, const std::string& info){
                        options->Add<SidebarEntryCallback>(name, [this, flags](){
                            DumpGames(flags);
                            m_dirty = true;
                        }, true, info);
                    };

                    add("Dump All"_i18n, DumpFileFlag_All, "Dump the full XCI and all binary data."_i18n);
                    add("Dump All Bins"_i18n, DumpFileFlag_AllBin, "Dump all binary files without the XCI."_i18n);
                    add("Dump XCI"_i18n, DumpFileFlag_XCI, "Dump the full XCI image of the game card."_i18n);
                    add("Dump Card ID Set"_i18n, DumpFileFlag_Set, "Dump the Card ID Set binary file."_i18n);
                    add("Dump Card UID"_i18n, DumpFileFlag_UID, "Dump the Card UID binary file."_i18n);
                    add("Dump Certificate"_i18n, DumpFileFlag_Cert, "Dump the game card Certificate binary file."_i18n);
                    add("Dump Initial Data"_i18n, DumpFileFlag_Initial, "Dump the Initial Data binary file."_i18n);
                }
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Game Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            options->Add<SidebarEntryCallback>("Install options"_i18n, [this](){
                App::DisplayInstallOptions(false);
            }, "Configure installation settings such as skipping content types."_i18n);

            options->Add<SidebarEntryCallback>("Dump options"_i18n, [this](){
                App::DisplayDumpOptions(false);
            }, "Configure dump output settings and file format options."_i18n);
        }})
    );

    const Vec4 v{485, 275, 720, 70};
    const Vec2 pad{0, 125 - v.h};
    m_list = std::make_unique<List>(1, 3, m_pos, v, pad);

    fsOpenDeviceOperator(std::addressof(m_dev_op));
    fsOpenGameCardDetectionEventNotifier(std::addressof(m_event_notifier));
    fsEventNotifierGetEventHandle(std::addressof(m_event_notifier), std::addressof(m_event), true);
    title::Init();
}

Menu::~Menu() {
    title::Exit();
    GcUnmount();
    eventClose(std::addressof(m_event));
    fsEventNotifierClose(std::addressof(m_event_notifier));
    fsDeviceOperatorClose(std::addressof(m_dev_op));
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    // poll for the gamecard first before handling inputs as the gamecard
    // may have been removed, thus pressing A would fail.
    if (m_dirty || R_SUCCEEDED(eventWait(std::addressof(m_event), 0))) {
        GcOnEvent(m_dirty);
        m_dirty = false;
    }

    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_option_index, std::size(g_option_list), [this](bool touch, auto i) {
        if (touch && m_option_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            m_option_index = i;
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    #define STORAGE_BAR_W   325
    #define STORAGE_BAR_H   14

    const auto size_sd_gb = CalculateStorageFreeGb(m_size_total_sd, m_size_free_sd);
    const auto size_nand_gb = CalculateStorageFreeGb(m_size_total_nand, m_size_free_nand);

    const auto nand_used_ratio = CalculateStorageUsedRatio(m_size_total_nand, m_size_free_nand);
    const auto sd_used_ratio = CalculateStorageUsedRatio(m_size_total_sd, m_size_free_sd);

    const float nand_fill_w = std::clamp(static_cast<float>(nand_used_ratio * (STORAGE_BAR_W - 4)), 0.0f, static_cast<float>(STORAGE_BAR_W - 4));
    const float sd_fill_w = std::clamp(static_cast<float>(sd_used_ratio * (STORAGE_BAR_W - 4)), 0.0f, static_cast<float>(STORAGE_BAR_W - 4));

    gfx::drawTextArgs(vg, 490, 135, 23.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "System memory %.1f GB"_i18n.c_str(), size_nand_gb);
    gfx::drawRect(vg, 480, 170, STORAGE_BAR_W, STORAGE_BAR_H, theme->GetColour(ThemeEntryID_TEXT));
    gfx::drawRect(vg, 480 + 1, 170 + 1, STORAGE_BAR_W - 2, STORAGE_BAR_H - 2, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND));
    if (nand_fill_w > 0.0f) {
        gfx::drawRect(vg, 480 + 2, 170 + 2, nand_fill_w, STORAGE_BAR_H - 4, theme->GetColour(ThemeEntryID_TEXT));
    }

    gfx::drawTextArgs(vg, 870, 135, 23.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "microSD card %.1f GB"_i18n.c_str(), size_sd_gb);
    gfx::drawRect(vg, 860, 170, STORAGE_BAR_W, STORAGE_BAR_H, theme->GetColour(ThemeEntryID_TEXT));
    gfx::drawRect(vg, 860 + 1, 170 + 1, STORAGE_BAR_W - 2, STORAGE_BAR_H - 2, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND));
    if (sd_fill_w > 0.0f) {
        gfx::drawRect(vg, 860 + 2, 170 + 2, sd_fill_w, STORAGE_BAR_H - 4, theme->GetColour(ThemeEntryID_TEXT));
    }

    gfx::drawRect(vg, 30, 90, 375, 555, theme->GetColour(ThemeEntryID_GRID));

    if (!m_entries.empty()) {
        const auto& e = m_entries[m_entry_index];
        const auto size = e.GetSize();
        gfx::drawImage(vg, 90, 130, 256, 256, m_icon ? m_icon : App::GetDefaultImage());

        nvgSave(vg);
            nvgIntersectScissor(vg, 50, 90, 325, 555);
            gfx::drawTextArgs(vg, 50, 415, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", e.lang_entry.name);
            gfx::drawTextArgs(vg, 50, 455, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", e.lang_entry.author);
            gfx::drawTextArgs(vg, 50, 495, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "App-ID: 0%lX", e.app_id);
            gfx::drawTextArgs(vg, 50, 535, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Key-Gen: %u (%s)", e.key_gen, nca::GetKeyGenStr(e.key_gen));
            gfx::drawTextArgs(vg, 50, 575, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Size: %.2f GB", (double)size / 0x40000000);
            gfx::drawTextArgs(vg, 50, 615, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Base: %zu Patch: %zu Addon: %zu Data: %zu", e.application.size(), e.patch.size(), e.add_on.size(), e.data_patch.size());
        nvgRestore(vg);
    }

    m_list->Draw(vg, theme, std::size(g_option_list), m_option_index, [this](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto text_y = y + (h / 2.f);
        auto colour = ThemeEntryID_TEXT;
        if (i == m_option_index) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
            // g_background.selected_bar = create_shape(Colour_Nintendo_Cyan, 90, 230, 4, 45, true);
            // draw_shape_position(&g_background.selected_bar, 485, g_options[i].text->rect.y - 10);
            gfx::drawRect(vg, 490, text_y - 45.f / 2.f, 2, 45, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
            colour = ThemeEntryID_TEXT_SELECTED;
        }
        if (i != 2 && !m_mounted) {
            colour = ThemeEntryID_TEXT_INFO;
        }

        gfx::drawTextArgs(vg, x + 15, y + (h / 2.f), 23.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(colour), "%s", i18n::get(g_option_list[i]).c_str());
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    GcOnEvent();
    UpdateStorageSize();
}

} // namespace sphaira::ui::menu::gc
