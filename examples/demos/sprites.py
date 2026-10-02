# screen.blit(): thirty skulls, scaled, fading in and out.
import math
import random

from picovector import image, rect, vec2
from pspdisplay import screen, WIDTH, HEIGHT

skull = image.load("assets/skull.png")


def update(ticks):
    random.seed(0)
    for i in range(30):
        s = (math.sin(ticks / 500) + 2) * 2

        skull.alpha = int((math.sin((ticks + i * 30) / 500) + 1) * 127)

        x = math.sin(i + ticks / 1000) * 90
        y = math.cos(i + ticks / 1000) * 90

        pos = vec2(x + random.randint(-60, WIDTH + 60), y + random.randint(-45, HEIGHT + 45))

        screen.blit(skull, rect(pos.x, pos.y, 32 * s, 24 * s))
