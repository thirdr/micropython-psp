# Sketch: draw with the d-pad or the analog stick, drawn with picovector.
# Hold X to draw, TRIANGLE changes colour, SQUARE clears, START finishes.
import psp
from picovector import color, font, image, shape, vec2
from pspdisplay import screen, update, WIDTH, HEIGHT

PAPER = color.rgb(245, 240, 225)
WHITE = color.rgb(255, 255, 255)
COLOURS = [color.rgb(*rgb) for rgb in (
    (20, 20, 20), (200, 30, 40), (30, 120, 200), (40, 160, 60), (240, 170, 20))]
MOVES = {psp.LEFT: (-1, 0), psp.RIGHT: (1, 0), psp.UP: (0, -1), psp.DOWN: (0, 1)}

# The drawing is its own image, copied onto the screen each frame; the cursor
# and help text go over the top, so they never smudge the drawing.
paper = image(WIDTH, HEIGHT)
paper.antialias = image.X4
screen.font = font.load("fonts/sins.ppf")
x, y = WIDTH // 2, HEIGHT // 2
colour = 0


def clear_paper():
    paper.pen = PAPER
    paper.clear()


clear_paper()
while True:
    pressed = psp.pressed()
    held = psp.held()
    if psp.START in pressed:
        break
    if psp.TRIANGLE in pressed:
        colour = (colour + 1) % len(COLOURS)
    if psp.SQUARE in pressed:
        clear_paper()

    sx, sy = psp.analog()
    dx, dy = sx // 32, sy // 32
    for button, (mx, my) in MOVES.items():
        if button in held:
            dx, dy = dx + mx * 2, dy + my * 2
    nx = min(max(x + dx, 0), WIDTH - 1)
    ny = min(max(y + dy, 0), HEIGHT - 1)
    if psp.CROSS in held:
        paper.pen = COLOURS[colour]
        paper.shape(shape.line(vec2(x, y), vec2(nx, ny), 4))
        paper.circle(vec2(nx, ny), 2)
    x, y = nx, ny

    screen.blit(paper, vec2(0, 0))
    screen.pen = COLOURS[colour]
    screen.circle(vec2(x, y), 5)
    screen.pen = WHITE
    screen.circle(vec2(x, y), 2)
    screen.pen = COLOURS[0]
    screen.text("X draw  TRIANGLE colour  SQUARE clear  START finishes", vec2(4, HEIGHT - 12))
    update()
