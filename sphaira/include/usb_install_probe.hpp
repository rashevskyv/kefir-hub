#pragma once

// A computer was plugged in. Before MTP takes the port, ask whether a USB
// install app (DBI Backend, ns-usbloader, Goldleaf) is already waiting on the
// PC: open usb:ds as the install device and run the install menu's own
// detection rounds for a few seconds in a thread. A host that answers hands
// its open link and file list to PC Install (USB); silence gives the port to
// MTP as before.

#include "yati/source/usb.hpp"
#include <memory>
#include <string>
#include <vector>

namespace sphaira::usb_probe {

enum class State {
    Idle,
    Running,
    Found,    // a host answered: TakeResult() yields the link and its list
    NotFound, // nobody answered (or the cable went away)
};

// starts the probe thread. No-op while one is running or in a build without
// network install (then the state stays Idle).
void Start();
auto GetState() -> State;
// collects the thread and the result; resets to Idle. Only after Found/NotFound.
void Finish(std::unique_ptr<yati::source::Usb>& out_source, std::vector<std::string>& out_names);

} // namespace sphaira::usb_probe
