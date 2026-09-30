# Hello from the PSP: versions, clock speed and battery.
import sys

import psp

print("Hello from MicroPython on the PSP!")
print()
print("MicroPython {}.{}.{}".format(*sys.implementation.version[:3]))
print("PSP port", psp.VERSION)
cpu, bus = psp.freq()
print("CPU {} MHz, bus {} MHz".format(cpu, bus))
battery = psp.battery()
if battery is None:
    print("No battery fitted")
else:
    state = "charging" if psp.charging() else "on mains" if psp.on_ac() else "on battery"
    print("Battery {}% ({})".format(battery, state))
if psp.emulator():
    print("Running in the PPSSPP emulator")
