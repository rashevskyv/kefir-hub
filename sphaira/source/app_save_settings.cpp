#include "app.hpp"
#include <string>
#include <utility>

namespace sphaira {

extern App* g_app;

auto App::GetSaveSettingsGlobally() -> bool {
    return g_app->m_save_settings_globally.Get();
}

auto App::GetSaveShowInstalled() -> bool {
    return g_app->m_save_show_installed.Get();
}

void App::SetSaveShowInstalled(bool enable) {
    g_app->m_save_show_installed.Set(enable);
}

auto App::GetSaveShowDeleted() -> bool {
    return g_app->m_save_show_deleted.Get();
}

void App::SetSaveShowDeleted(bool enable) {
    g_app->m_save_show_deleted.Set(enable);
}

auto App::GetSaveShowBackups() -> bool {
    return g_app->m_save_show_backups.Get();
}

void App::SetSaveShowBackups(bool enable) {
    g_app->m_save_show_backups.Set(enable);
}

auto App::GetSaveAutoBackupOnRestore() -> bool {
    return g_app->m_save_auto_backup_on_restore.Get();
}

void App::SetSaveAutoBackupOnRestore(bool enable) {
    g_app->m_save_auto_backup_on_restore.Set(enable);
}

auto App::GetSaveCompressBackup() -> bool {
    return g_app->m_save_compress_backup.Get();
}

void App::SetSaveCompressBackup(bool enable) {
    g_app->m_save_compress_backup.Set(enable);
}

auto App::GetSaveAutosync() -> bool {
    return g_app->m_save_autosync.Get();
}

void App::SetSaveAutosync(bool enable) {
    g_app->m_save_autosync.Set(enable);
}

auto App::GetSaveRestoreIncludeRemote() -> bool {
    return g_app->m_save_restore_include_remote.Get();
}

void App::SetSaveRestoreIncludeRemote(bool enable) {
    g_app->m_save_restore_include_remote.Set(enable);
}

auto App::GetSaveDefaultLocation() -> std::string {
    return g_app->m_save_default_location.Get();
}

void App::SetSaveDefaultLocation(std::string key) {
    g_app->m_save_default_location.Set(std::move(key));
}

} // namespace sphaira
