# Line joins (miter, round, bevel) and caps (butt, round, square).
import math

from picovector import color, image, shape, vec2
from pspdisplay import screen, WIDTH, HEIGHT


def chevron(cx, cy, s):
    # a sharp down-pointing V; the apex makes the line join obvious
    return [
        vec2(cx - s, cy - s * 0.7),
        vec2(cx, cy + s * 0.7),
        vec2(cx + s, cy - s * 0.7),
    ]


def update(ticks):
    screen.antialias = image.X4
    # stroke ribbons rely on even-odd to leave the band hollow
    screen.fill_rule = image.EVEN_ODD

    col = WIDTH / 4
    s = HEIGHT * 0.12

    # animate the thickness so you can watch the miter grow while round/bevel
    # stay bounded, and the caps extend
    thick = (math.sin(ticks / 600) + 1) * (HEIGHT * 0.045) + 10

    joins = [("miter", shape.JOIN_MITER), ("round", shape.JOIN_ROUND), ("bevel", shape.JOIN_BEVEL)]
    caps = [("butt", shape.CAP_BUTT), ("round", shape.CAP_ROUND), ("square", shape.CAP_SQUARE)]

    jy = HEIGHT * 0.30
    cy = HEIGHT * 0.72

    # top row: same chevron, three line joins (open stroke, centred)
    for i, (name, join) in enumerate(joins):
        cx = col * (i + 1)
        outline = shape.custom(chevron(cx, jy, s)).stroke(thick, shape.ALIGN_CENTER | shape.PATH_OPEN | join)
        screen.pen = color.rgb(120, 200, 255)
        screen.shape(outline)
        screen.pen = color.rgb(255, 255, 255)
        screen.text(name, cx - 16, jy + s + 16)

    # bottom row: same segment, three line caps
    for i, (name, cap) in enumerate(caps):
        cx = col * (i + 1)
        seg = [vec2(cx - s, cy), vec2(cx + s, cy)]
        outline = shape.custom(seg).stroke(thick, shape.ALIGN_CENTER | shape.PATH_OPEN | cap)
        screen.pen = color.rgb(120, 200, 255)
        screen.shape(outline)
        screen.pen = color.rgb(255, 255, 255)
        screen.text(name, cx - 16, cy + 26)

    # restore the default so the setting doesn't leak into other demos
    screen.fill_rule = image.EVEN_ODD
