# rect.intersection(): the overlap of two moving rectangles, outlined.
import math

from picovector import color, image, rect, shape
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    screen.antialias = image.OFF

    size = 180
    r1x = math.sin(ticks / 250) * 45 + WIDTH / 2 - size / 2
    r1y = math.cos(ticks / 250) * 45 + HEIGHT / 2 - size / 2
    r1 = rect(r1x, r1y, size, size)

    screen.pen = color.rgb(255, 0, 0, 100)
    screen.rectangle(r1)

    r2x = math.sin(ticks / 500) * 90 + WIDTH / 2 - size / 2
    r2y = math.cos(ticks / 500) * 90 + HEIGHT / 2 - size / 2
    r2 = rect(r2x, r2y, size, size)

    screen.pen = color.rgb(0, 0, 255, 100)
    screen.rectangle(r2)

    r3 = r1.intersection(r2)
    screen.pen = color.rgb(255, 0, 255)
    screen.shape(shape.rectangle(r3.x, r3.y, r3.w, r3.h).stroke(4))
