# brush.blur(): a frosted-glass window moving over spinning blobs.
import math

from picovector import brush, color, image, shape
from pspdisplay import screen, WIDTH, HEIGHT


def backdrop(ticks):
    # colourful spinning blobs for the effect to chew on
    for i in range(8):
        a = i / 8 * 2 * math.pi + ticks / 1400
        screen.pen = color.hsv(i * 32, 200, 230)
        x = WIDTH * 0.5 + math.cos(a) * WIDTH * 0.26
        y = HEIGHT * 0.5 + math.sin(a) * HEIGHT * 0.26
        screen.shape(shape.circle(x, y, HEIGHT * 0.19))


def update(ticks):
    screen.antialias = image.X4
    backdrop(ticks)

    # frosted-glass window; radius pulses 1..10 (box blur is O(r^2) per pixel, so
    # this gets heavy at the top end)
    radius = int((math.sin(ticks / 900) + 1) * 4.5) + 1
    cx = WIDTH * 0.5 + math.sin(ticks / 1100) * WIDTH * 0.18
    cy = HEIGHT * 0.5
    r = HEIGHT * 0.27

    screen.pen = brush.blur(radius)
    screen.shape(shape.circle(cx, cy, r))

    # outline the blurred region
    screen.pen = color.rgb(255, 255, 255)
    screen.shape(shape.circle(cx, cy, r).stroke(3))
    screen.text("blur {}".format(radius), 5, HEIGHT - 12)
