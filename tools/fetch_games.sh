#!/usr/bin/env bash
# Prepare the real-game carts of test/games/games.tsv for ./test.sh games.
#
#   tools/fetch_games.sh            fetch, verify and convert all carts
#   tools/fetch_games.sh NAME...    do only these games
#
# For each game this script:
#   1. takes the source (committed file or URL) and checks its SHA-256,
#   2. keeps downloads in cache/games/ (git-ignored) and skips cached files,
#   3. writes cache/games/NAME.p8.png (converts .p8 with tools/mkcart.py).
# A source "patch:BASE:FILE" makes a variant of the game BASE (an earlier
# line): tools/p8patch.py applies test/games/patches/FILE to its cart. The
# SHA-256 is then the hash of the patch file.
# It stops with an error on a hash mismatch.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="${GAMES_MANIFEST:-$ROOT/test/games/games.tsv}"
CACHE="${GAMES_CACHE_DIR:-$ROOT/cache/games}"
mkdir -p "$CACHE"

want=" $* "
rc=0
while IFS=$'\t' read -r name source sha license author page frames xfail note || [ -n "${name:-}" ]; do
    name="${name%$'\r'}"
    case "$name" in ''|'#'*) continue ;; esac
    [ "$#" -eq 0 ] || [[ "$want" == *" $name "* ]] || continue

    case "$source" in
        patch:*)
            base="${source#patch:}"; base="${base%%:*}"
            file="$ROOT/test/games/patches/${source##*:}"
            [ -f "$file" ] || { echo "fetch_games: $name: missing $file" >&2; rc=1; continue; }
            [ -f "$CACHE/$base.p8.png" ] || { echo "fetch_games: $name: base $base not ready" >&2; rc=1; continue; }
            ;;
        http://*|https://*)
            file="$CACHE/$name.src"
            actual=""
            [ ! -f "$file" ] || actual="$(sha256sum "$file" | cut -d' ' -f1)"
            if [ "$actual" != "$sha" ]; then
                echo "fetch_games: downloading $name"
                curl -fsSL --retry 3 --max-time 120 -o "$file.tmp" "$source" \
                    || { echo "fetch_games: $name: download failed" >&2; rm -f "$file.tmp"; rc=1; continue; }
                mv "$file.tmp" "$file"
            fi
            ;;
        *)
            case "$source" in /*) file="$source" ;; *) file="$ROOT/test/games/$source" ;; esac
            [ -f "$file" ] || { echo "fetch_games: $name: missing $file" >&2; rc=1; continue; }
            ;;
    esac

    actual="$(sha256sum "$file" | cut -d' ' -f1)"
    if [ "$actual" != "$sha" ]; then
        echo "fetch_games: $name: SHA-256 MISMATCH" >&2
        echo "  expected $sha" >&2
        echo "  actual   $actual" >&2
        rm -f "$CACHE/$name.src" "$CACHE/$name.p8.png"
        rc=1
        continue
    fi

    out="$CACHE/$name.p8.png"
    case "$source" in
        patch:*)
            python3 -I "$ROOT/tools/p8patch.py" "$CACHE/$base.p8.png" "$file" "$out" \
                || { echo "fetch_games: $name: p8patch failed" >&2; rc=1; continue; } ;;
        *.p8.png)
            cp "$file" "$out" ;;
        *.p8)
            python3 -I "$ROOT/tools/mkcart.py" "$file" "$out" 2>/dev/null \
                || { echo "fetch_games: $name: mkcart failed" >&2; rc=1; continue; } ;;
        *)
            echo "fetch_games: $name: unknown cart type: $source" >&2; rc=1; continue ;;
    esac
    echo "fetch_games: $name ok"
done < "$MANIFEST"

exit "$rc"
