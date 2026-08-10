#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace sphaira::ui {

struct TimeoutInput {
    uint64_t kdown{0};
    uint64_t kheld{0};
    int32_t stick_l_x{0};
    int32_t stick_l_y{0};
    int32_t stick_r_x{0};
    int32_t stick_r_y{0};
    bool is_touching{false};
    bool is_clicked{false};
};

inline bool HasUserActivity(const TimeoutInput& input, int32_t deadzone = 4000) {
    if (input.kdown != 0 || input.kheld != 0) return true;
    if (input.is_touching || input.is_clicked) return true;
    if (std::abs(input.stick_l_x) > deadzone || std::abs(input.stick_l_y) > deadzone) return true;
    if (std::abs(input.stick_r_x) > deadzone || std::abs(input.stick_r_y) > deadzone) return true;
    return false;
}

class InactivityTracker {
public:
    void Reset(double current_time_sec) {
        m_last_activity_sec = current_time_sec;
    }

    void OnStateChange(bool is_installing, double current_time_sec) {
        if (!m_was_installing && is_installing) {
            m_last_activity_sec = current_time_sec;
        }
        m_was_installing = is_installing;
    }

    bool Update(bool is_installing, bool is_saver_active, long timeout_sec, const TimeoutInput& input, double current_time_sec, int32_t deadzone = 4000) {
        OnStateChange(is_installing, current_time_sec);

        if (!is_installing) {
            return false;
        }

        if (is_saver_active) {
            m_last_activity_sec = current_time_sec;
            return false;
        }

        if (HasUserActivity(input, deadzone)) {
            m_last_activity_sec = current_time_sec;
            return false;
        }

        if (timeout_sec <= 0) {
            return false;
        }

        const double idle_time = current_time_sec - m_last_activity_sec;
        if (idle_time >= static_cast<double>(timeout_sec)) {
            return true;
        }

        return false;
    }

private:
    double m_last_activity_sec{0.0};
    bool m_was_installing{false};
};

} // namespace sphaira::ui
