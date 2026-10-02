# Writes the REPL test's keystrokes, as a terminal would send them, to
# repl-input.txt in the given folder. A test build that finds that file next
# to it runs the REPL on it instead of the keyboard. A 0 byte pauses half a
# second, so a Ctrl-C arrives while code is running. As CI runs it:
#   python3 tests/repl/make_input.py build/repl-test
#   tools/run-ppsspp.sh --files build/repl-test build/psp-test/EBOOT.PBP 60
#   python3 tests/repl/check.py build/logs/latest/stdout.log
import os
import sys

CTRL_A, CTRL_B, CTRL_C, CTRL_D, CTRL_E = "\x01", "\x02", "\x03", "\x04", "\x05"
PAUSE = "\x00"

steps = [
    "1+1\r",
    # Auto-indent adds the body's indentation, and keeps it for two blank
    # lines; the third Enter gives an empty line, which ends the block.
    "def f():\rreturn 41\r\r\r\rf() + 1\r",
    # Backspace while editing: the line runs as 1+4.
    "1+3\b4\r",
    # Ctrl-C stops a running loop.
    "while True:\rpass\r\r\r\r" + PAUSE + CTRL_C,
    # Paste mode.
    CTRL_E + "for i in range(3):\n    print('paste', i)\n" + CTRL_D,
    # Raw REPL, as mpremote uses it, then back.
    CTRL_A + "print('raw', 6 * 7)" + CTRL_D + CTRL_B,
    # mpremote's start: Ctrl-C, raw REPL, soft reset there, then run. The
    # pause lets the steps before finish first: input arrives faster than any
    # keyboard, and an early Ctrl-C would interrupt them.
    PAUSE + "\r" + CTRL_C + "\r" + CTRL_A + CTRL_D + "print('after reset')" + CTRL_D + CTRL_B,
    # Soft reset forgets variables.
    "x = 5\r" + CTRL_D + "x\r",
    "print('repl done')\r",
]

os.makedirs(sys.argv[1], exist_ok=True)
with open(os.path.join(sys.argv[1], "repl-input.txt"), "w", newline="") as f:
    f.write("".join(steps))
