#pragma once

namespace sphaira::ntp {

// Background clock sync.
//
// The console's rtc drifts, and on a cfw setup that never talks to Nintendo's
// servers nothing corrects it -- clocks hours or months out are common, which
// makes log timestamps, screenshot dates and playlog entries useless.
//
// Start() spins up a low priority thread that idles until nifm reports an
// internet connection, queries an sntp server, and sets the system clock when
// it is off by more than a couple of seconds. It never prompts and never
// blocks the ui; if anything fails it simply tries again later.
void Start();
void Stop();

} // namespace sphaira::ntp
