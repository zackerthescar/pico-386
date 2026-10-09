#!/usr/bin/env bash
# Test harness for pico-386
#
# Modes:
#   ./test.sh              Run unit tests (TEST.EXE) in QEMU
#   ./test.sh integration  Run integration test (MAIN.EXE + cartridge) in QEMU
#   ./test.sh vga          Run VGA screenshot test (VGATEST.EXE) in QEMU
#   ./test.sh screens      Run end-to-end screen tests (test/carts/*.p8 + .expect)
#   ./test.sh bench        Count instructions per drawing phase (QEMU -icount)
#   ./test.sh prof         Instructions per frame phase in the real games
#                          (PROF.EXE, QEMU -icount, scripted buttons).
#                          PROF_EXE=PROFOPS adds instructions per opcode.
#   ./test.sh games        Run real games (test/games/games.tsv) in MAIN.EXE.
#                          Committed carts and downloaded carts (cache/games/,
#                          git-ignored, filled by tools/fetch_games.sh). Known
#                          failures are XFAIL. Not part of 'all'.
#   ./test.sh carts        Run integration test against every cart in
#                          test/carts.manifest plus optional Lexaloffle carts
#                          dropped under $LEXALOFFLE_CARTS_DIR (see
#                          test/CARTS.md). Carts are fetched into cache/carts/
#                          which is gitignored.
#
# Speed: QEMU stops as soon as the guest prints its completion marker, so
# TIMEOUT only limits a run that hangs. The modes 'all', 'screens', 'games'
# and 'carts' build once, then boot their tests in parallel (JOBS at a time).
# Each parallel job has its own floppy image; its serial log is kept in
# test_logs/<job>.serial.log.
#
# Environment variables:
#   TIMEOUT              QEMU timeout in seconds for a hung run (default: 60)
#   JOBS                 Parallel QEMU runs (default: number of CPUs)
#   CART_URL             URL to a .p8.png cartridge for integration tests
#   CART_NAME            DOS filename stem (default: TESTCART)
#   LEXALOFFLE_CARTS_DIR Optional local directory of *.p8.png carts that may
#                        not be redistributed (proprietary BBS uploads). Used
#                        only by the 'carts' mode.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DOS_DIR="$SCRIPT_DIR/dos"
SERIAL_LOG="$SCRIPT_DIR/test_serial.log"
LOG_DIR="$SCRIPT_DIR/test_logs"
TIMEOUT="${TIMEOUT:-60}"
JOBS="${JOBS:-$(nproc)}"
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

# Serial lines that end a run (grep -E).
TEST_DONE_RE='^# (ALL TESTS PASSED|SOME TESTS FAILED)'    # TEST.EXE, VMTEST.EXE
CART_DONE_RE='Unloading cart'                               # MAIN.EXE

# Wait until a serial line matches done_re, QEMU exits, or TIMEOUT passes.
# Usage: wait_serial <qemu_pid> <done_re>
wait_serial() {
    local pid="$1" done_re="$2" i
    for ((i = 0; i < TIMEOUT * 10; i++)); do
        grep -qE "$done_re" "$SERIAL_LOG" 2>/dev/null && return 0
        kill -0 "$pid" 2>/dev/null || return 1
        sleep 0.1
    done
    return 1
}

# Boot the floppy and stop QEMU when a serial line matches done_re.
# Usage: run_qemu <done_re>
run_qemu() {
    local done_re="$1"
    rm -f "$SERIAL_LOG"
    echo "Starting QEMU (stops at /$done_re/ or after ${TIMEOUT}s)..."
    qemu-system-i386 \
        -drive file="$FLOPPY",format=raw,if=floppy,file.locking=off \
        -serial null \
        -serial file:"$SERIAL_LOG" \
        -display none \
        -m 32 \
        -boot a \
        2>/dev/null &
    local pid=$!
    wait_serial "$pid" "$done_re" || echo "QEMU: no completion marker within ${TIMEOUT}s"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true

    if [ ! -f "$SERIAL_LOG" ] || [ ! -s "$SERIAL_LOG" ]; then
        die "No serial output produced"
    fi
}

# Quiet make: only show errors. Parallel jobs set SKIP_BUILD=1: the driver
# builds everything first, so two makes never run in the same tree.
quiet_make() {
    [ -z "${SKIP_BUILD:-}" ] || return 0
    local output
    output=$(make -C "$SCRIPT_DIR" "$@" 2>&1) || {
        echo "$output" >&2
        die "Build failed"
    }
}

# Run QEMU with monitor socket, wait for serial marker, screendump, quit.
# Usage: run_qemu_screenshot <marker_string> <output_png> [keep_ppm_path]
run_qemu_screenshot() {
    local MARKER="$1"
    local OUTPUT_PNG="$2"
    local KEEP_PPM="${3:-}"
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
        ${QEMU_EXTRA:-} \
        2>/dev/null &
    local QEMU_PID=$!

    # Wait for monitor socket to appear
    local i
    for i in $(seq 1 100); do
        [ -S "$MONITOR_SOCK" ] && break
        sleep 0.05
    done
    [ -S "$MONITOR_SOCK" ] || { kill "$QEMU_PID" 2>/dev/null; die "Monitor socket never appeared"; }

    # Poll serial log for the marker string
    echo "Waiting for '$MARKER' in serial output..."
    if ! wait_serial "$QEMU_PID" "$MARKER"; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
        rm -f "$MONITOR_SOCK"
        die "Marker '$MARKER' never appeared in serial output"
    fi
    sleep 0.3  # let the guest finish the frame it is showing

    # Capture screenshot; wait until the file is complete.
    echo "screendump $SCREENSHOT_PPM" | socat - UNIX-CONNECT:"$MONITOR_SOCK" || true
    local size=-1 now
    for i in $(seq 1 50); do
        now=$(stat -c %s "$SCREENSHOT_PPM" 2>/dev/null || echo 0)
        [ "$now" -gt 0 ] && [ "$now" = "$size" ] && break
        size=$now
        sleep 0.05
    done

    # Quit QEMU
    echo "quit" | socat - UNIX-CONNECT:"$MONITOR_SOCK" || true
    wait "$QEMU_PID" 2>/dev/null || true
    rm -f "$MONITOR_SOCK"

    [ -f "$SCREENSHOT_PPM" ] || die "screendump did not produce $SCREENSHOT_PPM"

    # Convert PPM -> PNG
    convert "$SCREENSHOT_PPM" "$OUTPUT_PNG"
    [ -z "$KEEP_PPM" ] || cp "$SCREENSHOT_PPM" "$KEEP_PPM"
    rm -f "$SCREENSHOT_PPM"

    echo "Screenshot saved: $OUTPUT_PNG"
}

# ── Parallel jobs ──
#
# A job is a shell function run in a background subshell. Each job has its
# own floppy image and serial log, so jobs do not share files. Its output is
# kept in a file and printed in start order by finish_jobs.

JOB_IDS=()

# Usage: start_job <id> <function> [args...]
start_job() {
    local id="$1"; shift
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n || true; done
    JOB_IDS+=("$id")
    (
        FLOPPY="$WORK_DIR/$id.img"
        SERIAL_LOG="$LOG_DIR/$id.serial.log"
        SKIP_BUILD=1
        set +e
        ( set -e; "$@" ) > "$WORK_DIR/$id.out" 2>&1
        echo $? > "$WORK_DIR/$id.rc"
    ) &
}

# Wait for all jobs and print their output in start order. Sets JOB_RC[id].
declare -A JOB_RC
finish_jobs() {
    wait
    local id
    for id in "${JOB_IDS[@]}"; do
        cat "$WORK_DIR/$id.out"
        JOB_RC[$id]=$(cat "$WORK_DIR/$id.rc" 2>/dev/null || echo 1)
    done
}

# Prepare a parallel run: shared downloads first, private work dir.
init_jobs() {
    ensure_freedos
    mkdir -p "$LOG_DIR"
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/p386-test.XXXXXX")"
    trap 'rm -rf "$WORK_DIR"' EXIT
    JOB_IDS=()
    JOB_RC=()
}

# ── Unit tests ──

run_unit_tests() {
    echo "=== Building unit tests ==="
    quiet_make test

    [ -f "$DOS_DIR/TEST.EXE" ] || die "Build did not produce TEST.EXE"

    echo "=== Booting TEST.EXE in QEMU ==="
    build_floppy "$(printf '@echo off\r\nTEST.EXE\r\n')" \
        "$DOS_DIR/TEST.EXE::TEST.EXE"
    run_qemu "$TEST_DONE_RE"

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
    run_qemu "$TEST_DONE_RE"

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
    build_floppy "$(printf '@echo off\r\nset P386_FRAMES=3\r\nMAIN.EXE %s.P8\r\n' "$CART_NAME")" \
        "$DOS_DIR/MAIN.EXE::MAIN.EXE" \
        "$CART_PNG::$CART_NAME.P8"
    run_qemu "$CART_DONE_RE"

    echo ""
    echo "=== Serial output (COM2) ==="
    cat "$SERIAL_LOG"
    echo ""

    echo "=== Validation ==="
    local PASS=true

    if grep -q "Going into VGA\|Initializing VGA" "$SERIAL_LOG"; then
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

    if grep -q "main chunk failed:" "$SERIAL_LOG"; then
        echo "  [FAIL] Runtime failed while executing main chunk"
        grep "main chunk failed:" "$SERIAL_LOG" | sed 's/^/         /'
        PASS=false
    elif grep -q "_init failed:\|_update failed:\|_update60 failed:\|_draw failed:" "$SERIAL_LOG"; then
        echo "  [FAIL] Runtime failed during lifecycle callback"
        grep "_init failed:\|_update failed:\|_update60 failed:\|_draw failed:" "$SERIAL_LOG" | sed 's/^/         /'
        PASS=false
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
VGA_TEST_HASH="a4d89c4ce83e45b2a08e94215b2536d3c66d3140d4ecd754e9cebaa7a13708a5"

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

# ── End-to-end screen tests ──
#
# Each test/carts/NAME.p8 with a NAME.expect is built into a .p8.png with
# tools/mkcart.py, run in MAIN.EXE for a few frames, held on screen
# (P386_HOLD), and its QEMU screendump is checked against the pixel probes
# in NAME.expect (tools/checkshot.py).

run_screen_test() {
    local src="$1" name stem cart ppm png
    name="$(basename "$src" .p8)"
    stem="$(echo "$name" | tr '[:lower:]' '[:upper:]' | tr -c 'A-Z0-9_\n' '_' | cut -c1-8)"
    cart="$(mktemp "${TMPDIR:-/tmp}/p386-screen.XXXXXX.png")"
    ppm="$SCRIPT_DIR/test_screen_$name.ppm"
    png="$SCRIPT_DIR/test_screen_$name.png"

    echo "── $name ─────────────────────────────────────"
    python3 -I "$SCRIPT_DIR/tools/mkcart.py" "$src" "$cart" || { echo "  [FAIL] mkcart"; return 1; }
    build_floppy "$(printf '@echo off\r\nset P386_FRAMES=2\r\nset P386_HOLD=1\r\nMAIN.EXE %s.P8\r\n' "$stem")" \
        "$DOS_DIR/MAIN.EXE::MAIN.EXE" \
        "$cart::$stem.P8"
    rm -f "$ppm"
    run_qemu_screenshot "P386_HOLD" "$png" "$ppm" >/dev/null
    rm -f "$cart"

    if grep -q "failed:" "$SERIAL_LOG"; then
        grep "failed:" "$SERIAL_LOG" | sed 's/^/  /'
        echo "  [FAIL] runtime error"
        return 1
    fi
    if python3 -I "$SCRIPT_DIR/tools/checkshot.py" "$ppm" "${src%.p8}.expect"; then
        return 0
    fi
    echo "  screenshot: $png"
    return 1
}

run_screen_tests() {
    echo "=== Building MAIN.EXE ==="
    quiet_make pico
    [ -f "$DOS_DIR/MAIN.EXE" ] || die "Build did not produce MAIN.EXE"

    local src failed=0 ran=0 id
    init_jobs
    for src in "$SCRIPT_DIR"/test/carts/*.p8; do
        [ -f "${src%.p8}.expect" ] || continue
        ran=$((ran + 1))
        start_job "screen_$(basename "$src" .p8)" run_screen_test "$src"
    done
    finish_jobs
    for id in "${JOB_IDS[@]}"; do
        [ "${JOB_RC[$id]}" = 0 ] || failed=$((failed + 1))
    done
    echo ""
    if [ "$ran" -eq 0 ]; then
        echo "SCREEN TESTS: none found"
        return 1
    fi
    if [ "$failed" -gt 0 ]; then
        echo "SCREEN TESTS FAILED ($failed of $ran)"
        return 1
    fi
    echo "SCREEN TESTS PASSED ($ran)"
    return 0
}

# ── Drawing benchmark ──
#
# BENCH.EXE prints instruction counts per drawing phase. QEMU runs with
# -icount so the guest TSC counts instructions (see bench_gfx_main.c).
# Booting under -icount is slow: use TIMEOUT=180 or more (QEMU stops as soon
# as BENCH_DONE shows, so a long TIMEOUT costs nothing).

run_bench() {
    echo "=== Building BENCH.EXE ==="
    quiet_make bench
    build_floppy "$(printf '@echo off\r\nBENCH.EXE\r\n')" "$DOS_DIR/BENCH.EXE::BENCH.EXE"
    rm -f "$SERIAL_LOG"
    qemu-system-i386 -cpu pentium -icount shift=0,sleep=off \
        -drive file="$FLOPPY",format=raw,if=floppy,file.locking=off \
        -serial null -serial file:"$SERIAL_LOG" -display none -m 32 -boot a \
        2>/dev/null &
    local QEMU_PID=$! i
    # -icount runs slowly; stop QEMU as soon as the results are in.
    wait_serial "$QEMU_PID" BENCH_DONE || true
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
    grep -q BENCH_DONE "$SERIAL_LOG" || die "benchmark did not finish (TIMEOUT=$TIMEOUT)"
    grep '^BENCH ' "$SERIAL_LOG"
}

# ── Real-game tests ──
#
# For each row of test/games/games.tsv: boot the game for N frames, hold the
# last frame (P386_HOLD), and take a screendump. A game passes when
#   - serial shows no VM or loader error,
#   - the run finished (P386_HOLD marker; "Unloading cart" cannot show
#     because HOLD keeps the program running for the screendump),
#   - the screendump has more than one colour.
# A row with an xfail text is an expected failure: the exact line must show
# on serial. Result is XFAIL (as expected), XPASS (game works now: update the
# manifest), or FAIL.

GAMES_MANIFEST="${GAMES_MANIFEST:-$SCRIPT_DIR/test/games/games.tsv}"
GAMES_CACHE_DIR="${GAMES_CACHE_DIR:-$SCRIPT_DIR/cache/games}"

# Print the error lines of the serial log (empty when there are none).
game_errors() {
    grep -E "failed:|^p386_vm_load:|p8_compile: FAIL|pico8_decomp: no code" "$SERIAL_LOG" || true
}

# Usage: run_one_game <name> <frames> <xfail>   (prints result; returns 0 = ok)
run_one_game() {
    local name="$1" frames="$2" xfail="$3"
    [ "$xfail" != "-" ] || xfail=""
    local cart="$GAMES_CACHE_DIR/$name.p8.png"
    local ppm="$SCRIPT_DIR/test_game_$name.ppm" png="$SCRIPT_DIR/test_game_$name.png"
    local errs="" why="" colors=0

    echo "── $name ($frames frames) ─────────────────────────────────────"
    if [ ! -f "$cart" ]; then
        echo "  [FAIL] $name: cart missing ($cart); run tools/fetch_games.sh"
        return 1
    fi

    build_floppy "$(printf '@echo off\r\nset P386_FRAMES=%s\r\nset P386_HOLD=1\r\nMAIN.EXE %s.P8\r\n' "$frames" "$name")" \
        "$DOS_DIR/MAIN.EXE::MAIN.EXE" \
        "$cart::$name.P8"
    rm -f "$ppm"
    # run_qemu_screenshot calls die when the marker never shows: keep it in a subshell.
    ( run_qemu_screenshot "P386_HOLD" "$png" "$ppm" ) >/dev/null 2>&1 || true

    errs="$(game_errors)"
    if [ -n "$errs" ]; then
        why="$errs"
    elif ! grep -q "P386_HOLD" "$SERIAL_LOG" 2>/dev/null; then
        why="run did not finish within ${TIMEOUT}s (hang or too slow)"
    elif [ ! -f "$ppm" ]; then
        why="no screendump"
    elif ! colors="$(python3 -I "$SCRIPT_DIR/tools/shotcolors.py" "$ppm")"; then
        why="blank screen (1 colour)"
    fi

    if [ -z "$why" ]; then
        if [ -n "$xfail" ]; then
            echo "  [XPASS] $name works now ($colors colours): set its xfail field to - in games.tsv"
            return 2
        fi
        echo "  [PASS] $name ($colors colours)"
        rm -f "$ppm" "$png"
        return 0
    fi

    echo "$why" | head -n 5 | sed 's/^/    /'
    if [ -n "$xfail" ] && printf '%s\n' "$why" | grep -qF -- "$xfail"; then
        echo "  [XFAIL] $name (expected: $xfail)"
        rm -f "$ppm" "$png"
        return 3
    fi
    [ -z "$xfail" ] || echo "  expected XFAIL text not found: $xfail"
    echo "  [FAIL] $name   screenshot: $png"
    return 1
}

run_games_tests() {
    echo "=== Building MAIN.EXE ==="
    quiet_make pico
    [ -f "$DOS_DIR/MAIN.EXE" ] || die "Build did not produce MAIN.EXE"

    echo "=== Fetching games ==="
    "$SCRIPT_DIR/tools/fetch_games.sh" || die "fetch_games.sh failed (hash mismatch or download error)"

    local name src sha lic author page frames xfail note
    local pass=0 fail=0 xfailed=0 xpass=0 rc id
    local failed_names=() xpass_names=()
    init_jobs
    while IFS=$'\t' read -r name src sha lic author page frames xfail note || [ -n "${name:-}" ]; do
        name="${name%$'\r'}"
        case "$name" in ''|'#'*) continue ;; esac
        # GAMES="A B" runs only these games.
        [ -z "${GAMES:-}" ] || [[ " $GAMES " == *" $name "* ]] || continue
        start_job "game_$name" run_one_game "$name" "${frames:-90}" "${xfail:--}"
    done < "$GAMES_MANIFEST"
    finish_jobs
    for id in "${JOB_IDS[@]}"; do
        name="${id#game_}"
        rc="${JOB_RC[$id]}"
        case "$rc" in
            0) pass=$((pass+1)) ;;
            2) xpass=$((xpass+1)); xpass_names+=("$name") ;;
            3) xfailed=$((xfailed+1)) ;;
            *) fail=$((fail+1)); failed_names+=("$name") ;;
        esac
    done

    echo ""
    echo "=== Game summary ==="
    echo "  pass: $pass   xfail: $xfailed   xpass: $xpass   fail: $fail"
    [ "$xpass" -eq 0 ] || printf '  XPASS (update games.tsv): %s\n' "${xpass_names[*]}"
    if [ "$fail" -gt 0 ]; then
        printf '  FAIL: %s\n' "${failed_names[*]}"
        echo "GAME TESTS FAILED"
        return 1
    fi
    echo "GAME TESTS PASSED"
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

    build_floppy "$(printf '@echo off\r\nset P386_FRAMES=3\r\nMAIN.EXE %s.P8\r\n' "$dos_stem")" \
        "$DOS_DIR/MAIN.EXE::MAIN.EXE" \
        "$cart_png::$dos_stem.P8"
    run_qemu "$CART_DONE_RE"

    local ok=true
    grep -q "Going into VGA\|Initializing VGA" "$SERIAL_LOG" || { echo "  [FAIL] no serial init"; ok=false; }
    grep -q "Loading cart"     "$SERIAL_LOG" || { echo "  [FAIL] cart load did not start"; ok=false; }
    if grep -q "pico8_decomp: no code" "$SERIAL_LOG"; then
        echo "  [FAIL] decompression produced no code"; ok=false
    fi
    grep -q "Lua code:" "$SERIAL_LOG" || { echo "  [FAIL] no Lua decompressed"; ok=false; }
    if grep -q "p8_compile: FAIL" "$SERIAL_LOG"; then
        echo "  [FAIL] compiler rejected code"; ok=false
    fi
    if grep -q "main chunk failed:\|_init failed:\|_update failed:\|_update60 failed:\|_draw failed:" "$SERIAL_LOG"; then
        echo "  [FAIL] runtime error during cart execution"; ok=false
    fi
    # The frame loop runs until Esc unless P386_FRAMES is set; a hang here
    # means the loop never finished its frames.
    grep -q "Unloading cart" "$SERIAL_LOG" || { echo "  [FAIL] no clean shutdown (hung?)"; ok=false; }

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

    local pass=0 fail=0 skip=0 id
    local failed_labels=()
    local -A labels=()
    init_jobs

    # Permissive carts from manifest
    if [ -f "$CART_MANIFEST" ]; then
        while IFS=$'\t' read -r dos_name url sha license source; do
            dos_name="${dos_name%$'\r'}"
            [ -z "${dos_name:-}" ] && continue
            case "$dos_name" in \#*) continue ;; esac
            local cart="$CART_CACHE_DIR/$dos_name.p8.png"
            local label="manifest:$dos_name [$license]"
            labels["cart_$dos_name"]="$label"
            start_job "cart_$dos_name" run_one_cart "$label" "$cart" "$dos_name"
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
            labels["lexa_$stem"]="$label"
            start_job "lexa_$stem" run_one_cart "$label" "$f" "$stem"
        done
    else
        echo "=== Lexaloffle carts: skipped (set LEXALOFFLE_CARTS_DIR to enable) ==="
    fi

    finish_jobs
    for id in "${JOB_IDS[@]}"; do
        case "${JOB_RC[$id]}" in
            0) pass=$((pass+1)) ;;
            2) skip=$((skip+1)) ;;
            *) fail=$((fail+1)); failed_labels+=("${labels[$id]}") ;;
        esac
    done

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

# ── Frame profile of the real games ──
#
# PROF.EXE (MAIN.EXE built with P386_PROF) runs each game under QEMU -icount,
# where the TSC counts guest instructions. Scripted buttons get past title
# screens: O+X a few times, then hold right and jump now and then. Frames
# up to PROF_SKIP are not measured. A screendump of the last frame is kept
# (test_prof_NAME.png) to check that the game was really playing.

PROF_FRAMES="${PROF_FRAMES:-240}"
PROF_SKIP="${PROF_SKIP:-80}"
PROF_KEYS="${PROF_KEYS:-20-23:48,40-43:48,60-63:48,80-999:2,90-91:32,120-121:32,150-151:32,180-181:32,210-211:32}"

run_one_prof() {
    local name="$1" exe="${PROF_EXE:-PROF}"
    local cart="$GAMES_CACHE_DIR/$name.p8.png" png="$SCRIPT_DIR/test_prof_$name.png"
    [ -f "$cart" ] || { echo "  [FAIL] $name: cart missing"; return 1; }
    build_floppy "$(printf '@echo off\r\nset P386_FRAMES=%s\r\nset P386_PROF=%s\r\nset P386_KEYS=%s\r\nset P386_HOLD=1\r\nPROF.EXE %s.P8\r\n' \
                    "$PROF_FRAMES" "$PROF_SKIP" "$PROF_KEYS" "$name")" \
        "$DOS_DIR/$exe.EXE::PROF.EXE" \
        "$cart::$name.P8"
    QEMU_EXTRA="-cpu pentium -icount shift=0,sleep=off"
    ( run_qemu_screenshot "P386_HOLD" "$png" ) >/dev/null 2>&1 || true
    echo "── $name ─────────────────────────────────────"
    if ! grep -q PROF_DONE "$SERIAL_LOG" 2>/dev/null; then
        game_errors | sed 's/^/    /'
        echo "  [FAIL] $name: no profile (serial: $SERIAL_LOG)"
        return 1
    fi
    grep '^PROF ' "$SERIAL_LOG" | sed 's/^PROF /  /'
    [ "$exe" = PROF ] || python3 -I "$SCRIPT_DIR/tools/profops.py" "$SERIAL_LOG"
    echo "  screenshot: $png"
}

run_prof() {
    echo "=== Building ${PROF_EXE:-PROF}.EXE ==="
    if [ "${PROF_EXE:-PROF}" = PROF ]; then quiet_make prof; else quiet_make profops; fi
    "$SCRIPT_DIR/tools/fetch_games.sh" || die "fetch_games.sh failed"
    local name rest id failed=0
    init_jobs
    while IFS=$'\t' read -r name rest || [ -n "${name:-}" ]; do
        name="${name%$'\r'}"
        case "$name" in ''|'#'*) continue ;; esac
        [ -z "${GAMES:-}" ] || [[ " $GAMES " == *" $name "* ]] || continue
        start_job "prof_$name" run_one_prof "$name"
    done < "$GAMES_MANIFEST"
    finish_jobs
    for id in "${JOB_IDS[@]}"; do [ "${JOB_RC[$id]}" = 0 ] || failed=$((failed + 1)); done
    [ "$failed" -eq 0 ] || { echo "PROF: $failed FAILED"; return 1; }
    echo "PROF DONE"
}

# ── All tests, in parallel ──

run_all() {
    echo "=== Building everything ==="
    quiet_make test vm-test pico vga-test
    init_jobs
    start_job unit run_unit_tests
    start_job vm run_vm_tests
    start_job integration run_integration_test
    start_job vga run_vga_test "$@"
    local src id failed=()
    for src in "$SCRIPT_DIR"/test/carts/*.p8; do
        [ -f "${src%.p8}.expect" ] || continue
        start_job "screen_$(basename "$src" .p8)" run_screen_test "$src"
    done
    finish_jobs

    echo ""
    echo "=== Summary ==="
    for id in "${JOB_IDS[@]}"; do
        if [ "${JOB_RC[$id]}" = 0 ]; then
            echo "  [PASS] $id"
        else
            echo "  [FAIL] $id (serial log: test_logs/$id.serial.log)"
            failed+=("$id")
        fi
    done
    if [ "${#failed[@]}" -gt 0 ]; then
        echo "ALL: ${#failed[@]} FAILED"
        return 1
    fi
    echo "ALL TESTS PASSED (${#JOB_IDS[@]} jobs)"
}

# ── Main ──

case "$MODE" in
    unit)        run_unit_tests ;;
    vm)          run_vm_tests ;;
    integration) run_integration_test ;;
    vga)         run_vga_test "$@" ;;
    carts)       run_carts_matrix ;;
    screens|sprites) run_screen_tests ;;
    games)       run_games_tests ;;
    bench)       run_bench ;;
    prof)        run_prof ;;
    all)         run_all "$@" ;;
    *)           echo "Usage: $0 [unit|vm|integration|vga|carts|screens|games|bench|prof|all]" >&2; exit 1 ;;
esac
