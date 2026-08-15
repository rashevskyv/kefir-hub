#pragma once

#include "ui/widget.hpp"
#include "ui/install_progress.hpp"
#include "ui/scrolling_text.hpp"
#include "fs.hpp"
#include <functional>
#include <span>
#include <atomic>
#include <array>

namespace sphaira::ui {

struct ProgressBox;
using ProgressBoxCallback = std::function<Result(ProgressBox*)>;
using ProgressBoxDoneCallback = std::function<void(Result rc)>;

struct ProgressBox final : Widget, InstallProgress {
    ProgressBox(
        int image,
        const std::string& action,
        const std::string& title,
        ProgressBoxCallback callback, ProgressBoxDoneCallback done = [](Result rc){},
        int cpuid = 1, int prio = PRIO_PREEMPTIVE, int stack_size = 1024*128
    );
    ~ProgressBox();

    ProgressBox(const ProgressBox&) = delete;
    ProgressBox& operator=(const ProgressBox&) = delete;
    ProgressBox(ProgressBox&&) = delete;
    ProgressBox& operator=(ProgressBox&&) = delete;

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto IsModal() const -> bool override { return true; }

    auto SetActionName(const std::string& action) -> ProgressBox&;
    auto SetTitle(const std::string& title) -> ProgressBox&;
    auto NewTransfer(const std::string& transfer) -> ProgressBox&;
    // changes only the transfer label, keeping offset/size/speed. used when one
    // progress bar spans several steps (e.g. a multi-component title move).
    auto SetTransfer(const std::string& transfer) -> ProgressBox&;
    auto ResetTransferProgress() -> ProgressBox&;
    auto UpdateTransfer(s64 offset, s64 size) -> ProgressBox&;

    auto Mute(bool mute) -> ProgressBox& { m_muted = mute; return *this; }

    // hide the transfer speed / ETA readout. used when the progress bar is
    // driven by synthetic units (e.g. WebDAV sync, one budget per file) rather
    // than real bytes, where a "MiB/s" figure derived from those units would be
    // meaningless. the percentage and bar keep working.
    auto SetHideSpeed(bool hide) -> ProgressBox& { m_hide_speed = hide; return *this; }
    auto NewTransferForce(const std::string& transfer) -> ProgressBox&;
    auto UpdateTransferForce(s64 offset, s64 size) -> ProgressBox&;

    // not const in order to avoid copy by using std::swap
    auto SetImage(int image) -> ProgressBox&;
    auto SetImageData(std::vector<u8>& data) -> ProgressBox&;
    auto SetImageDataConst(std::span<const u8> data) -> ProgressBox&;

    void RequestExit();
    void ShowCancelConfirmation();
    auto ShouldExit() -> bool;
    auto ShouldExitResult() -> Result;

    // a "detached" box lives outside the widget stack (see App::PushTransfer()):
    // it still runs its worker thread and can be Draw()n, but never receives
    // Update() and thus never blocks input to whatever menu is on screen.
    // it can be minimised into a small corner badge via L3 (see App::Update()).
    void SetDetached(bool detached) { m_detached = detached; }
    auto IsDetached() const { return m_detached; }
    void ToggleMinimized() { m_minimized = !m_minimized; }
    auto IsMinimized() const { return m_minimized; }

    // helper functions
    auto CopyFile(fs::Fs* fs_src, fs::Fs* fs_dst, const fs::FsPath& src, const fs::FsPath& dst, bool single_threaded = false) -> Result;
    auto CopyFile(fs::Fs* fs, const fs::FsPath& src, const fs::FsPath& dst, bool single_threaded = false) -> Result;
    auto CopyFile(const fs::FsPath& src, const fs::FsPath& dst, bool single_threaded = false) -> Result;
    void Yield();

    auto GetCpuId() const {
        return m_cpuid;
    }

    auto OnDownloadProgressCallback() {
        return [this](s64 dltotal, s64 dlnow, s64 ultotal, s64 ulnow){
            if (this->ShouldExit()) {
                return false;
            }

            if (dltotal) {
                this->UpdateTransfer(dlnow, dltotal);
            } else {
                this->UpdateTransfer(ulnow, ultotal);
            }

            return true;
        };
    }

    // auto-clear = false
    auto GetCancelEvent() {
        return &m_uevent;
    }

    Result CheckCancelled() override { return ShouldExitResult(); }
    UEvent* GetInstallCancelEvent() override { return GetCancelEvent(); }
    void SetInstallTitle(const std::string& title) override { SetTitle(title); }
    void SetInstallImage(std::vector<u8>& image) override { SetImageData(image); }
    void SetInstallTransfer(const std::string& transfer) override { NewTransfer(transfer); }
    void UpdateInstallTransfer(s64 offset, s64 size) override { UpdateTransfer(offset, size); }
    void InstallYield() override { Yield(); }
    void OnCompatibilityWarning(const CompatibilityWarning& warning) override;

private:
    void FreeImage();

public:
    struct ThreadData {
        ProgressBox* pbox{};
        ProgressBoxCallback callback{};
        Result result{};
    };

private:
    UEvent m_uevent{};
    Mutex m_mutex{};
    Thread m_thread{};
    ThreadData m_thread_data{};
    ProgressBoxDoneCallback m_done{};

    // shared data start.
    std::string m_action{};
    std::string m_title{};
    std::string m_transfer{};
    s64 m_size{};
    s64 m_offset{};
    s64 m_last_offset{};
    s64 m_speed{};
    std::array<s64, 8> m_speed_samples{};
    size_t m_speed_sample_count{};
    size_t m_speed_sample_index{};
    TimeStamp m_timestamp{};
    std::vector<u8> m_image_data{};
    int m_image_pending{};
    bool m_is_image_pending{};
    // shared data end.

    ScrollingText m_scroll_action{};
    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_transfer{};

    int m_cpuid{};
    int m_image{};
    bool m_own_image{};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_hide_speed{false};
    bool m_detached{false};
    bool m_minimized{false};
    std::vector<CompatibilityWarning> m_compat_warnings{};
};

// this is a helper function that does many things.
// 1. creates a progress box, pushes that box to app
// 2. creates a thread and passes the pbox and callback to that thread
// 3. that thread calls the callback.

// this allows for blocking processes to run on a seperate thread whilst
// updating the ui with the progress of the operation.
// the callback should poll ShouldExit() whether to keep running

} // namespace sphaira::ui {
