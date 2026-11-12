#pragma once

// The Apps tab of the Updater: every app the Homebrew menu shows, with the newest version its source knows.
// A updates the selected app: Kefir Hub from its GitHub release, DBI from DBIPatcher with the language
// the card has, every other app from the App Store sources.

#include "ui/widget.hpp"
#include "ui/list.hpp"
#include "ui/menus/kefir/kefir_apps_state.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::appstore {
struct Entry;
}

namespace sphaira::ui::menu::kefir {

struct AppRow {
    std::string name;
    std::string path;      // the nro on the card
    std::string installed; // its version
    std::string latest;    // the newest version its source knows; empty: none
    AppSource source{AppSource::Store};
    AppState state{AppState::NoInfo};
    std::string url;       // Hub: the release asset
    std::shared_ptr<appstore::Entry> store; // Store: the matched entry
};

class AppsTab final : public Widget {
public:
    explicit AppsTab(const Vec4& pos);
    ~AppsTab();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    void Activate(); // A: update the selected app
    void Refresh();  // X: ask every source again

private:
    void Build(); // the rows, from the answers saved on the card
    void UpdateHub(const AppRow& row);
    void UpdateDbi(const AppRow& row);
    void UpdateStoreApp(const AppRow& row);
    void Installed(Result rc, const std::string& name); // after an update: tell, then read the card again
    auto StatusText(const AppRow& row) const -> std::string;

    std::vector<AppRow> m_rows;
    std::unique_ptr<List> m_list;
    s64 m_index{};
    u32 m_pending{}; // answers still running (the callbacks run on the ui thread)
    bool m_loading{};
};

} // namespace sphaira::ui::menu::kefir
