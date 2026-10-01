# picovector check: run with the examples' font copied in beside this file:
#   mkdir -p build/picovector-test
#   cp tests/picovector/main.py build/picovector-test/
#   cp -R examples/fonts build/picovector-test/
#   tools/run-ppsspp.sh --files build/picovector-test build/psp/EBOOT.PBP 60
# Draws with picovector into pspdisplay's screen, shows it, and checks the
# pixels really on screen (_launcher.screen_pixel), then leaves a picture
# for the run's screen.png. Each check prints "pvtest: <name>: ok" or "...
# FAIL <detail>", then a summary line that CI greps for.
import _launcher
import picovector
from picovector import color, font, image, rect, shape, vec2

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("pvtest: {}: ok".format(name))
    else:
        _failures += 1
        print("pvtest: {}: FAIL {}".format(name, detail))


def _check(name, fn):
    try:
        ok, detail = fn()
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    _result(name, ok, detail)


def on_screen(x, y):
    return _launcher.screen_pixel(x, y)


def rgb(c):
    return (c >> 16) & 255, (c >> 8) & 255, c & 255


def near(a, b, tolerance=3):
    return all(abs(p - q) <= tolerance for p, q in zip(rgb(a), rgb(b)))


import pspdisplay
from pspdisplay import screen, update

BACKGROUND = color.rgb(0, 0, 40)


def fresh():
    screen.pen = BACKGROUND
    screen.clear()


_check("module types", lambda: (all(hasattr(picovector, n) for n in (
    "brush", "color", "font", "image", "mat3", "rect", "shape", "vec2")), ""))
_check("screen is 480x272", lambda: (
    (screen.width, screen.height, pspdisplay.WIDTH, pspdisplay.HEIGHT) == (480, 272, 480, 272),
    repr((screen.width, screen.height))))
_check("same screen each time", lambda: (pspdisplay.screen is screen, ""))


def clear_shows():
    screen.pen = color.rgb(10, 200, 30)
    screen.clear()
    update()
    corners = [on_screen(x, y) for x, y in ((0, 0), (479, 0), (0, 271), (479, 271))]
    return all(p == 0x0AC81E for p in corners), " ".join("{:06x}".format(p) for p in corners)
_check("clear fills the whole screen", clear_shows)


def rectangle():
    fresh()
    screen.pen = color.rgb(255, 0, 0)
    screen.rectangle(100, 50, 20, 10)
    update()
    inside = on_screen(100, 50), on_screen(119, 59)
    outside = on_screen(99, 50), on_screen(120, 59), on_screen(100, 60)
    return (all(p == 0xFF0000 for p in inside) and all(p == 0x000028 for p in outside),
            " ".join("{:06x}".format(p) for p in inside + outside))
_check("rectangle lands on its pixels", rectangle)


def antialiased_circle():
    fresh()
    screen.antialias = image.X4
    screen.pen = color.rgb(255, 255, 255)
    screen.shape(shape.circle(240, 136, 40))
    update()
    centre, outside = on_screen(240, 136), on_screen(240, 136 - 45)
    edge = [on_screen(240 + 40 * dx // 57, 136 + 40 * dy // 57) for dx, dy in ((40, 40), (-40, 40), (40, -40), (-40, -40))]
    blended = [p for p in edge if p not in (0xFFFFFF, 0x000028)]
    screen.antialias = image.OFF
    return (centre == 0xFFFFFF and outside == 0x000028 and blended,
            "centre {:06x} outside {:06x} edge {}".format(centre, outside, " ".join("{:06x}".format(p) for p in edge)))
_check("antialiased circle", antialiased_circle)


def alpha_blend():
    fresh()
    screen.pen = color.rgb(255, 0, 0)
    screen.rectangle(0, 0, 40, 40)
    screen.pen = color.rgb(0, 0, 255, 128)
    screen.rectangle(0, 0, 40, 40)
    update()
    p = on_screen(20, 20)
    return near(p, 0x7F0080, 4), "{:06x}".format(p)
_check("translucent pen blends", alpha_blend)


def line():
    fresh()
    screen.pen = color.rgb(255, 255, 0)
    screen.shape(shape.line(vec2(10, 200), vec2(200, 200), 6))
    update()
    return on_screen(100, 200) == 0xFFFF00 and on_screen(100, 210) == 0x000028, "{:06x}".format(on_screen(100, 200))
_check("thick line", line)


def blit():
    fresh()
    sprite = image(16, 16)
    sprite.pen = color.rgb(0, 255, 255)
    sprite.clear()
    screen.blit(sprite, vec2(300, 100))
    update()
    return (on_screen(300, 100) == 0x00FFFF and on_screen(315, 115) == 0x00FFFF
            and on_screen(316, 116) == 0x000028), "{:06x}".format(on_screen(300, 100))
_check("blit an image", blit)


def text():
    fresh()
    screen.font = font.load("fonts/sins.ppf")
    screen.pen = color.rgb(255, 255, 255)
    box = screen.text("Hello, PSP", vec2(10, 10), font_size=2)
    update()
    lit = sum(1 for y in range(10, 10 + int(box.h)) for x in range(10, 10 + int(box.w)) if on_screen(x, y) == 0xFFFFFF)
    return box.w > 20 and box.h > 8 and lit > 50, "box {} lit {}".format(box, lit)
_check("pixel font text", text)


def hsv():
    c = color.hsv(0, 255, 255)
    return (c.r, c.g, c.b) == (255, 0, 0), repr((c.r, c.g, c.b))
_check("hsv colour", hsv)


def release():
    _launcher.release_display()
    kept = on_screen(0, 0) == 0x000028   # the last frame stays showing
    new = pspdisplay.screen
    c = new.get(vec2(20, 20))
    blank = new is not screen and (c.r, c.g, c.b) == (0, 0, 0)
    return kept and blank, "kept {} new {} {}".format(kept, new is not screen, (c.r, c.g, c.b))
_check("after release: last frame stays, new black screen", release)

# A picture for screen.png.
screen = pspdisplay.screen
screen.antialias = image.X4
screen.pen = color.rgb(20, 24, 36)
screen.clear()
for i in range(12):
    screen.pen = color.hsv(i * 21, 220, 255, 200)
    screen.shape(shape.star(vec2(40 + i * 37, 136 + (i % 2) * 40 - 20), 5, 18, 8))
screen.font = font.load("fonts/sins.ppf")
screen.pen = color.rgb(255, 255, 255)
screen.text("picovector v3 on the PSP", vec2(10, 10), font_size=2)
update()

print("pvtest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))
