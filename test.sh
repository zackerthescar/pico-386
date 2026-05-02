#!/usr/bin/env bash
# Test harness for pico-386
#
# Modes:
#   ./test.sh              Run unit tests (TEST.EXE) in QEMU
#   ./test.sh integration  Run integration test (MAIN.EXE + cartridge) in QEMU
#   ./test.sh vga          Run VGA screenshot test (VGATEST.EXE) in QEMU
#   ./test.sh carts        Run integration test against every cart in
#                          test/carts.manifest plus optional Lexaloffle carts
#                          dropped under $LEXALOFFLE_CARTS_DIR (see
#                          test/CARTS.md). Carts are fetched into cache/carts/
#                          which is gitignored.
#
# Environment variables:
#   TIMEOUT              QEMU timeout in seconds (default: 30)
#   CART_URL             URL to a .p8.png cartridge for integration tests
#   CART_NAME            DOS filename stem (default: TESTCART)
#   LEXALOFFLE_CARTS_DIR Optional local directory of *.p8.png carts that may
#                        not be redistributed (proprietary BBS uploads). Used
#                        only by the 'carts' mode.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DOS_DIR="$SCRIPT_DIR/dos"
SERIAL_LOG="$SCRIPT_DIR/test_serial.log"
TIMEOUT="${TIMEOUT:-30}"
MODE="${1:-unit}"

FREEDOS_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.2/official/FD12FLOPPY.zip"

# Default test cart: "Poom" demo by freds72 (freely shared on BBS)
# Override with CART_URL= for a different cartridge.
CART_URL="${CART_URL:-https://www.lexaloffle.com/bbs/cposts/2/27508.p8.png}"
CART_NAME="${CART_NAME:-TESTCART}"

FLOPPY="$DOS_DIR/freedos.img"
FREEDOS_ZIP="$DOS_DIR/freedos.zip"
FREEDOS_ORIG="$DOS_DIR/FLOPPY.img"

# ── Helpers ──

die() { echo "FAIL: $*" >&2; exit 1; }

# Require WATCOM env (set by nix develop / devshell)
[ -n "${WATCOM:-}" ] || die "WATCOM env not set — run from within 'nix develop'"
DOS4GW="$WATCOM/binw/dos4gw.exe"
[ -f "$DOS4GW" ] || die "dos4gw.exe not found at $DOS4GW"

ensure_freedos() {
    if [ ! -f "$FREEDOS_ORIG" ]; then
        echo "Downloading FreeDOS floppy..."
        curl -L -o "$FREEDOS_ZIP" "$FREEDOS_URL"
        unzip -o "$FREEDOS_ZIP" -d "$DOS_DIR"
        rm -f "$FREEDOS_ZIP"
    fi
}

# Build a bootable floppy with the given files.
# Usage: build_floppy <autoexec_content> <file1> [file2] ...
# Each file arg is: <host_path>::<dos_name>
build_floppy() {
    local autoexec="$1"; shift

    ensure_freedos

    MTOOLS_SKIP_CHECK=1
    export MTOOLS_SKIP_CHECK

    local KERNEL COMMAND HIMEMX
    KERNEL="$(mktemp)" ; COMMAND="$(mktemp)" ; HIMEMX="$(mktemp)"
    trap 'rm -f "${KERNEL:-}" "${COMMAND:-}" "${HIMEMX:-}"' EXIT

    mcopy -n -i "$FREEDOS_ORIG" ::KERNEL.SYS "$KERNEL"
    mcopy -n -i "$FREEDOS_ORIG" ::COMMAND.COM "$COMMAND"
    mcopy -n -i "$FREEDOS_ORIG" ::FDSETUP/BIN/HIMEMX.EXE "$HIMEMX"

    dd if=/dev/zero of="$FLOPPY" bs=512 count=2880 2>/dev/null
    mformat -i "$FLOPPY" -f 1440 :: 2>/dev/null
    dd if="$FREEDOS_ORIG" of="$FLOPPY" bs=1 count=3 conv=notrunc 2>/dev/null
    dd if="$FREEDOS_ORIG" of="$FLOPPY" bs=1 skip=62 seek=62 count=$((512 - 62)) conv=notrunc 2>/dev/null

    mcopy -i "$FLOPPY" "$KERNEL"   ::KERNEL.SYS
    mcopy -i "$FLOPPY" "$COMMAND"  ::COMMAND.COM
    mcopy -i "$FLOPPY" "$HIMEMX"   ::HIMEMX.EXE
    mcopy -i "$FLOPPY" "$DOS4GW" ::DOS4GW.EXE

    for spec in "$@"; do
        local host="${spec%%::*}"
        local dos="${spec##*::}"
        mcopy -i "$FLOPPY" "$host" "::$dos"
    done

    printf '!FILES=40\r\nDEVICE=\\HIMEMX.EXE\r\n' | mcopy -i "$FLOPPY" - ::FDCONFIG.SYS
    printf '%s' "$autoexec" | mcopy -i "$FLOPPY" - ::AUTOEXEC.BAT
}

run_qemu() {
    rm -f "$SERIAL_LOG"
    echo "Starting QEMU with ${TIMEOUT}s timeout..."
    timeout "$TIMEOUT" qemu-system-i386 \
        -drive file="$FLOPPY",format=raw,if=floppy,file.locking=off \
        -serial null \
        -serial file:"$SERIAL_LOG" \
        -display none \
        -m 32 \
        -boot a \
        2>/dev/null || true

    if [ ! -f "$SERIAL_LOG" ] || [ ! -s "$SERIAL_LOG" ]; then
        die "No serial output produced"
    fi
}

# Quiet make: only show errors
quiet_make() {
    local output
    output=$(make -C "$SCRIPT_DIR" "$@" 2>&1) || {
        echo "$output" >&2
        die "Build failed"
    }
}

# Run QEMU with monitor socket, wait for serial marker, screendump, quit.
# Usage: run_qemu_screenshot <marker_string> <output_png>
run_qemu_screenshot() {
    local MARKER="$1"
    local OUTPUT_PNG="$2"
    local MONITOR_SOCK
    MONITOR_SOCK="$(mktemp -u /tmp/qemu-monitor.XXXXXX).sock"
    local SCREENSHOT_PPM
    SCREENSHOT_PPM="$(mktemp /tmp/qemu-screen.XXXXXX.ppm)"

    rm -f "$SERIAL_LOG" "$MONITOR_SOCK" "$SCREENSHOT_PPM"

    echo "Starting QEMU with monitor socket..."
    qemu-system-i386 \
        -drive file="$FLOPPY",format=raw,if=floppy,file.locking=off \
        -serial null \
        -serial file:"$SERIAL_LOG" \
        -display none \
        -m 32 \
        -boot a \
        -monitor unix:"$MONITOR_SOCK",server,nowait \
        2>/dev/null &
    local QEMU_PID=$!

    # Watchdog: kill after TIMEOUT
    ( sleep "$TIMEOUT" && kill "$QEMU_PID" 2>/dev/null ) &
    local WATCHDOG_PID=$!

    # Wait for monitor socket to appear
    local i
    for i in $(seq 1 60); do
        [ -S "$MONITOR_SOCK" ] && break
        sleep 0.2
    done
    [ -S "$MONITOR_SOCK" ] || { kill "$QEMU_PID" 2>/dev/null; die "Monitor socket never appeared"; }

    # Poll serial log for the marker string
    echo "Waiting for '$MARKER' in serial output..."
    for i in $(seq 1 "$TIMEOUT"); do
        if grep -q "$MARKER" "$SERIAL_LOG" 2>/dev/null; then
            sleep 1  # let VGA settle one more second
            break
        fi
        sleep 1
    done

    if ! grep -q "$MARKER" "$SERIAL_LOG" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        kill "$WATCHDOG_PID" 2>/dev/null || true
        rm -f "$MONITOR_SOCK"
        die "Marker '$MARKER' never appeared in serial output"
    fi

    # Capture screenshot
    echo "screendump $SCREENSHOT_PPM" | socat - UNIX-CONNECT:"$MONITOR_SOCK" || true
    sleep 0.5

    # Quit QEMU
    echo "quit" | socat - UNIX-CONNECT:"$MONITOR_SOCK" || true
    wait "$QEMU_PID" 2>/dev/null || true
    kill "$WATCHDOG_PID" 2>/dev/null || true
    rm -f "$MONITOR_SOCK"

    [ -f "$SCREENSHOT_PPM" ] || die "screendump did not produce $SCREENSHOT_PPM"

    # Convert PPM -> PNG
    convert "$SCREENSHOT_PPM" "$OUTPUT_PNG"
    rm -f "$SCREENSHOT_PPM"

    echo "Screenshot saved: $OUTPUT_PNG"
}

# ── Unit tests ──

run_unit_tests() {
    echo "=== Building unit tests ==="
    quiet_make test

    [ -f "$DOS_DIR/TEST.EXE" ] || die "Build did not produce TEST.EXE"

    echo "=== Booting TEST.EXE in QEMU ==="
    build_floppy "$(printf '@echo off\r\nTEST.EXE\r\n')" \
        "$DOS_DIR/TEST.EXE::TEST.EXE"
    run_qemu

    echo ""
    echo "=== Serial output (COM2) ==="
    cat "$SERIAL_LOG"
    echo ""

    echo "=== Results ==="
    if grep -q "# ALL TESTS PASSED" "$SERIAL_LOG"; then
        local passed failed
        passed=$(grep -c "^ok " "$SERIAL_LOG" || true)
        failed=$(grep -c "^not ok " "$SERIAL_LOG" || true)
        echo "  $passed passed, $failed failed"
        echo ""
        echo "UNIT TESTS PASSED"
        return 0
    elif grep -q "# SOME TESTS FAILED" "$SERIAL_LOG"; then
        echo "  Failed tests:"
        grep "^not ok " "$SERIAL_LOG" | sed 's/^/    /'
        grep "^# FAIL" "$SERIAL_LOG" | sed 's/^/    /'
        echo ""
        echo "UNIT TESTS FAILED"
        return 1
    else
        echo "  Test runner did not complete (timeout or crash)"
        echo "  Last serial output:"
        grep "^" "$SERIAL_LOG" | sed 's/^/    /'
        echo ""
        echo "UNIT TESTS INCONCLUSIVE"
        return 1
    fi
}

run_vm_tests() {
    echo "=== Building VM tests ==="
    quiet_make vm-test

    [ -f "$DOS_DIR/VMTEST.EXE" ] || die "Build did not produce VMTEST.EXE"

    echo "=== Booting VMTEST.EXE in QEMU ==="
    build_floppy "$(printf '@echo off\r\nVMTEST.EXE\r\n')" \
        "$DOS_DIR/VMTEST.EXE::VMTEST.EXE"
    run_qemu

    echo ""
    echo "=== Serial output (COM2) ==="
    cat "$SERIAL_LOG"
    echo ""

    echo "=== Results ==="
    if grep -q "# ALL TESTS PASSED" "$SERIAL_LOG"; then
        local passed failed
        passed=$(grep -c "^ok " "$SERIAL_LOG" || true)
        failed=$(grep -c "^not ok " "$SERIAL_LOG" || true)
        echo "  $passed passed, $failed failed"
        echo ""
        echo "VM TESTS PASSED"
        return 0
    fi

    echo "VM TESTS FAILED OR INCONCLUSIVE"
    grep "^not ok \|^# FAIL" "$SERIAL_LOG" | sed 's/^/    /' || true
    return 1
}

# ── Integration test ──

run_integration_test() {
    echo "=== Building pico-386 ==="
    quiet_make pico

    [ -f "$DOS_DIR/MAIN.EXE" ] || die "Build did not produce MAIN.EXE"

    CART_PNG="$DOS_DIR/$CART_NAME.p8.png"
    if [ ! -f "$CART_PNG" ]; then
        echo "Downloading test cartridge..."
        curl -L -o "$CART_PNG" "$CART_URL"
    fi

    echo "=== Booting MAIN.EXE + $CART_NAME in QEMU ==="
    build_floppy "$(printf '@echo off\r\nMAIN.EXE %s.P8\r\n' "$CART_NAME")" \
        "$DOS_DIR/MAIN.EXE::MAIN.EXE" \
        "$CART_PNG::$CART_NAME.P8"
    run_qemu

    echo ""
    echo "=== Serial output (COM2) ==="
    cat "$SERIAL_LOG"
    echo ""

    echo "=== Validation ==="
    local PASS=true

    if grep -q "Going into VGA" "$SERIAL_LOG"; then
        echo "  [PASS] Serial init OK"
    else
        echo "  [FAIL] No serial init message"
        PASS=false
    fi

    if grep -q "Loading cart" "$SERIAL_LOG"; then
        echo "  [PASS] Cart loading started"
    else
        echo "  [FAIL] Cart loading never started"
        PASS=false
    fi

    if grep -q "pico8_decomp: no code" "$SERIAL_LOG"; then
        echo "  [FAIL] Decompression produced no code"
        PASS=false
    fi

    if grep -q "Lua code:" "$SERIAL_LOG"; then
        echo "  [PASS] Lua code decompressed"
        grep "Lua code:" "$SERIAL_LOG" | sed 's/^/         /'
    else
        echo "  [FAIL] No Lua code decompressed"
        PASS=false
    fi

    if grep -q "p8_compile: OK" "$SERIAL_LOG"; then
        echo "  [PASS] Compiler produced bytecode"
        grep "p8_compile: OK" "$SERIAL_LOG" | sed 's/^/         /'
    elif grep -q "p8_compile: FAIL" "$SERIAL_LOG"; then
        echo "  [FAIL] Compiler rejected code"
        PASS=false
    elif grep -q "p8_compile:" "$SERIAL_LOG"; then
        echo "  [WARN] Compiler started but did not finish"
    else
        echo "  [WARN] Compiler did not run"
    fi

    if grep -q "Unloading cart" "$SERIAL_LOG"; then
        echo "  [PASS] Clean shutdown"
    else
        echo "  [WARN] Did not reach clean shutdown (may need longer timeout)"
    fi

    if [ "$PASS" = true ]; then
        echo ""
        echo "INTEGRATION TEST PASSED"
    else
        echo ""
        echo "INTEGRATION TEST FAILED"
        exit 1
    fi
}

# ── VGA screenshot test ──

# SHA-256 of the expected VGA test screenshot (color bars, Mode X 320x400).
# Regenerate with: ./test.sh vga --update-hash
VGA_TEST_HASH="65e068e39d3ecbd9baf700b793462b2835b6cacb7ec6968c4930e0336fdb4c54"

run_vga_test() {
    echo "=== Building VGA test ==="
    quiet_make vga-test

    [ -f "$DOS_DIR/VGATEST.EXE" ] || die "Build did not produce VGATEST.EXE"

    echo "=== Booting VGATEST.EXE in QEMU ==="
    build_floppy "$(printf '@echo off\r\nVGATEST.EXE\r\n')" \
        "$DOS_DIR/VGATEST.EXE::VGATEST.EXE"

    local SCREENSHOT_PNG="$SCRIPT_DIR/test_vga_screenshot.png"
    run_qemu_screenshot "RENDER_COMPLETE" "$SCREENSHOT_PNG"

    echo ""
    echo "=== Serial output (COM2) ==="
    cat "$SERIAL_LOG"
    echo ""

    # Compute hash
    local ACTUAL_HASH
    ACTUAL_HASH=$(sha256sum "$SCREENSHOT_PNG" | cut -d' ' -f1)
    echo "Screenshot hash: $ACTUAL_HASH"

    if [ "${2:-}" = "--update-hash" ]; then
        echo ""
        echo "Update VGA_TEST_HASH in test.sh to:"
        echo "  VGA_TEST_HASH=\"$ACTUAL_HASH\""
        echo ""
        echo "VGA TEST: hash generated (update test.sh to lock it in)"
        return 0
    fi

    if [ -z "$VGA_TEST_HASH" ]; then
        echo ""
        echo "No reference hash set. Run './test.sh vga --update-hash' to generate one."
        echo "Screenshot at: $SCREENSHOT_PNG"
        echo ""
        echo "VGA TEST: SKIPPED (no reference hash)"
        return 0
    fi

    echo ""
    echo "=== Hash comparison ==="
    if [ "$ACTUAL_HASH" = "$VGA_TEST_HASH" ]; then
        echo "  [PASS] Screenshot matches reference"
        echo ""
        echo "VGA TEST PASSED"
        return 0
    else
        echo "  [FAIL] Screenshot hash mismatch"
        echo "    expected: $VGA_TEST_HASH"
        echo "    actual:   $ACTUAL_HASH"
        echo "    file:     $SCREENSHOT_PNG"
        echo ""
        echo "VGA TEST FAILED"
        return 1
    fi
}

# ── External cart matrix ──
#
# Builds MAIN.EXE once, then loops over every cart in test/carts.manifest plus
# any *.p8.png in $LEXALOFFLE_CARTS_DIR. Each cart is judged against the same
# serial-log heuristics as run_integration_test(); failures are summarised at
# the end so one bad cart does not abort the run.

CART_CACHE_DIR="${CART_CACHE_DIR:-$SCRIPT_DIR/cache/carts}"
CART_MANIFEST="${CART_MANIFEST:-$SCRIPT_DIR/test/carts.manifest}"

run_one_cart() {
    local label="$1"
    local cart_png="$2"
    local dos_stem="$3"

    [ -f "$cart_png" ] || { echo "  [SKIP] $label: missing $cart_png"; return 2; }

    echo "── $label ($dos_stem) ─────────────────────────────────────"

    build_floppy "$(printf '@echo off\r\nMAIN.EXE %s.P8\r\n' "$dos_stem")" \
        "$DOS_DIR/MAIN.EXE::MAIN.EXE" \
        "$cart_png::$dos_stem.P8"
    run_qemu

    local ok=true
    grep -q "Going into VGA"   "$SERIAL_LOG" || { echo "  [FAIL] no serial init"; ok=false; }
    grep -q "Loading cart"     "$SERIAL_LOG" || { echo "  [FAIL] cart load did not start"; ok=false; }
    if grep -q "pico8_decomp: no code" "$SERIAL_LOG"; then
        echo "  [FAIL] decompression produced no code"; ok=false
    fi
    grep -q "Lua code:" "$SERIAL_LOG" || { echo "  [FAIL] no Lua decompressed"; ok=false; }
    if grep -q "p8_compile: FAIL" "$SERIAL_LOG"; then
        echo "  [FAIL] compiler rejected code"; ok=false
    fi

    if [ "$ok" = true ]; then
        echo "  [PASS] $label"
        return 0
    fi
    echo "  serial tail:"
    tail -n 20 "$SERIAL_LOG" | sed 's/^/    /'
    return 1
}

run_carts_matrix() {
    echo "=== Building pico-386 ==="
    quiet_make pico
    [ -f "$DOS_DIR/MAIN.EXE" ] || die "Build did not produce MAIN.EXE"

    echo "=== Fetching permissive carts ==="
    "$SCRIPT_DIR/script/fetch_carts.sh" || echo "(fetch_carts.sh reported issues; continuing with whatever is cached)"

    local pass=0 fail=0 skip=0
    local failed_labels=()

    # Permissive carts from manifest
    if [ -f "$CART_MANIFEST" ]; then
        while IFS=$'\t' read -r dos_name url sha license source; do
            dos_name="${dos_name%$'\r'}"
            [ -z "${dos_name:-}" ] && continue
            case "$dos_name" in \#*) continue ;; esac
            local cart="$CART_CACHE_DIR/$dos_name.p8.png"
            local label="manifest:$dos_name [$license]"
            local rc=0
            run_one_cart "$label" "$cart" "$dos_name" || rc=$?
            case "$rc" in
                0) pass=$((pass+1)) ;;
                2) skip=$((skip+1)) ;;
                *) fail=$((fail+1)); failed_labels+=("$label") ;;
            esac
        done < "$CART_MANIFEST"
    fi

    # Optional proprietary Lexaloffle carts (local-only, never committed).
    if [ -n "${LEXALOFFLE_CARTS_DIR:-}" ] && [ -d "$LEXALOFFLE_CARTS_DIR" ]; then
        echo "=== Lexaloffle local carts ($LEXALOFFLE_CARTS_DIR) ==="
        local f
        for f in "$LEXALOFFLE_CARTS_DIR"/*.p8.png; do
            [ -f "$f" ] || continue
            local base stem
            base="$(basename "$f" .p8.png)"
            # Compress to a DOS 8.3-safe stem: keep [A-Z0-9], truncate.
            stem="$(printf '%s' "$base" | tr 'a-z' 'A-Z' | tr -c 'A-Z0-9' '_' | cut -c1-8)"
            [ -n "$stem" ] || stem="LEXACART"
            local label="lexaloffle:$base"
            local rc=0
            run_one_cart "$label" "$f" "$stem" || rc=$?
            case "$rc" in
                0) pass=$((pass+1)) ;;
                2) skip=$((skip+1)) ;;
                *) fail=$((fail+1)); failed_labels+=("$label") ;;
            esac
        done
    else
        echo "=== Lexaloffle carts: skipped (set LEXALOFFLE_CARTS_DIR to enable) ==="
    fi

    echo ""
    echo "=== Cart matrix summary ==="
    echo "  passed:  $pass"
    echo "  failed:  $fail"
    echo "  skipped: $skip"
    if [ "$fail" -gt 0 ]; then
        printf '  - %s\n' "${failed_labels[@]:-}"
        echo ""
        echo "CART MATRIX FAILED"
        return 1
    fi
    if [ "$pass" -eq 0 ]; then
        echo ""
        echo "CART MATRIX: no carts ran (check network / manifest / LEXALOFFLE_CARTS_DIR)"
        return 1
    fi
    echo ""
    echo "CART MATRIX PASSED"
    return 0
}

# ── Main ──

case "$MODE" in
    unit)        run_unit_tests ;;
    vm)          run_vm_tests ;;
    integration) run_integration_test ;;
    vga)         run_vga_test "$@" ;;
    carts)       run_carts_matrix ;;
    all)         run_unit_tests && run_vm_tests && run_integration_test && run_vga_test "$@" ;;
    *)           echo "Usage: $0 [unit|vm|integration|vga|carts|all]" >&2; exit 1 ;;
esac
