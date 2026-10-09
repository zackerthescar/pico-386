#!/usr/bin/env bash
# Make a bootable 1.44 MB FreeDOS floppy with MAIN.EXE and the test games,
# for real hardware or 86Box. Run from 'nix develop' after 'make pico'.
#
#   tools/mkfloppy.sh [out.img]        (default: dos/PICO386.img)
#
# EXTENDER=dos32a (default) uses DOS/32A, renamed to DOS4GW.EXE. It is 28 KB;
# DOS/4GW is 265 KB, which is about 10 s more to read from a real floppy.
# EXTENDER=dos4gw uses DOS/4GW.
#
# The downloaded games (cache/games) are not redistributable: do not share
# the image.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:-$ROOT/dos/PICO386.img}"
SRC="$ROOT/dos/FLOPPY.img"          # FreeDOS 1.2 floppy (test.sh downloads it)
GAMES="${GAMES:-CELESTE CEL400 CRATE PIKORALL SHMUP ISHIDO NUDGE}"
export MTOOLS_SKIP_CHECK=1

[ -n "${WATCOM:-}" ] || { echo "WATCOM not set: run from 'nix develop'" >&2; exit 1; }
[ -f "$SRC" ] || { echo "$SRC missing: run ./test.sh once" >&2; exit 1; }
[ -f "$ROOT/dos/MAIN.EXE" ] || { echo "dos/MAIN.EXE missing: run 'make pico'" >&2; exit 1; }
case "${EXTENDER:-dos32a}" in
    dos32a) EXT="$WATCOM/binw/dos32a.exe" ;;
    dos4gw) EXT="$WATCOM/binw/dos4gw.exe" ;;
    *) echo "EXTENDER must be dos32a or dos4gw" >&2; exit 1 ;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for f in KERNEL.SYS COMMAND.COM FDSETUP/BIN/HIMEMX.EXE; do
    mcopy -n -i "$SRC" "::$f" "$TMP/$(basename "$f")"
done

# Blank FAT12 image with the FreeDOS boot code (keep the mformat BPB).
dd if=/dev/zero of="$IMG" bs=512 count=2880 2>/dev/null
mformat -i "$IMG" -f 1440 -v PICO386 ::
dd if="$SRC" of="$IMG" bs=1 count=3 conv=notrunc 2>/dev/null
dd if="$SRC" of="$IMG" bs=1 skip=62 seek=62 count=450 conv=notrunc 2>/dev/null

mcopy -i "$IMG" "$TMP/KERNEL.SYS" ::KERNEL.SYS
mcopy -i "$IMG" "$TMP/COMMAND.COM" ::COMMAND.COM
mcopy -i "$IMG" "$TMP/HIMEMX.EXE" ::HIMEMX.EXE
mcopy -i "$IMG" "$EXT" ::DOS4GW.EXE
mcopy -i "$IMG" "$ROOT/dos/MAIN.EXE" ::MAIN.EXE

list=""
for g in $GAMES; do
    cart="$ROOT/cache/games/$g.p8.png"
    [ -f "$cart" ] || { echo "skip $g: $cart missing (tools/fetch_games.sh)" >&2; continue; }
    mcopy -i "$IMG" "$cart" "::$g.P8"
    printf '@echo off\r\nMAIN.EXE %s.P8\r\n' "$g" | mcopy -i "$IMG" - "::$g.BAT"
    list="$list$(printf '  %-9s' "$g")"
done

printf '!FILES=40\r\nDEVICE=\\HIMEMX.EXE\r\n' | mcopy -i "$IMG" - ::FDCONFIG.SYS
printf '@echo off\r\nset DOS4G=quiet\r\necho.\r\necho pico-386: type a game name and press Enter. Esc quits a game.\r\necho.\r\necho %s\r\necho.\r\n' \
    "$list" | mcopy -i "$IMG" - ::AUTOEXEC.BAT

mdir -i "$IMG" :: | tail -2
echo "Wrote $IMG"
