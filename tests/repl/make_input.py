# Writes tests/repl/repl-input.txt: keystrokes for the REPL test, as a
# terminal would send them. A 0 byte pauses half a second (test builds only),
# so a Ctrl-C arrives while code is running. Run after changing the steps.
import os

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
    # mpremote's start: Ctrl-C, raw REPL, soft reset there, then run.
    "\r" + CTRL_C + "\r" + CTRL_A + CTRL_D + "print('after reset')" + CTRL_D + CTRL_B,
    # Soft reset forgets variables.
    "x = 5\r" + CTRL_D + "x\r",
    "print('repl done')\r",
]

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "repl-input.txt")
with open(path, "w", newline="") as f:
    f.write("".join(steps))
