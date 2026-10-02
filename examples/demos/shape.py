# shape.custom() with two paths, drawn 36 times round a ring.
import math

from picovector import color, image, mat3, shape, vec2
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    screen.antialias = image.X4
    screen.pen = color.rgb(0, 255, 255, 50)
    s = shape.custom(
        [vec2(10, 10), vec2(20, 10), vec2(20, 20), vec2(10, 20)],
        [vec2(15, 15), vec2(25, 15), vec2(25, 25), vec2(15, 25)],
    )

    for i in range(36):
        size = math.sin(ticks / 500 + i) * 7
        angle = ticks / 50 + i * 18
        s.transform = mat3().translate(WIDTH / 2, HEIGHT / 2).scale(size).rotate(angle)
        screen.shape(s)
