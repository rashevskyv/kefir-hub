#pragma once

#include <switch.h>

// Updates and DLC installed without their base game. Every install path (file
// browser, queue, USB, MTP, FTP, web) registers titles through yati, so yati
// reports here, and the App loop asks the user once the installs are over.
namespace sphaira::orphan_content {

// yati calls this for every update or DLC it registers. Any thread.
void NoteInstalled(u64 app_id);

// Once per frame from the App loop: when nothing was installed for a few seconds,
// asks to keep or delete the noted titles whose base game is not installed.
void Poll();

} // namespace sphaira::orphan_content
