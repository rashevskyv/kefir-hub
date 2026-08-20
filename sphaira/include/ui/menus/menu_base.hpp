#pragma once

#include "ui/widget.hpp"
#include "ui/scrolling_text.hpp"
#include <string>

namespace sphaira::ui::menu {

enum MenuFlag {
    MenuFlag_None = 0,
    MenuFlag_Tab = 1 << 1,
};

struct PolledData {
    struct tm tm{};
    u32 battery_percetange{};
    PsmChargerType charger_type{};
    NifmInternetConnectionType type{};
    NifmInternetConnectionStatus status{};
    u32 strength{};
    u32 ip{};
    // SSID of the connected access point, empty for ethernet / no connection.
    std::string ssid{};
    // Storage info (NAND built-in + SD card)
    s64 nand_free{};
    s64 nand_total{};
    s64 sd_free{};
    s64 sd_total{};
};

struct MenuBase : Widget {
    MenuBase(const std::string& title, u32 flags);
    virtual ~MenuBase();

    virtual auto GetShortTitle() const -> const char* = 0;
    virtual void Update(Controller* controller, TouchInfo* touch);
    // draws the menu body only (background + waves). Subclasses call this
    // first, then draw their content on top.
    virtual void Draw(NVGcontext* vg, Theme* theme);

    // draws the header and footer. App calls this once the whole menu body is
    // down, so the chrome always wins over content that overflows its band.
    // Parts of the header covered by a panel stacked above are skipped rather
    // than drawn underneath it - see ui/layout.hpp.
    void DrawChrome(NVGcontext* vg, Theme* theme);

    // menus that replace the standard chrome with their own (the image viewer)
    // opt out here.
    auto WantsChrome() const -> bool override {
        return true;
    }

    auto IsMenu() const -> bool override {
        return true;
    }

    auto GetChromeOwner() -> MenuBase* override {
        return this;
    }

    void SetTitle(std::string title);
    void SetTitleSubHeading(std::string sub_heading, bool top_row = false);
    // two small stacked lines drawn right after the title (top / bottom).
    // The title sub heading shifts right to make room. Empty strings hide it.
    void SetTitleStats(std::string top, std::string bottom);
    void SetSubHeading(std::string sub_heading);
    void SetStorageHighlight(u64 nand_bytes, u64 sd_bytes);
    // like SetStorageHighlight, but the bytes are *planned* usage: the segment
    // is drawn extending from the used region into the free space (red when it
    // does not fit) and the label shows "+size". Used by the install queue.
    // The optional focus bytes are the part of that projection belonging to the
    // package in focus; they are drawn in a second colour at the head of the
    // segment and shown as "+focus / +total".
    void SetStorageProjection(u64 nand_bytes, u64 sd_bytes, u64 nand_focus = 0, u64 sd_focus = 0);
    void ClearStorageHighlight();

    void SetShowStorage(bool show) {
        m_show_storage = show;
    }

    auto ShowStorage() const -> bool {
        return m_show_storage;
    }

    auto GetTitle() const {
        return m_title;
    }

    auto IsTab() const -> bool {
        return m_flags & MenuFlag_Tab;
    }

    static auto GetPolledData(bool force_refresh = false) -> PolledData;

private:
    std::string m_title{};
    std::string m_title_sub_heading{};
    bool m_title_sub_heading_top_row{false};
    std::string m_sub_heading{};
    std::string m_title_stat_top{};
    std::string m_title_stat_bottom{};

    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_title_sub_heading{};
    ScrollingText m_scroll_sub_heading{};

    // left edge of the status block (storage bars), measured while drawing it.
    // The sub heading parks against it, so it has to survive the frame.
    float m_status_left_x{SCREEN_WIDTH - 60.f};

    u64 m_nand_highlight{};
    u64 m_sd_highlight{};
    // the share of the projection that belongs to the one package in focus
    // (hovered in the queue, or currently installing). Drawn in its own colour.
    u64 m_nand_focus{};
    u64 m_sd_focus{};
    bool m_storage_highlight_active{};
    bool m_storage_projection{};
    bool m_show_storage{true};

    u32 m_flags{};
};

} // namespace sphaira::ui::menu
