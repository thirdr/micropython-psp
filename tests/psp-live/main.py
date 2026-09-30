# psp module, live: shows button presses and releases, the buttons held, the analog stick
# and the battery, for checking by hand in the PPSSPP window or on a PSP.
#   tools/run-gui.sh --files tests/psp-live build/psp/EBOOT.PBP
# Hold START + SELECT to stop.
import psp

print("psp module live test. Hold START + SELECT to stop.")
print("cpu/bus MHz:", psp.freq(), " emulator:", psp.emulator())
last_held = None
frame = 0
while True:
    held = psp.held()
    if psp.START in held and psp.SELECT in held:
        break
    # Each press and release should be reported once, however long it's held.
    for b in psp.pressed():
        print("pressed:", b)
    for b in psp.released():
        print("released:", b)
    if held != last_held:
        print("held:", held)
        last_held = held
    if frame % 60 == 0:
        x, y = psp.analog()
        psp.set_deadzone(0)
        rx, ry = psp.analog()
        psp.set_deadzone(16)
        print("stick: {:4d} {:4d} (raw {:4d} {:4d})  battery: {}%  ac: {}".format(
            x, y, rx, ry, psp.battery(), psp.on_ac()))
    frame += 1
    psp.vsync()
print("stopped")
