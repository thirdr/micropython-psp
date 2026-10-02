# brush.lighten() and brush.darken() windows over spinning blobs.
import math

from picovector import brush, color, image, shape
from pspdisplay import screen, WIDTH, HEIGHT


def backdrop(ticks):
    # colourful spinning blobs for the effect to chew on
    for i in range(8):
        a = i / 8 * 2 * math.pi + ticks / 1400
        screen.pen = color.hsv(i * 32, 200, 200)
        x = WIDTH * 0.5 + math.cos(a) * WIDTH * 0.26
        y = HEIGHT * 0.5 + math.sin(a) * HEIGHT * 0.26
        screen.shape(shape.circle(x, y, HEIGHT * 0.19))


def effect_window(cx, cy, r, b, label):
    screen.pen = b
    screen.shape(shape.circle(cx, cy, r))

    # white outline around the region
    screen.pen = color.rgb(255, 255, 255)
    screen.shape(shape.circle(cx, cy, r).stroke(3))
    screen.text(label, cx - 28, cy + r + 12)


def update(ticks):
    screen.antialias = image.X4
    backdrop(ticks)

    amount = int((math.sin(ticks / 700) + 1) * 60) + 20  # 20..140
    r = HEIGHT * 0.23
    cy = HEIGHT * 0.5

    effect_window(WIDTH * 0.3, cy, r, brush.lighten(amount), "lighten %d" % amount)
    effect_window(WIDTH * 0.7, cy, r, brush.darken(amount), "darken %d" % amount)
