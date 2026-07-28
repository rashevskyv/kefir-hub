#pragma once

#include "ui/widget.hpp"
#include "ui/list.hpp"
#include "ui/scrolling_text.hpp"
#include "option.hpp"
#include <memory>
#include <concepts>
#include <utility>

namespace sphaira::ui {

class SidebarEntryBase : public Widget {
public:
    using DependsCallback = std::function<bool(void)>;
    using DependsClickCallback = std::function<void(void)>;

public:
    explicit SidebarEntryBase(const std::string& title, const std::string& info);
    void SetTitle(const std::string& title) {
        m_title = title;
    }

    using Widget::Draw;
    virtual void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left);
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;

    // decorative entries (group headers) cannot be selected; the cursor
    // scrolls straight past them.
    virtual auto IsFocusable() const -> bool {
        return true;
    }

    void DrawEntry(NVGcontext* vg, Theme* theme, const std::string& left, const std::string& right, bool use_selected);

    void Depends(const DependsCallback& callback, const std::string& depends_info, const DependsClickCallback& depends_click = {}) {
        m_depends_callback = callback;
        m_depends_info = depends_info;
        m_depends_click = depends_click;
    }

    void Depends(bool& value, const std::string& depends_info, const DependsClickCallback& depends_click = {}) {
        m_depends_callback = [&value](){ return value; };
        m_depends_info = depends_info;
        m_depends_click = depends_click;
    }

    void Depends(option::OptionBool& value, const std::string& depends_info, const DependsClickCallback& depends_click = {}) {
        m_depends_callback = [&value](){ return value.Get(); };
        m_depends_info = depends_info;
        m_depends_click = depends_click;
    }

protected:
    auto IsEnabled() const -> bool {
        if (m_depends_callback) {
            return m_depends_callback();
        }

        return true;
    }

    void DependsClick() const {
        if (m_depends_click) {
            m_depends_click();
        }
    }

protected:
    std::string m_title;

private:
    std::string m_info{};
    std::string m_depends_info{};
    DependsCallback m_depends_callback{};
    DependsClickCallback m_depends_click{};
    ScrollingText m_scolling_title{};
    ScrollingText m_scolling_value{};
};

template<typename T>
concept DerivedFromSidebarBase = std::is_base_of_v<SidebarEntryBase, T>;

class SidebarEntryBool final : public SidebarEntryBase {
public:
    using Callback = std::function<void(bool&)>;

public:
    explicit SidebarEntryBool(const std::string& title, bool option, Callback cb, const std::string& info = "", const std::string& true_str = "On", const std::string& false_str = "Off");
    explicit SidebarEntryBool(const std::string& title, bool& option, const std::string& info = "", const std::string& true_str = "On", const std::string& false_str = "Off");
    explicit SidebarEntryBool(const std::string& title, option::OptionBool& option, const Callback& cb, const std::string& info = "", const std::string& true_str = "On", const std::string& false_str = "Off");
    explicit SidebarEntryBool(const std::string& title, option::OptionBool& option, const std::string& info = "", const std::string& true_str = "On", const std::string& false_str = "Off");
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;

private:
    bool m_option;
    Callback m_callback;
    std::string m_true_str;
    std::string m_false_str;
};

class SidebarEntryCheckbox final : public SidebarEntryBase {
public:
    using Getter = std::function<bool()>;
    using Callback = std::function<void(bool)>;

public:
    explicit SidebarEntryCheckbox(const std::string& title, Getter getter, Callback cb, const std::string& info = "");
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;

private:
    Getter m_getter;
    Callback m_callback;
};

class SidebarEntryHeader final : public SidebarEntryBase {
public:
    explicit SidebarEntryHeader(const std::string& title, const std::string& info = "");
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;

    auto IsFocusable() const -> bool override {
        return false;
    }
};

class SidebarEntryCallback final : public SidebarEntryBase {
public:
    using Callback = std::function<void()>;

public:
    explicit SidebarEntryCallback(const std::string& title, Callback cb, const std::string& info);
    explicit SidebarEntryCallback(const std::string& title, Callback cb, bool pop_on_click = false, const std::string& info = "");
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;
    auto OnFocusLost() noexcept -> void override;

    void SetHasSubmenu(bool value = true) {
        m_has_submenu = value;
    }

    bool HasSubmenu() const noexcept {
        return m_has_submenu;
    }

private:
    Callback m_callback;
    bool m_pop_on_click;
    bool m_has_submenu{false};
    ScrollingText m_scolling_entry_title{};
};

class SidebarEntryArray final : public SidebarEntryBase {
public:
    using Items = std::vector<std::string>;
    using ListCallback = std::function<void()>;
    using Callback = std::function<void(s64& index)>;

public:
    explicit SidebarEntryArray(const std::string& title, const Items& items, Callback cb, s64 index = 0, const std::string& info = "");
    explicit SidebarEntryArray(const std::string& title, const Items& items, Callback cb, const std::string& index, const std::string& info = "");
    explicit SidebarEntryArray(const std::string& title, const Items& items, std::string& index, const std::string& info = "");
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;

private:
    Items m_items;
    ListCallback m_list_callback;
    Callback m_callback;
    s64 m_index;
};

// single text entry.
// the callback is called when the entry is clicked.
// usually, the within the callback the text will be changed, use SetText().
class SidebarEntryTextBase : public SidebarEntryBase {
public:
    using Callback = std::function<void(void)>;

public:
    explicit SidebarEntryTextBase(const std::string& title, const std::string& value, const Callback& cb, const std::string& info = "");

    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;

    void SetCallback(const Callback& cb) {
        m_callback = cb;
    }

    auto GetValue() const -> const std::string& {
        return m_value;
    }

    void SetValue(const std::string& value) {
        m_value = value;
    }

private:
    std::string m_value;
    Callback m_callback;
};

class SidebarEntryTextInput final : public SidebarEntryTextBase {
public:
    explicit SidebarEntryTextInput(const std::string& title, const std::string& value, const std::string& guide = {}, s64 len_min = -1, s64 len_max = FS_MAX_PATH, const std::string& info = "");

private:
    const std::string m_guide;
    const s64 m_len_min;
    const s64 m_len_max;
};

class SidebarEntryFilePicker final : public SidebarEntryTextBase {
public:
    explicit SidebarEntryFilePicker(const std::string& title, const std::string& value, const std::vector<std::string>& filter, const std::string& info = "");

    // extension filter.
    void SetFilter(const std::vector<std::string>& filter) {
        m_filter = filter;
    }

private:
    std::vector<std::string> m_filter{};
};

class Sidebar : public Widget {
public:
    enum class Side { LEFT, RIGHT };
    using Items = std::vector<std::unique_ptr<SidebarEntryBase>>;

public:
    explicit Sidebar(const std::string& title, Side side, Items&& items);
    explicit Sidebar(const std::string& title, Side side);
    explicit Sidebar(const std::string& title, const std::string& sub, Side side, Items&& items);
    explicit Sidebar(const std::string& title, const std::string& sub, Side side);

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;

    // the panel spans the full height of its side of the screen and draws its
    // own title and separators there, so it takes that slice of the header and
    // footer over from the menu underneath instead of covering it - the
    // sidebar fill is not quite opaque and the menu's clock, storage bars and
    // hint row used to read straight through it.
    auto GetChromeOcclusion() const -> Vec4 override {
        return m_pos;
    }

    auto Add(std::unique_ptr<SidebarEntryBase>&& entry) -> SidebarEntryBase*;

    template<DerivedFromSidebarBase T, typename... Args>
    auto Add(Args&&... args) -> T* {
        return (T*)Add(std::make_unique<T>(std::forward<Args>(args)...));
    }

private:
    void SetIndex(s64 index);
    void SetupButtons();
    auto IsFocusable(s64 index) const -> bool;
    // walks past non-focusable entries (group headers) in the direction of
    // travel, reusing the list's own scrolling so the view stays in sync.
    auto SkipUnfocusable(s64 index, bool forward) -> s64;

private:
    std::string m_title;
    std::string m_sub;
    Side m_side;
    Items m_items;
    s64 m_index{};
    // set once the first selectable entry has been given focus.
    bool m_has_focus{};

    std::unique_ptr<List> m_list;

    Vec4 m_top_bar{};
    Vec4 m_bottom_bar{};
    Vec2 m_title_pos{};
    Vec4 m_base_pos{};

    static constexpr float m_title_size{28.f};
    // static constexpr Vec2 box_size{380.f, 70.f};
    static constexpr Vec2 m_box_size{400.f, 70.f};
};

} // namespace sphaira::ui
