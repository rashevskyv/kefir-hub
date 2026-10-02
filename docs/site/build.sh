#!/bin/sh
# Build every language folder under docs/site/ into build/docs-site/: English at the root,
# others under /<code>/ (code = assets/romfs/i18n/<code>.json). A page missing from a language
# falls back to the English page; its [[Label]]s still come out in that language.
#     docs/site/build.sh          needs mkdocs-material (docs/site/requirements.txt)
set -e
cd "$(dirname "$0")"
here=$(pwd)
root=$(cd ../.. && pwd)
stage="$root/build/docs-stage"
out="$root/build/docs-site"
rm -rf "$stage" "$out"

theme_lang() {
    case $1 in
        ptbr) echo pt-BR ;; zhtw) echo zh-TW ;; es419) echo es ;; frca) echo fr ;;
        engb) echo en ;; se) echo sv ;; *) echo "$1" ;;
    esac
}

build() {
    lang=$1 dest=$2
    mkdir -p "$stage/$lang"
    cp -r en/. "$stage/$lang/"
    [ "$lang" = en ] || cp -r "$lang/." "$stage/$lang/"
    DOCS_LANG=$lang DOCS_THEME_LANG=$(theme_lang "$lang") DOCS_STAGE="$stage/$lang" DOCS_OUT="$dest" \
        mkdocs build --strict -q -f "$here/mkdocs.yml"
    echo "built $lang -> $dest"
}

# English first: mkdocs cleans site_dir, so it must not run after the subfolders exist.
build en "$out"
for dir in */; do
    lang=${dir%/}
    [ "$lang" = en ] && continue
    [ -f "$root/assets/romfs/i18n/$lang.json" ] || continue
    build "$lang" "$out/$lang"
done
