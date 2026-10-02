# screen.line(): a hundred lines drifting round.
import math
import random

from picovector import color, vec2
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    random.seed(0)

    for i in range(100):
        x = math.sin(i + ticks / 500) * 90
        y = math.cos(i + ticks / 500) * 90
        p1 = vec2(x + random.randint(-150, WIDTH + 150), y + random.randint(-110, HEIGHT + 110))
        p2 = vec2(x + random.randint(-150, WIDTH + 150), y + random.randint(-110, HEIGHT + 110))
        screen.pen = color.rgb(random.randint(0, 255), random.randint(0, 255), random.randint(0, 255))
        screen.line(p1, p2)
