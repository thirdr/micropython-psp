# Dice: press X to roll two dice, START to finish.
import random

import psp

print("X rolls the dice, START finishes.")
while True:
    pressed = psp.pressed()
    if psp.START in pressed:
        break
    if psp.CROSS in pressed:
        a, b = random.randint(1, 6), random.randint(1, 6)
        extra = "  Double!" if a == b else ""
        print("You rolled {} and {}: {}{}".format(a, b, a + b, extra))
    psp.vsync()
