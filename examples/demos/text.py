# screen.text() markup: [pen:r,g,b] changes colour mid-text, [sprite:skull]
# draws an image inline, and image.add_glyph() adds new codes ([circle]).
import math

from picovector import color, font, image, rect, shape
from pspdisplay import screen

skull = image.load("assets/skull.png")
compass = font.load("fonts/compass.ppf")


# A renderer is fn(image, params, measure): it returns its advance width when
# measuring, else draws at image.cursor. [pen:r,g,b] is built in.
def sprite_glyph_renderer(target, _parameters, measure):
    if measure:
        return skull.width
    target.blit(skull, target.cursor)
    return None


def circle_glyph_renderer(target, _parameters, measure):
    if measure:
        return 12
    target.shape(shape.circle(target.cursor.x + 6, target.cursor.y + 7, 6))
    return None


image.add_glyph("sprite", sprite_glyph_renderer)
image.add_glyph("circle", circle_glyph_renderer)

message = (
    "[pen:180,150,120]Upon the mast I gleam and grin, A sentinel of bone and sin. "
    "Wind and thunder, night and hull- None fear the sea like a "
    "[pen:230,220,200]pirate skull[pen:180,150,120][sprite:skull]. "
    "[pen:100,200,255]Round [circle] and round [circle] it goes."
)


def update(ticks):
    screen.font = compass
    screen.pen = color.rgb(100, 255, 100, 150)

    width = math.sin(ticks / 500) * 120 + 300
    bounds = rect(10, 10, width, 200)
    screen.text(message, bounds, line_height=1, word_spacing=1.05)

    screen.pen = color.rgb(60, 80, 100, 100)
    screen.line(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y)
    screen.line(bounds.x, bounds.y, bounds.x, bounds.y + bounds.h)
    screen.line(bounds.x, bounds.y + bounds.h, bounds.x + bounds.w, bounds.y + bounds.h)
    screen.line(bounds.x + bounds.w, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h)
