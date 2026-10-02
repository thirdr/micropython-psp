# brush.erase(): holes punched in an overlay, one clear and one tinted.
import math

from picovector import brush, color, image, shape, vec2
from pspdisplay import screen, WIDTH, HEIGHT

# Off-screen panel we punch holes in, then composite over the backdrop.
# Re-cleared every frame.
overlay = image(WIDTH, HEIGHT)


def backdrop(ticks):
    # colourful spinning blobs for the holes to reveal
    for i in range(8):
        a = i / 8 * 2 * math.pi + ticks / 1400
        screen.pen = color.hsv(i * 32, 200, 230)
        x = WIDTH * 0.5 + math.cos(a) * WIDTH * 0.26
        y = HEIGHT * 0.5 + math.sin(a) * HEIGHT * 0.26
        screen.shape(shape.circle(x, y, HEIGHT * 0.19))


def update(ticks):
    screen.antialias = image.X4
    backdrop(ticks)

    # opaque frosted panel that hides the backdrop until we cut through it
    overlay.antialias = image.X4
    overlay.pen = color.rgb(22, 24, 34, 240)
    overlay.clear()

    # brush.erase(): a clean porthole (dst-out), AA edges feather into transparency
    cx = WIDTH * 0.32 + math.sin(ticks / 1100) * WIDTH * 0.06
    cy = HEIGHT * 0.5
    r = HEIGHT * 0.3
    overlay.pen = brush.erase()
    overlay.shape(shape.circle(cx, cy, r))

    # brush.erase(color): a translucent stained-glass window in a single pass
    wx = WIDTH * 0.68
    tint = color.rgb(255, 120, 0, 150)
    overlay.pen = brush.erase(tint)
    overlay.shape(shape.circle(wx, cy, r))

    # composite the punched panel over the backdrop
    screen.blit(overlay, vec2(0, 0))

    screen.pen = color.rgb(255, 255, 255)
    screen.text("erase()", cx - 24, cy + r + 10)
    screen.text("erase(color)", wx - 40, cy + r + 10)
