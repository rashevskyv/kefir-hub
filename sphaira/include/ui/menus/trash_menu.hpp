#pragma once

#include "system_cleanup.hpp"

namespace sphaira::ui::menu::trash_page {

// Tools → Trash: what the Hub deleted (trash.hpp), with Restore, Delete forever and Empty.
void Open();
// once on start: items older than 30 days go, and the oldest while the card is low on space.
void PurgeOnStart();
// for the cleanup step "Saves of removed users": a copy of each save into the trash, then the delete.
// Call it on the UI thread.
auto RemovedUserSavesDeleter() -> decltype(cleanup::Options::delete_user_saves);

} // namespace sphaira::ui::menu::trash_page
