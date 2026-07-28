#pragma once

#include <functional>
#include <string>
#include <switch.h>

namespace sphaira::net {

// Network gate.
//
// Everything that needs the network (downloads, the web server, WebDAV) used to
// fail with whatever raw result nifm or curl returned, which showed up as an
// error code the user cannot act on. Nothing in a "0x2586E" tells you that the
// console simply has wi-fi turned off.
//
// So: ask nifm to bring the connection up first, and only if that fails say --
// in plain words -- that the internet is off. Entries that cannot work offline
// are drawn greyed out, and pressing one runs the same gate.

// true when the console currently has an ip. this is what the status header in
// MenuBase calls connected; nifmGetInternetConnectionStatus is stricter and
// never reports Connected on a console kept off nintendo's servers.
auto IsConnected() -> bool;

// same test, rate limited to one ipc a second. safe to call from Draw().
auto IsConnectedCached() -> bool;

// best effort connect: turns wireless comms back on when airplane mode is the
// only thing in the way (needs nifm:a, see main.cpp) and asks nifm to bring the
// configured network up, then waits for an ip. blocks for up to timeout_ms, so
// call it from a worker thread. cancelled() is polled while waiting.
auto TryConnect(u64 timeout_ms, const std::function<bool()>& cancelled = {}) -> bool;

// the gate itself. runs on_connected right away when there already is a
// connection; otherwise tries to connect behind a progress box and either runs
// the callback or explains that the internet is off, with a retry.
void RequireConnection(std::function<void()> on_connected);

// the plain-words popup on its own, for callers that already know they failed.
// on_retry, when set, is offered as the second button.
void ShowNoConnectionPopup(std::function<void()> on_retry = {});

// the text used by that popup. also used to rewrite network errors that would
// otherwise reach the user as a result code.
auto NoConnectionMessage() -> std::string;

// true when rc is a failure that "the console is offline" fully explains.
auto IsOfflineError(Result rc) -> bool;

// closes the nifm request held open for the connection, if any.
void Exit();

} // namespace sphaira::net
