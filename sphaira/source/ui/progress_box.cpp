#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "threaded_file_transfer.hpp"
#include "i18n.hpp"
#include "version_compare.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace sphaira::ui {
namespace {

void threadFunc(void* arg) {
    auto d = static_cast<ProgressBox::ThreadData*>(arg);
    d->result = d->callback(d->pbox);
    d->pbox->RequestExit();
}

} // namespace

ProgressBox::ProgressBox(int image, const std::string& action, const std::string& title, ProgressBoxCallback callback, ProgressBoxDoneCallback done, int cpuid, int prio, int stack_size, bool cpu_boost) {
    App::SetProgressActive(true);
    // FastLoad clocks the GPU down; ncm moves are storage-bound and the UI
    // (including Cancel) has to keep painting. Installs still want the boost.
    if (cpu_boost && App::GetApp()->m_progress_boost_mode.Get()) {
        App::SetBoostMode(true);
    }

    SetAction(Button::B, Action{"Back"_i18n, [this](){
        ShowCancelConfirmation();
    }});

    m_pos.w = 770.f;
    m_pos.h = 315.f;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    m_done = done;
    m_title = title;
    m_action = action;
    m_image = image;

    // create cancel event.
    ueventCreate(&m_uevent, false);

    m_cpuid = cpuid;
    m_thread_data.pbox = this;
    m_thread_data.callback = callback;
    if (R_FAILED(threadCreate(&m_thread, threadFunc, &m_thread_data, nullptr, stack_size, prio, cpuid))) {
        log_write("failed to create thead\n");
    }
    if (R_FAILED(threadStart(&m_thread))) {
        log_write("failed to start thread\n");
    }
}

void ProgressBox::OnCompatibilityWarning(const CompatibilityWarning& warning) {
    SCOPED_MUTEX(&m_mutex);
    auto w = warning;
    if (w.title_name.empty() && !m_title.empty()) {
        w.title_name = m_title;
    }
    for (auto& existing : m_compat_warnings) {
        if (existing.title_id == w.title_id) {
            if (version::IsLower(existing.required_hos, w.required_hos)) {
                existing = w;
            }
            return;
        }
    }
    m_compat_warnings.push_back(w);
}

ProgressBox::~ProgressBox() {
    ueventSignal(GetCancelEvent());
    m_stop_source.request_stop();

    if (R_FAILED(threadWaitForExit(&m_thread))) {
        log_write("failed to join thread\n");
    }
    if (R_FAILED(threadClose(&m_thread))) {
        log_write("failed to close thread\n");
    }

    FreeImage();
    if (!App::IsExiting()) {
        m_done(m_thread_data.result);

        if (R_SUCCEEDED(m_thread_data.result)) {
            for (const auto& w : m_compat_warnings) {
                App::Push<OptionBox>(FormatCompatibilityWarning(w), "OK"_i18n);
            }
        }
    }

    App::SetBoostMode(false);
    App::SetProgressActive(false);
}

auto ProgressBox::Update(Controller* controller, TouchInfo* touch) -> void {
    if (m_detached) {
        m_pos.h = 360.f;
        m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);
    }

    Widget::Update(controller, touch);

    if (ShouldExit()) {
        SetPop();
    }

    if (touch->is_clicked) {
        const float center_x = m_pos.x + m_pos.w / 2.f;
        const float btn_w = 200.f;
        const float btn_h = 40.f;
        const float btn_x = center_x - btn_w / 2.f;
        const float btn_y = m_detached ? (m_pos.y + m_pos.h - 95.f) : (m_pos.y + m_pos.h - 65.f);
        if (touch->in_range(btn_x, btn_y, btn_w, btn_h)) {
            App::PlaySoundEffect(SoundEffect_Focus);
            ShowCancelConfirmation();
        }
    }
}

auto ProgressBox::SetActionName(const std::string& action)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_action = action;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::SetTitle(const std::string& title)  -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_title = title;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::NewTransfer(const std::string& transfer)  -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_transfer = transfer;
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_speed = 0;
    m_speed_samples.fill(0);
    m_speed_sample_count = 0;
    m_speed_sample_index = 0;
    m_timestamp.Update();
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::NewTransferForce(const std::string& transfer)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_transfer = transfer;
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_speed = 0;
    m_speed_samples.fill(0);
    m_speed_sample_count = 0;
    m_speed_sample_index = 0;
    m_timestamp.Update();
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::SetTransfer(const std::string& transfer) -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_transfer = transfer;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::ResetTransferProgress() -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_speed = 0;
    m_speed_samples.fill(0);
    m_speed_sample_count = 0;
    m_speed_sample_index = 0;
    m_timestamp.Update();
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::UpdateTransfer(s64 offset, s64 size)  -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_size = size;
    m_offset = offset;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::UpdateTransferForce(s64 offset, s64 size)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_size = size;
    m_offset = offset;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::SetImage(int image) -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_image_pending = image;
    m_is_image_pending = true;
    mutexUnlock(&m_mutex);
    return *this;
}

auto ProgressBox::SetImageData(std::vector<u8>& data) -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    std::swap(m_image_data, data);
    mutexUnlock(&m_mutex);
    return *this;
}

auto ProgressBox::SetImageDataConst(std::span<const u8> data) -> ProgressBox& {
    if (m_muted) return *this;
    mutexLock(&m_mutex);
    m_image_data.resize(data.size());
    std::memcpy(m_image_data.data(), data.data(), m_image_data.size());
    mutexUnlock(&m_mutex);
    return *this;
}

void ProgressBox::RequestExit() {
    m_stop_source.request_stop();
    ueventSignal(GetCancelEvent());
}

void ProgressBox::ShowCancelConfirmation() {
    App::Push<OptionBox>("Are you sure you wish to cancel?"_i18n, "\uE0E1 " + "No"_i18n, "\uE0EF " + "Yes"_i18n, 1, [this](auto op_index){
        if (op_index && *op_index) {
            if (m_cancel_cb) {
                m_cancel_cb();
            }
            RequestExit();
            if (!m_detached) {
                SetPop();
            }
        }
    });
}

auto ProgressBox::ShouldExit() -> bool {
    return m_stop_source.stop_requested();
}

auto ProgressBox::ShouldExitResult() -> Result {
    if (ShouldExit()) {
        R_THROW(Result_TransferCancelled);
    }
    R_SUCCEED();
}

auto ProgressBox::CopyFile(fs::Fs* fs_src, fs::Fs* fs_dst, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    const auto is_file_based_emummc = App::IsFileBaseEmummc();
    const auto is_both_native = fs_src->IsNative() && fs_dst->IsNative();

    fs::File src_file;
    R_TRY(fs_src->OpenFile(src_path, FsOpenMode_Read, &src_file));

    s64 src_size;
    R_TRY(src_file.GetSize(&src_size));

    // this can fail if it already exists so we ignore the result.
    // if the file actually failed to be created, the result is implicitly
    // handled when we try and open it for writing.
    fs_dst->CreateFile(dst_path, src_size, 0);

    fs::File dst_file;
    R_TRY(fs_dst->OpenFile(dst_path, FsOpenMode_Write, &dst_file));
    R_TRY(dst_file.SetSize(src_size));

    R_TRY(thread::Transfer(this, src_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = src_file.Read(off, data, size, 0, bytes_read);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            const auto rc = dst_file.Write(off, data, size, 0);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        }, single_threaded ? thread::Mode::SingleThreaded : thread::Mode::MultiThreaded
    ));

    R_SUCCEED();
}

auto ProgressBox::CopyFile(fs::Fs* fs, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    return CopyFile(fs, fs, src_path, dst_path, single_threaded);
}

auto ProgressBox::CopyFile(const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    return CopyFile(&fs, src_path, dst_path, single_threaded);
}

void ProgressBox::Yield() {
    svcSleepThread(YieldType_ToAnyThread);
}

void ProgressBox::FreeImage() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }

    m_image = 0;
    m_own_image = false;
}

} // namespace sphaira::ui
