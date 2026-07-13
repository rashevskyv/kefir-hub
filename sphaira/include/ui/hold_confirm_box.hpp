#pragma once

#include "ui/widget.hpp"
#include <functional>
#include <string>

namespace sphaira::ui {

class HoldConfirmBox final : public Widget {
public:
    using Callback = std::function<void(bool)>;

public:
    HoldConfirmBox(std::string message, Callback callback);
    HoldConfirmBox(std::string message, float hold_seconds, Callback callback);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    std::string m_message;
    float m_hold_seconds{3.0f};
    Callback m_callback;
    bool m_holding{};
    u64 m_hold_start{};
    float m_progress{};
};

} // namespace sphaira::ui
