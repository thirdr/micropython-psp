# Clock: the date and time from the PSP's clock, in its time zone.
# Press START to finish.
import time

import psp

DAYS = ("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")

print("START finishes.")
last = None
while psp.START not in psp.pressed():
    t = time.localtime()
    if t[5] != last:
        year, month, day, hour, minute, second, weekday, _ = t
        print("{} {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}".format(
            DAYS[weekday], year, month, day, hour, minute, second))
        last = t[5]
    psp.vsync()
