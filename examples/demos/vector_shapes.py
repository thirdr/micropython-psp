# Every built-in shape, filled and stroked, in a spinning grid.
import math

from picovector import color, image, mat3, shape
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    screen.antialias = image.X4

    i = math.sin(ticks / 2000) * 0.2 + 0.5
    f = math.sin(ticks / 1000) * 150
    t = f + (math.sin(ticks / 500) + 1.0) * 50 + 100

    stroke = ((math.sin(ticks / 1000) + 1) * 0.05) + 0.1

    shapes = [
        shape.rectangle(-1, -1, 2, 2),
        shape.rectangle(-1, -1, 2, 2).stroke(stroke),
        shape.circle(0, 0, 1),
        shape.circle(0, 0, 1).stroke(stroke),
        shape.ellipse(0, 0, 1, 0.5),
        shape.ellipse(0, 0, 1, 0.5).stroke(stroke),
        shape.star(0, 0, 5, i, 1),
        shape.star(0, 0, 5, i, 1).stroke(stroke),
        shape.squircle(0, 0, 1),
        shape.squircle(0, 0, 1).stroke(stroke),
        shape.pie(0, 0, 1, f, t),
        shape.pie(0, 0, 1, f, t).stroke(stroke),
        shape.arc(0, 0, i, 1, f, t),
        shape.arc(0, 0, i, 1, f, t).stroke(stroke),
        shape.regular_polygon(0, 0, 1, 3),
        shape.regular_polygon(0, 0, 1, 3).stroke(stroke),
        shape.line(-0.75, -0.75, 0.75, 0.75, 0.5),
        shape.line(-0.75, -0.75, 0.75, 0.75, 0.5).stroke(stroke),
    ]

    # 6 columns of 3 rows fit the PSP's wide screen
    for y in range(3):
        for x in range(6):
            i = y * 6 + x

            scale = ((math.sin((ticks + i * 2000) / 1000) + 1) * 6) + 12

            screen.pen = color.oklch(220, 128, i * 20, 150)

            shapes[i].transform = mat3().translate(x * 76 + 50, y * 80 + 50).rotate(ticks / 100).scale(scale)
            screen.shape(shapes[i])
