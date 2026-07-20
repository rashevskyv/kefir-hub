#pragma once

#include "ui/menus/menu_base.hpp"
#include "yati/source/stream.hpp"
#include <atomic>
#include <memory>

namespace sphaira::ui::menu::stream {

enum InstallState {
    InstallState_None,
    InstallState_Progress,
    InstallState_Finished,
};

extern std::atomic<int> INSTALL_STATE;

enum class State {
    // not connected.
    None,
    // just connected, starts the transfer.
    Connected,
    // set whilst transfer is in progress.
    Progress,
    // set when the transfer is finished.
    Done,
    // failed to connect.
    Failed,
};

using OnInstallStart = std::function<bool(const char* path)>;
using OnInstallWrite = std::function<bool(const void* buf, size_t size)>;
using OnInstallClose = std::function<void()>;

struct Stream final : yati::source::Stream {
    Stream(const fs::FsPath& path, std::stop_token token);

    Result ReadChunk(void* buf, s64 size, u64* bytes_read) override;
    bool Push(const void* buf, s64 size);
    void Disable();
    void SignalCancel() override {
        Disable();
    }
    auto& GetPath() const { return m_path; }

private:
    fs::FsPath m_path{};
    std::stop_token m_token{};
    std::vector<u8> m_buffer{};
    CondVar m_can_read{};
    CondVar m_can_write{};

public:
    Mutex m_mutex{};
    std::atomic_bool m_active{};
};

struct Menu : MenuBase {
    Menu(const std::string& title, u32 flags);
    virtual ~Menu();

    virtual void Update(Controller* controller, TouchInfo* touch);
    virtual void Draw(NVGcontext* vg, Theme* theme);
    virtual void OnDisableInstallMode() = 0;

protected:
    friend class BackgroundInstaller;
    bool OnInstallStart(const char* path);
    bool OnInstallWrite(const void* buf, size_t size);
    void OnInstallClose();

private:
    std::unique_ptr<Stream> m_source{};
    Thread m_thread{};
    Mutex m_mutex{};
    State m_state{State::None};
};

class BackgroundInstaller {
public:
    static void RegisterMtpCallbacks();
    static void SetActiveMenu(Menu* menu);

    static bool OnInstallStart(const char* path);
    static bool OnInstallWrite(const void* buf, size_t size);
    static void OnInstallClose();

private:
    static Menu* s_active_menu;
    static std::shared_ptr<Stream> s_source;
    static std::stop_source s_stop_source;
    static std::atomic<bool> s_installing;
    static Mutex s_mutex;
    static CondVar s_callback_cond;
    static std::atomic<int> s_callback_count;
};

} // namespace sphaira::ui::menu::stream
