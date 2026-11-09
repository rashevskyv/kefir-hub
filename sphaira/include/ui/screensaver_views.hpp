#pragma once

// The inside of the screensaver: what each view shows and how big it is. The frame around it (drift, stick, edges,
// brightness, the switch-on) is ui::Screensaver's, the same for every view.

#include "ui/saver_frame.hpp"
#include "nanovg.h"

namespace sphaira::ui {

struct SaverInfo;
namespace menu { struct PolledData; }

namespace saver_view {

// the rectangle the view needs around its centre (the transfer readout, or the battery calibration's when info.battery_view)
auto Measure(const SaverInfo& info, const menu::PolledData& pdata) -> SaverExtent;
// draws the view inside `block`, the rectangle Measure() asked for, already placed on the panel
void Draw(NVGcontext* vg, const SaverInfo& info, SaverRect block, const menu::PolledData& pdata);

} // namespace saver_view
} // namespace sphaira::ui
