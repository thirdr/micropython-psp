# Clock face: the PSP's clock as a clock with hands, drawn with picovector.
# Press START to finish.
import math
import time

import psp
from picovector import color, font, image, shape, vec2
from pspdisplay import screen, update, WIDTH, HEIGHT

CX, CY, R = WIDTH // 2, HEIGHT // 2, HEIGHT // 2 - 12
BACKGROUND = color.rgb(20, 24, 36)
FACE = color.rgb(235, 235, 225)
MARKS = color.rgb(40, 40, 50)
HANDS = color.rgb(20, 20, 30)
SECONDS = color.rgb(210, 40, 40)
screen.antialias = image.X4
screen.font = font.load("fonts/sins.ppf")


def point(angle, length):
    # angle in turns, 0 at twelve o'clock, clockwise
    a = angle * 2 * math.pi
    return vec2(CX + math.sin(a) * length, CY - math.cos(a) * length)


def hand(angle, length, thickness, pen):
    screen.pen = pen
    screen.shape(shape.line(vec2(CX, CY), point(angle, length), thickness))


last = None
while psp.START not in psp.pressed():
    t = time.localtime()
    if t[5] != last:
        last = t[5]
        hour, minute, second = t[3], t[4], t[5]
        screen.pen = BACKGROUND
        screen.clear()
        screen.pen = FACE
        screen.circle(vec2(CX, CY), R)
        screen.pen = MARKS
        for i in range(60):
            inner = R - (12 if i % 5 == 0 else 5)
            screen.shape(shape.line(point(i / 60, inner), point(i / 60, R - 2), 3 if i % 5 == 0 else 1))
        hand((hour % 12 + minute / 60) / 12, R * 0.5, 6, HANDS)
        hand((minute + second / 60) / 60, R * 0.8, 4, HANDS)
        hand(second / 60, R * 0.85, 2, SECONDS)
        screen.pen = SECONDS
        screen.circle(vec2(CX, CY), 5)
        screen.pen = FACE
        screen.text("{:02d}:{:02d}:{:02d}".format(hour, minute, second), vec2(8, 8), font_size=2)
    update()
