#pragma once

#include "fs.hpp"
#include "ui/list.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/widget.hpp"
#include "zip_extract_plan.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui {

// Full-screen extract dialog: archive tree with checkboxes, then dest actions.
class ZipExtractBox final : public Widget {
public:
    using ExtractCb = std::function<void(fs::FsPath dest, std::string nro_only, std::vector<std::string> files)>;
    using BrowseCb = std::function<void(std::vector<std::string> files)>;

    ZipExtractBox(std::string title, std::vector<std::string> entry_names, ExtractCb extract, BrowseCb browse);

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;
    auto IsModal() const -> bool override { return true; }
    auto WantsChrome() const -> bool override { return false; }

private:
    void UpdateAHint();
    void SetFocusActions(bool actions);
    void Toggle(s64 index);
    void SyncDirChecks();
    void Invert();
    void OnA();
    void RunExtractAction(s64 index);
    auto SelectedFiles() const -> std::vector<std::string>;

    std::string m_title{};
    std::string m_hint{};
    std::string m_named_dest{};
    std::optional<std::string> m_nro{};
    std::vector<zip_extract::ZipTreeNode> m_nodes{};
    std::vector<char> m_checked{};
    std::vector<std::string> m_actions{};
    ExtractCb m_extract{};
    BrowseCb m_browse{};

    std::unique_ptr<List> m_tree_list{};
    std::unique_ptr<List> m_action_list{};
    ScrollingText m_tree_scroll{};
    ScrollingText m_action_scroll{};

    s64 m_tree_index{};
    s64 m_action_index{};
    bool m_focus_actions{};
    float m_tree_top{};
    float m_actions_top{};
};

} // namespace sphaira::ui
