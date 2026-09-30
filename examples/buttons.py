# Buttons: prints each press and release, and what's held with it.
# Press START to finish.
import psp

print("Press some buttons. START finishes.")
while True:
    for b in psp.pressed():
        print("pressed ", b, " held:", " ".join(psp.held()))
        if b == psp.START:
            print("Bye!")
            raise SystemExit
    for b in psp.released():
        print("released", b)
    psp.vsync()
