#pragma once

#include "ui/types.hpp"

namespace sphaira {

struct FrameBufferSize {
    Vec2 size;
    Vec2 scale;
};

auto GetFrameBufferSize() -> FrameBufferSize;

} // namespace sphaira
