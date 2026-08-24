#include "swkbd.hpp"
#include "app.hpp"
#include "defines.hpp"
#include <cstdlib>
#include <string>

namespace sphaira::swkbd {
namespace {

Result ShowInternal(bool numpad, std::string& out, const char* guide, const char* initial, s64 len_min, s64 len_max) {
    SwkbdConfig c;
    R_TRY(swkbdCreate(&c, 0));
    ON_SCOPE_EXIT(swkbdClose(&c));

    swkbdConfigMakePresetDefault(&c);
    swkbdConfigSetInitialCursorPos(&c, 1);

    if (numpad) {
        swkbdConfigSetType(&c, SwkbdType_NumPad);
    }

    if (guide) {
        swkbdConfigSetGuideText(&c, guide);
    }

    if (initial) {
        swkbdConfigSetInitialText(&c, initial);
    }

    if (len_min >= 0) {
        swkbdConfigSetStringLenMin(&c, len_min);
    }

    s64 max_len = len_max >= 0 ? len_max : (FS_MAX_PATH - 1);
    if (max_len < 1) {
        max_len = 1;
    }
    if (max_len > 0x8000) {
        max_len = 0x8000;
    }
    swkbdConfigSetStringLenMax(&c, max_len);

    std::string buf(static_cast<size_t>(max_len) + 1, '\0');
    const auto rc = swkbdShow(&c, buf.data(), buf.size());
    App::ResetTouchAfterApplet();
    R_TRY(rc);
    out = buf.c_str();
    R_SUCCEED();
}

} // namespace

Result ShowText(std::string& out, const char* guide, const char* initial, s64 len_min, s64 len_max) {
    return ShowInternal(false, out, guide, initial, len_min, len_max);
}

Result ShowNumPad(s64& out, const char* guide, const char* initial, s64 len_min, s64 len_max) {
    std::string text;
    R_TRY(ShowInternal(true, text, guide, initial, len_min, len_max));
    out = std::atoll(text.c_str());
    R_SUCCEED();
}

} // namespace sphaira::swkbd
