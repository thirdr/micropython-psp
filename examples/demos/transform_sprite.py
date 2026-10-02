# brush.image() on a transformed rectangle: a sprite that swings and grows.
import math

from picovector import brush, image, mat3, shape
from pspdisplay import screen, WIDTH, HEIGHT

skull = image.load("assets/skull.png")


def magic_sprite(src, pos, scale=1, angle=0):
    w, h = src.width, src.height
    t = mat3().translate(*pos).scale(scale, scale).rotate(angle).translate(-w / 2, -h)
    screen.pen = brush.image(src)
    rect = shape.rectangle(0, 0, w, h)
    rect.transform = t
    screen.shape(rect)


def update(ticks):
    scale = (math.sin(ticks / 1000) + 1.0) * 4 + 2
    angle = math.cos(ticks / 500) * 100
    magic_sprite(skull, (WIDTH / 2, HEIGHT / 2 + 60), scale, angle)
