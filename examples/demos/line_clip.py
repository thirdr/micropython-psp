# algorithm.clip_line(): a hundred long lines clipped to a box.
import math

from picovector import algorithm, color, rect, vec2
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    clip = rect(20, 20, WIDTH - 40, HEIGHT - 40)
    for i in range(100):
        screen.pen = color.oklch(128, 128, (ticks / 100) + i + 150, 100)

        # line clipping to rect
        p1 = vec2(
            int(math.sin(i + ticks / 500) * 300),
            int(math.sin(i + ticks / 400) * 227)
        )

        p2 = vec2(
            int(math.sin(i + ticks / 300) * 300 + WIDTH * 2),
            int(math.sin(i + ticks / 200) * 227 + HEIGHT * 2)
        )

        # clip_line returns true if a line is inside the clipping bounds or has
        # been clipped; lines outside the bounds can't be clipped, so skip them
        if algorithm.clip_line(p1, p2, clip):
            screen.line(p1, p2)

    screen.pen = color.rgb(60, 80, 100, 100)
    screen.line(clip.x, clip.y, clip.x + clip.w, clip.y)
    screen.line(clip.x, clip.y, clip.x, clip.y + clip.h)
    screen.line(clip.x, clip.y + clip.h, clip.x + clip.w, clip.y + clip.h)
    screen.line(clip.x + clip.w, clip.y, clip.x + clip.w, clip.y + clip.h)
