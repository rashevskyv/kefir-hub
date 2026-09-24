#pragma once

#include "fs.hpp"
#include "yati/source/stream.hpp"
#include <atomic>
#include <memory>
#include <stop_token>

namespace sphaira::ui::menu::stream {

enum InstallState {
    InstallState_None,
    InstallState_Progress,
    InstallState_Finished,
};

extern std::atomic<int> INSTALL_STATE;

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
    size_t m_read_offset{0};
    CondVar m_can_read{};
    CondVar m_can_write{};

public:
    Mutex m_mutex{};
    std::atomic_bool m_active{};
};

} // namespace sphaira::ui::menu::stream

namespace sphaira::ui {
class InstallProgress;
}

namespace sphaira::ui::menu::dbi {
enum class TransportOrigin;
}

namespace sphaira::ui::menu::stream {

Result RunInstall(ui::InstallProgress* pbox, Stream* source);
void ScheduleMtpRestart();

class BackgroundInstaller {
public:
    static void RegisterMtpCallbacks();

    static bool OnInstallStart(const char* path, ui::menu::dbi::TransportOrigin origin);
    static bool OnInstallStart(const char* path);
    static bool OnInstallWrite(const void* buf, size_t size);
    static void OnInstallClose();
    static bool IsInstalling() { return s_installing.load(); }
    static void TeardownWorker();

private:
    static std::shared_ptr<Stream> s_source;
    static std::stop_source s_stop_source;
    static std::atomic<bool> s_installing;
    static Mutex s_mutex;
};

} // namespace sphaira::ui::menu::stream
