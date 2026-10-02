# screen.blur(): spinning stars under a full-screen blur that comes and goes.
import math
import random

from picovector import color, image, mat3, rect, shape
from pspdisplay import screen, WIDTH, HEIGHT

# screen.blur() takes about 60 ms on the whole 480x272 screen, whatever the
# radius, so the stars are drawn and blurred at half size, then scaled up.
canvas = image(WIDTH // 2, HEIGHT // 2)


def update(ticks):
    random.seed(1)

    canvas.pen = color.rgb(0, 0, 0)
    canvas.clear()
    canvas.pen = color.rgb(255, 255, 255)
    for _ in range(20):
        x = random.uniform(-5, 5)
        y = random.uniform(-5, 5)
        s = random.uniform(0.5, 2)
        star = shape.star(x, y, 5, s / 2, s)
        star.transform = mat3().translate(canvas.width / 2, canvas.height / 2).scale(17).rotate(ticks / 10)
        canvas.shape(star)

    b = math.sin(ticks / 500) * 4 + 4
    canvas.blur(b / 2)
    screen.blit(canvas, rect(0, 0, WIDTH, HEIGHT))

    screen.text("blur radius: {:.2f}".format(b), WIDTH / 2 - 60, HEIGHT - 50)
