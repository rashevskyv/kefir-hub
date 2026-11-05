#!/bin/sh
# Builds the pinned UnRAR sources (sphaira/cmake/unrar/unrar.cmake) for the host, links
# sphaira/source/rar_reader.cpp against them and reads tests/contract_fixtures/rar.
# The tarball and the host objects are cached in ~/.cache/kefir-hub (first run needs network).
set -e
cd "$(dirname "$0")/.."
root="$(pwd)"
cm=sphaira/cmake/unrar/unrar.cmake
url=$(sed -n 's/^ *URL \(http[^ ]*\).*/\1/p' "$cm" | tr -d '\r')
sha=$(sed -n 's/^ *URL_HASH SHA256=\([0-9a-f]*\).*/\1/p' "$cm" | tr -d '\r')
# the object list from unrar.cmake (between "set(UNRAR_SOURCES" and ")").
srcs=$(sed -n '/^set(UNRAR_SOURCES/,/^)/p' "$cm" | tr -d '\r' | sed '1d;$d')

cache="${XDG_CACHE_HOME:-$HOME/.cache}/kefir-hub"
tgz="$cache/$(basename "$url")"
lib="$cache/unrar-host-$sha"
mkdir -p "$cache"
if ! echo "$sha  $tgz" | sha256sum -c --status - 2>/dev/null; then
    curl -sSfL -o "$tgz.tmp" "$url"
    echo "$sha  $tgz.tmp" | sha256sum -c --status -
    mv "$tgz.tmp" "$tgz"
fi

if [ ! -f "$lib/libunrar.a" ]; then
    rm -rf "$lib.tmp"
    mkdir -p "$lib.tmp"
    tar xzf "$tgz" -C "$lib.tmp" --strip-components=1
    # the same patch the Switch build applies (inert on the host: it keys on __SWITCH__).
    (cd "$lib.tmp" && cmake -P "$root/sphaira/cmake/unrar/patch_unrar.cmake" >/dev/null)
    for s in $srcs; do
        [ "$s" = rar ] && continue
        echo "$s"
    done | xargs -P "$(nproc)" -I{} g++ -O1 -std=gnu++11 -w -DRARDLL -DSILENT -D_FILE_OFFSET_BITS=64 \
        -c "$lib.tmp/{}.cpp" -o "$lib.tmp/{}.o"
    ar rcs "$lib.tmp/libunrar.a" "$lib.tmp"/*.o
    rm -rf "$lib"
    mv "$lib.tmp" "$lib"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
g++ -std=c++20 -Wall -Wextra -Werror -O1 -D_UNIX -I sphaira/include -I "$lib" \
    tests/rar/rar_reader_host.cpp sphaira/source/rar_reader.cpp "$lib/libunrar.a" -lpthread -o "$tmp/rar_reader_host"
"$tmp/rar_reader_host" tests/contract_fixtures/rar "$tmp/scratch"
