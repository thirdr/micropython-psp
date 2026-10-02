# stroke() alignments (inner, centre, outer) on closed and open stars.
import math

from picovector import color, image, shape
from pspdisplay import screen, WIDTH, HEIGHT


def update(ticks):
    screen.antialias = image.X4

    # Animate the stroke thickness so the alignment modes are easy to compare.
    thickness = (math.sin(ticks / 500) + 1) * 8 + 2

    spacing = WIDTH / 4

    aligns = [
        ("inner", shape.ALIGN_INNER),
        ("centre", shape.ALIGN_CENTER),
        ("outer", shape.ALIGN_OUTER),
    ]

    # top row strokes each star as a closed loop, bottom row as an open polyline
    # (the open stars don't join the last edge back to the first).
    rows = [
        ("closed", HEIGHT * 0.27, True),
        ("open", HEIGHT * 0.70, False),
    ]

    for row_label, cy, closed in rows:
        for i, (label, align) in enumerate(aligns):
            cx = spacing * (i + 1)

            screen.pen = color.oklch(220, 128, i * 60, 150)
            flags = align | (0 if closed else shape.PATH_OPEN)
            screen.shape(shape.star(cx, cy, 5, 20, 42).stroke(thickness, flags))

            screen.pen = color.rgb(255, 255, 255)
            screen.text(label, cx - 18, cy + 52)

        screen.pen = color.rgb(255, 255, 255)
        screen.text(row_label, 5, cy)
