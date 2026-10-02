# screen.circle(): a hundred circles circling.
import math
import random

from picovector import color, vec2
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    random.seed(0)

    for i in range(100):
        x = math.sin(i + ticks / 100) * 90
        y = math.cos(i + ticks / 100) * 90

        p = vec2(x + random.randint(0, WIDTH), y + random.randint(0, HEIGHT))
        r = random.randint(11, 45)
        screen.pen = color.rgb(random.randint(0, 255), random.randint(0, 255), random.randint(0, 255))
        screen.circle(p, r)
