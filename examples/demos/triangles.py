# screen.triangle(): fifty triangles circling.
import math
import random

from picovector import color, vec2
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    random.seed(0)
    for i in range(50):
        x = math.sin(i + ticks / 100) * 90
        y = math.cos(i + ticks / 100) * 90

        p = vec2(x + random.randint(0, WIDTH), y + random.randint(0, HEIGHT))
        p1 = vec2(p.x + random.randint(-68, 68), p.y + random.randint(-68, 68))
        p2 = vec2(p.x + random.randint(-68, 68), p.y + random.randint(-68, 68))
        p3 = vec2(p.x + random.randint(-68, 68), p.y + random.randint(-68, 68))

        screen.pen = color.rgb(random.randint(0, 255), random.randint(0, 255), random.randint(0, 255))
        screen.triangle(p1, p2, p3)
