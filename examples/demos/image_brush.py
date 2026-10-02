# brush.image(): a circle filled with a spinning, zooming skull.
import math

from picovector import brush, image, mat3, shape
from pspdisplay import screen, WIDTH, HEIGHT

skull = image.load("assets/skull.png")


def update(ticks):
    t = mat3().translate(-12, -12).rotate(ticks / 100).translate(WIDTH / 2, HEIGHT / 2).scale(math.sin(ticks / 1000) * 9)
    screen.pen = brush.image(skull, t)
    screen.shape(shape.circle(WIDTH / 2, HEIGHT / 2, 113))
