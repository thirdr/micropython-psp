# Analog stick: draws the stick position as a bar for each axis.
# Readings near the centre are 0 (the dead zone). Press START to finish.
import psp

WIDTH = 25  # characters either side of the centre


def bar(v):
    n = abs(v) * WIDTH // 127
    left = " " * WIDTH if v >= 0 else " " * (WIDTH - n) + "#" * n
    right = "#" * n + " " * (WIDTH - n) if v > 0 else " " * WIDTH
    return left + "|" + right


print("Move the stick. START finishes.")
last = None
while psp.START not in psp.pressed():
    xy = psp.analog()
    if xy != last:
        print("x {:4d} {}".format(xy[0], bar(xy[0])))
        print("y {:4d} {}".format(xy[1], bar(xy[1])))
        last = xy
    psp.vsync()
