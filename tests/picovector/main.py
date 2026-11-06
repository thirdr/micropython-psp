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


# pico3d draws on the GE (pspdisplay.gpu3d), the CPU only for what the GE can't.
from array import array
import pico3d

VIEW = pico3d.mat4.look_at(pico3d.vec3(0, 0, 3), pico3d.vec3(0, 0, 0), pico3d.vec3(0, 1, 0))
VP = pico3d.mat4.perspective(60, 480 / 272, 0.1, 50) * VIEW
QUAD = pico3d.mesh(positions=array("f", (-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0)),
                   indices=array("H", (0, 1, 2, 0, 2, 3)),
                   normals=array("f", (0, 0, 1) * 4),
                   uvs=array("f", (0, 1, 1, 1, 1, 0, 0, 0)),
                   tangents=array("f", (1, 0, 0) * 4))
SUN = pico3d.light(direction=pico3d.vec3(-0.3, -0.4, -1), color=color.rgb(200, 200, 200),
                   ambient=color.rgb(40, 40, 40))
surface = pico3d.surface(screen)


def lit(rgb):
    return pico3d.material(color=color.rgb(*rgb), shading=pico3d.material.GOURAUD)


def quad_frame(ge, material=None, model=None):
    pspdisplay.gpu3d(ge)
    fresh()
    surface.clear_depth()
    surface.render(QUAD, model or pico3d.mat4(), VP, material or lit((255, 160, 40)), SUN)
    update()
    row = [on_screen(x, 136) for x in range(480)]
    return on_screen(240, 136), sum(1 for p in row if p != 0x000028)


def ge_draws():
    before = pspdisplay.gpu3d()
    centre, width = quad_frame(True)
    after = pspdisplay.gpu3d()
    return (centre != 0x000028 and width > 50 and after["draws"] > before["draws"]
            and after["fallbacks"] == before["fallbacks"]), "{:06x} {} {}".format(centre, width, after)
_check("pico3d draws on the GE", ge_draws)


def ge_matches_cpu():
    ge, ge_width = quad_frame(True)
    cpu, cpu_width = quad_frame(False)
    pspdisplay.gpu3d(True)
    return near(ge, cpu, 8) and abs(ge_width - cpu_width) <= 2, "GE {:06x} {} px, CPU {:06x} {} px".format(
        ge, ge_width, cpu, cpu_width)
_check("lit GE quad matches the CPU's", ge_matches_cpu)


def depth_carries():
    fresh()
    surface.clear_depth()
    surface.render(QUAD, pico3d.mat4(), VP, lit((255, 0, 0)), SUN)
    surface.render(QUAD, pico3d.mat4().translate(0, 0, -1), VP, lit((0, 0, 255)), SUN)
    update()
    p = on_screen(240, 136)
    r, g, b = rgb(p)
    return r > b, "{:06x}".format(p)
_check("a nearer render() hides a farther one", depth_carries)


def queued():
    pspdisplay.gpu3d(True, queue=True)
    fresh()
    surface.clear_depth()
    surface.render(QUAD, pico3d.mat4(), VP, lit((255, 160, 40)), SUN)
    at = (136 * 480 + 240) * 4
    waiting = bytes(screen.raw[at:at + 3]) == b"\x00\x00\x28"
    pspdisplay.gpu3d_sync()
    drawn = bytes(screen.raw[at:at + 3]) != b"\x00\x00\x28"
    pspdisplay.gpu3d(queue=False)
    return waiting and drawn, "waiting {} drawn {}".format(waiting, drawn)
_check("queued renders show at gpu3d_sync()", queued)


def near_plane():
    # a floor running under the camera: drawn below the horizon, not above
    slab = pico3d.mesh(positions=array("f", (-4, -0.5, 2, 4, -0.5, 2, 4, -0.5, -20, -4, -0.5, -20)),
                       indices=array("H", (0, 1, 2, 0, 2, 3)))
    eye = pico3d.mat4.perspective(70, 480 / 272, 0.1, 50) * pico3d.mat4.look_at(
        pico3d.vec3(0, 0, 0), pico3d.vec3(0, 0, -1), pico3d.vec3(0, 1, 0))
    fresh()
    surface.clear_depth()
    flat = pico3d.material(color=color.rgb(255, 255, 255), shading=pico3d.material.UNLIT, double_sided=True)
    surface.render(slab, pico3d.mat4(), eye, flat, None)
    update()
    below = sum(1 for x in range(0, 480, 4) if on_screen(x, 260) == 0xFFFFFF)
    above = sum(1 for x in range(0, 480, 4) if on_screen(x, 120) != 0x000028)
    return below == 120 and above == 0, "below {} above {}".format(below, above)
_check("a floor through the camera is clipped", near_plane)


def scene_on_ge():
    pspdisplay.gpu3d(True)
    before = pspdisplay.gpu3d()["draws"]
    fresh()
    surface.clear_depth()
    sc = pico3d.scene(surface, meshes=2)
    sc.add(QUAD, pico3d.mat4().translate(-1.2, 0, 0).scale(0.5), VP, lit((255, 0, 0)), SUN)
    sc.add(QUAD, pico3d.mat4().translate(1.2, 0, 0).scale(0.5), VP, lit((0, 255, 0)), SUN)
    counts = (sc.meshes, sc.vertices, sc.triangles)
    surface.draw(sc)
    update()
    left, right = rgb(on_screen(160, 136)), rgb(on_screen(320, 136))
    return (counts == (2, 8, 4) and left[0] > left[1] and right[1] > right[0]
            and pspdisplay.gpu3d()["draws"] == before + 2), "{} {} {}".format(counts, left, right)
_check("a scene draws on the GE", scene_on_ge)


def normal_map_falls_back():
    bumps = image(8, 8)
    bumps.pen = color.rgb(128, 128, 255)
    bumps.clear()
    m = pico3d.material(color=color.rgb(255, 160, 40), normal_map=bumps)
    before = pspdisplay.gpu3d()["fallbacks"]
    centre, width = quad_frame(True, m)
    return (pspdisplay.gpu3d()["fallbacks"] == before + 1 and centre != 0x000028,
            "{:06x} {}".format(centre, pspdisplay.gpu3d()))
_check("normal maps fall back to the CPU", normal_map_falls_back)


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
