#!/usr/bin/env bash
# Run an EBOOT/ELF in PPSSPPHeadless, show its stdout live, and save a
# screenshot of the PSP screen.
#
# Usage: tools/run-ppsspp.sh [--files DIR] path/to/EBOOT.PBP [timeout-seconds]
#
# Each run goes in build/logs/<name>-<timestamp>/ (build/logs/latest points
# at the newest), containing:
#   stdout.log   everything the program printed
#   screen.png   the screen at the program's last psp_emu_screenshot() call
#   app/         with --files: the EBOOT plus DIR's contents, run from there
#
# --files DIR stages scripts next to the EBOOT. PPSSPP runs an EBOOT that's
# outside a PSP/GAME folder with its own folder as the working directory
# (umd0:/), so DIR's main.py, lib/ etc. are what the port finds, just as on a
# PSP. Anything the program writes stays in app/ for inspection.
#
# The exit status is PPSSPPHeadless's, except that a timeout is reported as
# 124 (matching coreutils' timeout).
#
# Set PPSSPP_HEADLESS to override the path to the PPSSPPHeadless binary.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADLESS="${PPSSPP_HEADLESS:-$REPO/../ppsspp/build-headless/PPSSPPHeadless}"

files=""
if [ "${1:-}" = "--files" ]; then
    files="${2:?--files needs a directory}"
    shift 2
    if [ ! -d "$files" ]; then
        echo "no such directory: $files" >&2
        exit 2
    fi
fi

if [ $# -lt 1 ]; then
    echo "usage: $0 [--files DIR] path/to/EBOOT.PBP [timeout-seconds]" >&2
    exit 2
fi

target="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
timeout="${2:-10}"

if [ ! -x "$HEADLESS" ]; then
    echo "PPSSPPHeadless not found at $HEADLESS (set PPSSPP_HEADLESS)" >&2
    exit 2
fi
if [ ! -f "$target" ]; then
    echo "no such file: $target" >&2
    exit 2
fi

name="$(basename "$(dirname "$target")")"
[ -n "$files" ] && name="$name-$(basename "$(cd "$files" && pwd)")"
logdir="$REPO/build/logs/$name-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$logdir"

if [ -n "$files" ]; then
    mkdir -p "$logdir/app"
    cp -R "$files/." "$logdir/app/"
    cp "$target" "$logdir/app/EBOOT.PBP"
    target="$logdir/app/EBOOT.PBP"
fi
ln -sfn "$logdir" "$REPO/build/logs/latest"

# PPSSPPHeadless can only save a screenshot when a comparison "fails". Compare
# against a blank reference with a negative error threshold, so every capture
# is written out as __testfailure.bmp in the working directory. (The reference
# has to exist: PPSSPPHeadless crashes after the capture if it can't load one.)
# It also skips writing the file when GITHUB_ACTIONS is set, so unset that.
reference="$REPO/build/logs/.blank-reference.bmp"
[ -f "$reference" ] || python3 "$REPO/tools/ppsspp_screen.py" reference "$reference"
set +e
(
    cd "$logdir"
    env -u GITHUB_ACTIONS "$HEADLESS" \
        --graphics=software \
        --timeout="$timeout" \
        --screenshot="$reference" \
        --max-mse=-10 \
        "$target"
) 2>&1 | grep --line-buffered -vE '^(Screenshot MSE: |Actual output written to: |Unable to read screenshot)' \
       | tee "$logdir/stdout.log"
status=${PIPESTATUS[0]}
set -e

if grep -q "TIMEOUT" "$logdir/stdout.log" 2>/dev/null; then
    status=124
fi

if [ -f "$logdir/__testfailure.bmp" ]; then
    python3 "$REPO/tools/ppsspp_screen.py" png "$logdir/__testfailure.bmp" "$logdir/screen.png"
    rm -f "$logdir/__testfailure.bmp" "$logdir/__testcompare.png"
fi

echo "--- log: $logdir (exit $status)" >&2
[ -f "$logdir/screen.png" ] && echo "--- screen: $logdir/screen.png" >&2
exit "$status"
