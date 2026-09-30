#!/usr/bin/env bash
# Run MicroPython's own test suite (micropython/tests) on the PSP port in
# PPSSPPHeadless.
#
# Usage: tools/run-mp-tests.sh [run-tests.py options, e.g. -j4 or test files]
#
# tests/run-tests.py runs each test through tools/psp-micropython, one
# PPSSPPHeadless run per test, in place of the unix binary, and compares the
# output with CPython's. Results (.exp/.out for failures) go in
# build/mp-tests/results; the full log is build/mp-tests/run.log.
#
# Set PSP_EBOOT to test another EBOOT (default build/psp/EBOOT.PBP), and
# PPSSPP_HEADLESS for the PPSSPPHeadless binary.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/build/mp-tests"

# Tests that can't pass on the PSP. Each is a run-tests.py exclude regex.
EXPECTED_FAILURES=(
    # The unix binary's command-line options; the PSP has no command line.
    '^cmdline/'
    # PPSSPP lists lowercase 8.3 names in upper case ("test2" lists as
    # "TEST2"). A real PSP-1000 lists files copied from a Mac in lower case
    # (checked 2026-09-30); files created on the PSP itself are unchecked.
    '^extmod/vfs_posix\.py$'
    '^extmod/vfs_posix_ilistdir_filter\.py$'
    # The PSP keeps the working directory as a string, so getcwd() still
    # succeeds after that folder is removed.
    '^extmod/vfs_posix_enoent\.py$'
    # VfsPosix(root) treats a root that doesn't start with "/" as relative,
    # and PSP paths start with a drive ("umd0:/..."). Only affects VfsPosix
    # objects made with a root; the default mount is fine.
    '^extmod/vfs_posix_paths\.py$'
)

excludes=()
for re in "${EXPECTED_FAILURES[@]}"; do
    excludes+=(-e "$re")
done

mkdir -p "$OUT"
rm -rf "$OUT/results"
cd "$REPO/micropython/tests"
set +e
MICROPY_MICROPYTHON="$REPO/tools/psp-micropython" \
    python3 run-tests.py -r "$OUT/results" "${excludes[@]}" "$@" 2>&1 | tee "$OUT/run.log"
status=${PIPESTATUS[0]}
set -e
if [ "$status" -ne 0 ]; then
    echo "--- failures: $OUT/results (python3 run-tests.py -r $OUT/results --print-failures)" >&2
fi
exit "$status"
