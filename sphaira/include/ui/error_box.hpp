#pragma once

#include "ui/widget.hpp"
#include <optional>

namespace sphaira::ui {

// symbolic name for a result ("FsError_PathNotFound"), or "" when unknown.
auto GetResultCodeName(Result rc) -> const char*;
// translated, human-readable explanation of a result, or "" when there is none.
auto GetResultDescription(Result rc) -> std::string;
// the "0x%08X" form used wherever a raw result is shown to the user.
auto FormatResult(Result rc) -> std::string;

class ErrorBox final : public Widget {
public:
    ErrorBox(Result code, const std::string& message);
    ErrorBox(const std::string& message);

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;

private:
    std::optional<Result> m_code{};
    std::string m_message{};
    std::string m_code_message{};
    std::string m_code_module{};
};

} // namespace sphaira::ui
