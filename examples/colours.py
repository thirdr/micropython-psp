# Colours: the console's 16 colours, and a timer redrawn in place, with the
# ansi module's codes in print(). Press START to finish.
import time

import ansi
import psp

NAMES = ("BLACK", "RED", "GREEN", "YELLOW", "BLUE", "MAGENTA", "CYAN", "WHITE")

ansi.clear()
print(ansi.BRIGHT + "The console's colours" + ansi.RESET)
print()
print("normal    bright    background")
for name in NAMES:
    normal = getattr(ansi, name) + "{:<10}".format(name.lower())
    bright = getattr(ansi, "BRIGHT_" + name) + "{:<10}".format(name.lower())
    background = getattr(ansi, "BG_" + name) + " " * 10
    print(normal + bright + background + ansi.RESET)

ansi.move(0, 13)
print("Running for")
ansi.move(0, 15)
print(ansi.REVERSE + " START finishes " + ansi.RESET)

# Only when the time shown changes: print() also goes to the logs, and to
# the Mac over PSPLINK.
start = time.ticks_ms()
shown = None
while psp.START not in psp.pressed():
    text = "{:.1f} s".format(time.ticks_diff(time.ticks_ms(), start) / 1000)
    if text != shown:
        ansi.move(12, 13)
        print(ansi.BRIGHT_YELLOW + text + ansi.RESET, end="")
        ansi.clear_line()
        shown = text
    psp.vsync()

ansi.move(0, 17)
