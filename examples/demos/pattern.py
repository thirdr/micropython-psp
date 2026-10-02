# brush.pattern(): a custom 8x8 pattern and two built-in ones.
import math

from picovector import brush, color, shape
from pspdisplay import screen, WIDTH, HEIGHT

CX, CY = WIDTH / 2, HEIGHT / 2


def update(ticks):
    custom_pattern = brush.pattern(color.rgb(255, 100, 100, 100), color.rgb(0, 0, 0, 0), (
        0b00000000,
        0b01111110,
        0b01000010,
        0b01011010,
        0b01011010,
        0b01000010,
        0b01111110,
        0b00000000))
    screen.pen = custom_pattern
    screen.shape(shape.circle(CX + math.cos(ticks / 500) * 68, CY + math.sin(ticks / 1000) * 68, 68))

    built_in_pattern = brush.pattern(color.rgb(100, 255, 100, 100), color.rgb(0, 0, 0, 0), 11)
    screen.pen = built_in_pattern
    screen.shape(shape.circle(CX + math.sin(ticks / 250) * 136, CY + math.cos(ticks / 500) * 136, 68))

    built_in_pattern = brush.pattern(color.rgb(100, 100, 255, 100), color.rgb(0, 0, 0, 0), 8)
    screen.pen = built_in_pattern
    screen.shape(shape.circle(CX + math.cos(ticks / 250) * 136, CY + math.sin(ticks / 500) * 136, 68))
