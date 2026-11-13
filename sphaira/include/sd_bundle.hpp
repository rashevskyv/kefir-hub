#pragma once

#include "ui/install_progress.hpp"
#include "yati/source/base.hpp"

namespace sphaira::sd_bundle {

// Reads a microSD bundle (sd_bundle_rules.hpp) of `size` bytes from `source` as one forward
// stream and writes its files to the microSD. Archives in it are extracted with their rules.
Result Receive(ui::InstallProgress* progress, yati::source::Base* source, s64 size);

// Reads a save bundle (save_bundle_rules.hpp) the same way and writes it into the game's cache
// storages (Kind::Cache, `uid` unused) or into `uid`'s save (Kind::Account). A missing storage is
// created from the game's control data, a small one is grown. Cache files are replaced; a file
// already in the user's save is kept (a reinstall must not reset progress).
Result ReceiveSave(ui::InstallProgress* progress, yati::source::Base* source, s64 size,
                   bool cache, u64 app_id, const AccountUid& uid);

} // namespace sphaira::sd_bundle
