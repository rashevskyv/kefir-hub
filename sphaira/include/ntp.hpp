#pragma once

#include <switch.h>

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
s64 GetDisplayOffset();

// Makes hekate's own clock (Nyx) show the same time: writes the offset between
// utc (posix seconds, correct now) and the raw rtc to /bootloader/nyx.ini.
// Called after every successful sync; any other clock source can call it too.
void SyncHekateClock(u64 utc);

} // namespace sphaira::ntp
