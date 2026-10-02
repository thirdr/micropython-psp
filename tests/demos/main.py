# Demos check: runs every demo in examples/demos/ headless for a few frames.
# Run with the demos, assets and fonts copied in beside this file (as CI does):
#   mkdir -p build/demos-test && cp tests/demos/main.py build/demos-test/
#   cp -R examples/demos examples/assets examples/fonts build/demos-test/
#   tools/run-ppsspp.sh --files build/demos-test build/psp/EBOOT.PBP 120
#
# Each demo is drawn at a few fixed times, as examples/demos.py draws it, and
# must draw something. Prints "demostest: <name>: ok (<ms> ms)", the slowest
# frame in PPSSPP, or "... FAIL <detail>", then a summary line that CI greps for.
import gc
import os
import sys
import time

import _launcher
from picovector import color, font, image
from pspdisplay import screen, update, WIDTH, HEIGHT

TICKS = (0, 1234, 5678, 20000)
MENU_FONT = font.load("fonts/sins.ppf")

_failures = 0


def drew_something():
    # Every 7th pixel: an 8 px grid can miss all of an 8x8 brush.pattern.
    for y in range(0, HEIGHT, 7):
        for x in range(0, WIDTH, 7):
            if _launcher.screen_pixel(x, y) != 0:
                return True
    return False


def run(name):
    global _failures
    slowest = 0
    try:
        demo = __import__("demos/" + name)
        for ticks in TICKS:
            screen.antialias = image.OFF
            screen.fill_rule = image.EVEN_ODD
            screen.alpha = 255
            screen.pen = color.rgb(0, 0, 0)
            screen.clear()
            screen.pen = color.rgb(255, 255, 255)
            screen.font = MENU_FONT
            start = time.ticks_us()
            demo.update(ticks)
            slowest = max(slowest, time.ticks_diff(time.ticks_us(), start))
            update()
        ok, detail = drew_something(), "nothing drawn"
    except Exception as e:
        sys.print_exception(e)
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    finally:
        sys.modules.pop("demos/" + name, None)
        gc.collect()
    if ok:
        print("demostest: {}: ok ({} ms)".format(name.lower(), slowest // 1000))
    else:
        _failures += 1
        print("demostest: {}: FAIL {}".format(name.lower(), detail))


names = sorted(
    (n[:-3] for n in os.listdir("demos") if n.lower().endswith(".py") and not n.startswith((".", "_"))),
    key=lambda n: n.lower(),
)
for name in names:
    run(name)
ok = _failures == 0 and len(names) > 0
print("demostest: {} demos".format(len(names)))
print("demostest: PASS" if ok else "demostest: FAIL ({} failed)".format(_failures))
