#!/usr/bin/env bash
# Open an EBOOT/ELF in the PPSSPP window, and show the program's stdout in
# this terminal.
#
# Usage: tools/run-gui.sh path/to/EBOOT.PBP
#
# Closing the window (or HOME -> Exit in the program) ends the run. The
# emulator log is kept in build/logs/gui-latest.log, and PPSSPP's own console
# chatter in build/logs/gui-console.log.
#
# PPSSPP logs the program's stdout at INFO level on its PRINTF channel. If
# that channel is set lower in ppsspp.ini (Settings -> Tools -> Developer
# tools -> Logging channels), no output appears here; the script warns.
#
# Set PPSSPP_GUI to override the path to the PPSSPP binary.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GUI="${PPSSPP_GUI:-/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL}"
INI="${PPSSPP_INI:-$HOME/.config/ppsspp/PSP/SYSTEM/ppsspp.ini}"

if [ $# -lt 1 ]; then
    echo "usage: $0 path/to/EBOOT.PBP" >&2
    exit 2
fi

target="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
log="$REPO/build/logs/gui-latest.log"
console="$REPO/build/logs/gui-console.log"
mkdir -p "$(dirname "$log")"
: > "$log"

if [ -f "$INI" ]; then
    level="$(sed -n -E 's/^PRINTFLevel = ([0-9]+).*/\1/p' "$INI")"
    if [ -n "$level" ] && [ "$level" -lt 4 ]; then
        echo "note: PRINTFLevel is $level in $INI; set it to 4 (Info) to see program output here" >&2
    fi
fi

"$GUI" --windowed --pause-menu-exit --escape-exit --log="$log" "$target" > "$console" 2>&1 &
gui_pid=$!

# PPSSPP logs each write to stdout/stderr as "...stdout: <text>"; show the
# text. tail runs on its own (not as part of a pipeline) so $! is its PID and
# it can be stopped afterwards.
tail -n +1 -F "$log" 2> /dev/null > >(grep --line-buffered -E ' (stdout|stderr): ' \
    | sed -u -E 's/^.* (stdout|stderr): //') &
tail_pid=$!

wait "$gui_pid" || true
sleep 0.5
kill "$tail_pid" 2> /dev/null || true
