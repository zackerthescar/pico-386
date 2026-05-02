#!/usr/bin/env bash
# Fetch external PICO-8 carts listed in test/carts.manifest into a local cache.
#
# Usage:
#   script/fetch_carts.sh                Download all permissive carts.
#   script/fetch_carts.sh --print-hashes Print sha256 of each downloaded cart
#                                        in manifest column form (for locking).
#   script/fetch_carts.sh --list         List manifest entries (no download).
#   script/fetch_carts.sh --check        Verify cache against manifest hashes.
#   script/fetch_carts.sh --clean        Remove the cache directory.
#
# Cache layout (gitignored):
#   cache/carts/<dos_name>.p8.png
#
# Proprietary Lexaloffle carts are NOT handled here. Drop them into a local
# directory and point LEXALOFFLE_CARTS_DIR at it (see test/CARTS.md).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="${CART_MANIFEST:-$ROOT_DIR/test/carts.manifest}"
CACHE_DIR="${CART_CACHE_DIR:-$ROOT_DIR/cache/carts}"

die()  { echo "fetch_carts: $*" >&2; exit 1; }
warn() { echo "fetch_carts: $*" >&2; }
info() { echo "fetch_carts: $*"; }

[ -f "$MANIFEST" ] || die "manifest not found: $MANIFEST"

mode="download"
if [ "$#" -ge 1 ]; then
    case "$1" in
        --print-hashes) mode="print-hashes" ;;
        --list)         mode="list" ;;
        --check)        mode="check" ;;
        --clean)        mode="clean" ;;
        -h|--help)      sed -n '2,16p' "$0"; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
fi

if [ "$mode" = "clean" ]; then
    if [ -d "$CACHE_DIR" ]; then
        rm -rf "$CACHE_DIR"
        info "removed $CACHE_DIR"
    fi
    exit 0
fi

mkdir -p "$CACHE_DIR"

# read manifest line-by-line, skipping comments / blanks
rc=0
count=0
while IFS=$'\t' read -r dos_name url sha license source; do
    # strip CR if present
    dos_name="${dos_name%$'\r'}"
    [ -z "${dos_name:-}" ] && continue
    case "$dos_name" in \#*) continue ;; esac
    [ -n "${url:-}" ] || { warn "skipping malformed line for '$dos_name'"; continue; }

    count=$((count + 1))
    out="$CACHE_DIR/$dos_name.p8.png"

    case "$mode" in
    list)
        printf '%-12s %-10s %s\n' "$dos_name" "$license" "$source"
        ;;
    download)
        if [ -f "$out" ] && [ "$sha" != "-" ]; then
            actual=$(sha256sum "$out" | cut -d' ' -f1)
            if [ "$actual" = "$sha" ]; then
                info "cache hit: $dos_name ($license)"
                continue
            fi
            warn "cache stale for $dos_name (expected $sha, got $actual) — refetching"
            rm -f "$out"
        fi
        info "downloading $dos_name <- $url"
        if ! curl -fL --retry 3 --max-time 60 -o "$out.tmp" "$url"; then
            warn "download failed: $dos_name"
            rm -f "$out.tmp"
            rc=1
            continue
        fi
        if [ "$sha" != "-" ]; then
            actual=$(sha256sum "$out.tmp" | cut -d' ' -f1)
            if [ "$actual" != "$sha" ]; then
                warn "sha256 MISMATCH for $dos_name: expected $sha, got $actual"
                rm -f "$out.tmp"
                rc=1
                continue
            fi
        else
            actual=$(sha256sum "$out.tmp" | cut -d' ' -f1)
            warn "$dos_name: no hash pinned (got $actual) — lock it in carts.manifest"
        fi
        mv "$out.tmp" "$out"
        ;;
    print-hashes)
        if [ ! -f "$out" ]; then
            warn "$dos_name: not in cache, run without flags first"
            rc=1
            continue
        fi
        actual=$(sha256sum "$out" | cut -d' ' -f1)
        printf '%s\t%s\t%s\t%s\t%s\n' "$dos_name" "$url" "$actual" "$license" "$source"
        ;;
    check)
        if [ ! -f "$out" ]; then
            warn "$dos_name: missing from cache"
            rc=1
            continue
        fi
        actual=$(sha256sum "$out" | cut -d' ' -f1)
        if [ "$sha" = "-" ]; then
            warn "$dos_name: no pinned hash (cache: $actual)"
        elif [ "$actual" != "$sha" ]; then
            warn "$dos_name: HASH MISMATCH (expected $sha, got $actual)"
            rc=1
        else
            info "$dos_name: OK"
        fi
        ;;
    esac
done < "$MANIFEST"

if [ "$count" -eq 0 ]; then
    warn "manifest contained no entries"
fi

exit "$rc"
