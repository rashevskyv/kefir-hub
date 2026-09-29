"""Regression contract for save backup destinations and auto-sync source."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]
SAVE = ROOT / "sphaira/source/ui/menus/save"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> None:
    pub = source(SAVE / "save_backup_pub.cpp")
    inspection = source(SAVE / "save_backup_inspection.cpp")
    paths = source(ROOT / "sphaira/include/ui/menus/save/save_paths.hpp")
    options = source(SAVE / "save_menu_options.cpp")

    writer = pub[pub.index("Result Menu::BackupSaveInternal("):]
    assert "BuildDbiSavePath(e, now_tm, backup_root)" in writer
    assert "BuildSavePath(e, is_auto, backup_root)" in writer
    assert "DBI_SAVES_PATH" not in writer
    assert writer.count("if (out_path) *out_path = path;") == 2  # SD and stdio

    assert "CollectDbiBackups(fs, e, backup_root)" in pub
    assert "path::IsSubpathOf(path.s, target_root.s)" in pub
    assert "CollectDbiBackups(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root)" in inspection
    for root in ("backup_root.s", "DEFAULT_BACKUP_ROOT", "DBI_SAVES_PATH", "DBI_SAVES_ROOT_PATH"):
        assert f"add_root({root});" in inspection
    assert "BuildDbiSavePath(const Entry& e, const struct tm& tm, const fs::FsPath& base)" in paths

    assert "&(*created_paths)[i]" in pub
    assert "const auto& latest_path = (*created_paths)[i];" in pub
    assert "FindLatestBackupPath(fs.get(), e, backup_root, latest_path)" not in pub

    description = "Choose the storage and folder for backups. Game saves are written beneath the chosen folder in DBI format, while existing DBI folders remain discoverable during Restore."
    assert description in options
    for lang in ("en", "uk"):
        translations = json.loads(source(ROOT / f"assets/romfs/i18n/{lang}.json"))
        assert translations.get(description)

    print("save backup destination contract: PASS")


if __name__ == "__main__":
    main()
