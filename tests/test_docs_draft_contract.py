"""tools/docs/finish.py: draft blocks are found, and a proxy reply replaces a page only when it keeps the
shot markers and labels and leaves no draft behind. No proxy needed."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "docs"))
import finish  # noqa: E402

PAGE = """# Saves

<!-- shot: saves-list | Saves list -->

## Back up a save
1. Open [[Saves]].

<!-- draft
- [[Backup]] now stores every save type of the game; the tile shows a badge per type
-->

## Problems
**Nothing listed.** Install the game first.
"""


def main():
    assert finish.find_drafts(PAGE) == ["- [[Backup]] now stores every save type of the game; the tile shows a badge per type"]
    assert finish.find_drafts("<!-- draft: one line -->") == [": one line"]
    assert finish.find_drafts(PAGE.replace("draft", "shot:")) == []

    good = PAGE.replace(
        "<!-- draft\n- [[Backup]] now stores every save type of the game; the tile shows a badge per type\n-->",
        "[[Backup]] stores every save type of the game. The tile shows a badge per type.")
    assert finish.validate(PAGE, good) is None
    assert finish.validate(PAGE, finish.strip_fence("```markdown\n" + good.rstrip("\n") + "\n```\n")) is None
    assert finish.validate(PAGE, PAGE) == "a draft block is still there"
    assert finish.validate(PAGE, good.replace("<!-- shot: saves-list | Saves list -->", "")) == "shot markers changed"
    assert finish.validate(PAGE, good.replace("[[Backup]]", "[[Restore]]")).startswith("labels not in the page")
    assert finish.validate(PAGE, "# Saves\n\n<!-- shot: saves-list | Saves list -->\n") == "the page became much shorter"
    assert finish.other_changes(PAGE, good) == 0
    assert finish.other_changes(PAGE, good.replace("Install the game first.", "Install the game.")) == 1
    assert finish.validate(PAGE, "Sure! Here is the page:\n" + good) == "the page does not start with a heading"
    print("docs draft contract: ok")


if __name__ == "__main__":
    main()
