#!/usr/bin/env bash
# MicroPython's REPL on a real PSP, over USB with PSPLINK, opened in mpremote.
#
# Usage: tools/psp-repl.sh [--attach | --prx PATH] [mpremote command...]
#   tools/psp-repl.sh                  the >>> prompt (Ctrl-] quits mpremote)
#   tools/psp-repl.sh run script.py    run a script from the Mac
#   tools/psp-repl.sh --attach         connect to a REPL that's already running
#   tools/psp-repl.sh --prx ms0:/PSP/GAME/MicroPython/micropython.prx
#                                      start the copy on the memory stick (from
#                                      the release zip), in its own folder
#
# Needs PSPLINK running on the PSP, connected by USB, and usbhostfs_pc running
# on the Mac. By default it starts micropython.prx from usbhostfs_pc's folder,
# which the PSP sees as host0:/ (for example: cd build/psp && usbhostfs_pc).
# PSPLINK can't start the EBOOT.PBP itself. It starts it in REPL mode through
# pspsh, then connects mpremote to the PSP's stdin and stdout.
# usbhostfs_pc serves those on port 10002 (PSP_STDIO_PORT; 2 more than
# usbhostfs_pc's -b base port), and tools/psp-tty.py turns that into a
# terminal device, since mpremote's REPL can't use a TCP connection.
set -euo pipefail

export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PATH:$PSPDEV/bin"
TOOLS="$(cd "$(dirname "$0")" && pwd)"
PORT="${PSP_STDIO_PORT:-10002}"
TMP="${TMPDIR:-/tmp}"
LINK="${TMP%/}/psp-tty"

PRX="micropython.prx"
ATTACH=0
case "${1:-}" in
    --attach) ATTACH=1; shift ;;
    --prx) PRX="$2"; shift 2 ;;
esac

if [ $ATTACH = 0 ]; then
    # A copy left running (leaving mpremote doesn't stop it) holds nearly all
    # the PSP's memory, so another won't load (0x800200D9). Restarting PSPLINK
    # frees it; pspsh's next command waits until PSPLINK is back (about 4 s).
    if pspsh -n -e modlist 2>&1 | grep -q "Name: MicroPython"; then
        echo "psp-repl: restarting PSPLINK to stop the MicroPython already running"
        pspsh -n -e reset > /dev/null
        pspsh -n -e ver > /dev/null
    fi
    # pspsh reports a failed start but still exits with 0.
    started="$(pspsh -n -e "ldstart $PRX repl" 2>&1)"
    echo "$started"
    if [[ "$started" != *"Load/Start"*"UID"* ]]; then
        echo "psp-repl: MicroPython didn't start on the PSP" >&2
        exit 1
    fi
fi

"$TOOLS/psp-tty.py" --port "$PORT" --link "$LINK" > /dev/null &
bridge=$!
trap 'kill $bridge 2>/dev/null; wait $bridge 2>/dev/null || true' EXIT

# The bridge makes the link once it has reached usbhostfs_pc.
for _ in $(seq 50); do
    [ -e "$LINK" ] && break
    if ! kill -0 $bridge 2>/dev/null; then
        echo "psp-repl: tools/psp-tty.py stopped" >&2
        exit 1
    fi
    sleep 0.2
done
if [ ! -e "$LINK" ]; then
    echo "psp-repl: can't reach port $PORT: is usbhostfs_pc running?" >&2
    exit 1
fi

if [ $# -eq 0 ]; then
    set -- repl
fi
mpremote connect "$LINK" "$@"
