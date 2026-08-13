#pragma once

#include "ui/types.hpp"
#include "ui/widget.hpp"
#include <string>

#include <array>

namespace sphaira::ui {

// what the Minus button does while the install queue is busy.
enum class BlankMode : long {
    Dim = 0,          // turn the panel brightness down, leave the ui alone
    BacklightOff = 1, // panel off entirely
    Screensaver = 2,  // black page with a small drifting readout
    MAX,
};

// bits of the "show on the screensaver" mask.
enum SaverField : long {
    SaverField_Clock    = 1 << 0,
    SaverField_Status   = 1 << 1,
    SaverField_File     = 1 << 2,
    SaverField_Counter  = 1 << 3,
    SaverField_Bar      = 1 << 4,
    SaverField_Speed    = 1 << 5,
    SaverField_Eta      = 1 << 6,
    SaverField_Elapsed  = 1 << 7,
    SaverField_Battery  = 1 << 8,
    SaverField_Errors   = 1 << 9,
    SaverField_Graph    = 1 << 10,
    SaverField_MAX      = 1 << 11,
    SaverField_ALL      = SaverField_MAX - 1,
};

// the live numbers the screensaver paints, rebuilt by the queue every frame.
struct SaverInfo {
    std::string status{};      // "Installing" / "Analysing" / "Done" ...
    std::string file{};        // package (and inner file) being written
    size_t package{};          // 1-based
    size_t package_count{};
    size_t installed{};
    size_t failed{};
    double ratio{};            // whole-queue progress, 0..1
    double speed_mib{};        // average write rate
    std::string eta{};         // empty until there are enough samples
    u64 elapsed_ns{};

    bool is_complete{false};
    bool is_failed{false};

    // Live R/W speed graph history
    bool has_graph{false};
    size_t history_count{0};
    size_t history_index{0};
    std::array<s64, 96> read_history{};
    std::array<s64, 96> write_history{};
};

// The Minus-key screen blank. Owns the console brightness while it is on, so
// it has to be Stop()ped before the owner goes away -- the destructor does it.
struct Screensaver {
    ~Screensaver() {
        Stop();
        FlushPendingBrightness();
    }

    // starts whichever mode the settings ask for. no-op if already running.
    void Start();
    void Stop();

    auto IsActive() const { return m_active; }

    // true while the black page owns the display, i.e. the menu underneath must
    // draw nothing. Dim and backlight-off leave the ui alone.
    auto OwnsScreen() const { return m_active && m_mode == BlankMode::Screensaver; }

    // any input wakes it, after a short grace period so the press that started
    // it -- or a finger still resting on the panel -- does not end it at once.
    auto WantsWake(const Controller* controller, const TouchInfo* touch) const -> bool;

    void Update(const Controller* controller, const TouchInfo* touch);
    void Draw(NVGcontext* vg, Theme* theme, const SaverInfo& info);

    void FlushPendingBrightness();

private:
    bool m_active{};
    BlankMode m_mode{};
    // console brightness state captured at Start() and put back by Stop().
    bool m_lbl_ready{};
    float m_saved_brightness{};
    bool m_saved_auto_brightness{};
    TimeStamp m_started{};
    TimeStamp m_drift{};
    TimeStamp m_last_update{};
    float m_user_offset_x{0.f};
    float m_user_offset_y{0.f};
    float m_drift_speed_mult{1.0f};
    float m_drift_accum{0.f};
    float m_current_brightness{1.0f};
    bool m_brightness_dirty{false};
};

// full screen preview of the current screensaver settings, so the readout can
// be judged at the brightness it will actually run at. Pops on any input.
struct SaverPreview final : Widget {
    SaverPreview();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    auto WantsChrome() const -> bool override { return !m_saver.OwnsScreen(); }

private:
    Screensaver m_saver{};
};

} // namespace sphaira::ui
