# screen.dither(): grey stars reduced to dithered black and white.
import random

from picovector import color, mat3, shape
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    random.seed(1)

    for i in range(20):
        screen.pen = color.rgb(i * 10, i * 10, i * 10)
        x = random.uniform(-5, 5)
        y = random.uniform(-5, 5)
        s = random.uniform(0.5, 2)
        star = shape.star(x, y, 5, s / 2, s)
        star.transform = mat3().translate(WIDTH / 2, HEIGHT / 2).scale(34).rotate(ticks / 10)
        screen.shape(star)

    screen.dither()
