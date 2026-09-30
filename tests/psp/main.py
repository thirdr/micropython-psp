# psp module check: run with tools/run-ppsspp.sh --files tests/psp <EBOOT>.
# Headless PPSSPP has no buttons and a centred stick, so this checks the
# resting values, types and timing; tests/psp-live checks buttons by hand.
# Each check prints "psptest: <name>: ok" or "... FAIL <detail>", then a
# summary line that CI greps for.
import time

import psp

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("psptest: {}: ok".format(name))
    else:
        _failures += 1
        print("psptest: {}: FAIL {}".format(name, detail))


def _check(name, fn):
    try:
        ok, detail = fn()
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    _result(name, ok, detail)


BUTTONS = (
    "UP", "DOWN", "LEFT", "RIGHT", "CROSS", "CIRCLE",
    "TRIANGLE", "SQUARE", "L", "R", "START", "SELECT",
)


def t_constants():
    values = [getattr(psp, n) for n in BUTTONS]
    return values == list(BUTTONS), repr(values)


def t_held():
    h = psp.held()
    return h == [], repr(h)


def t_pressed():
    # The first call only notes what's held; nothing is pressed headless.
    first = psp.pressed()
    second = psp.pressed()
    return first == [] and second == [] and psp.CROSS not in second, repr((first, second))


def t_released():
    first = psp.released()
    second = psp.released()
    # Calling pressed() in between must not affect released(), and vice versa.
    psp.pressed()
    third = psp.released()
    return first == second == third == [], repr((first, second, third))


def t_analog():
    # PPSSPP's resting stick is centred, so it reads 0 inside the default
    # dead zone, and near 0 raw.
    xy = psp.analog()
    ok = isinstance(xy, tuple) and len(xy) == 2 and xy == (0, 0)
    psp.set_deadzone(0)
    raw = psp.analog()
    ok = ok and all(-127 <= v <= 127 and abs(v) <= 16 for v in raw)
    psp.set_deadzone(16)
    try:
        psp.set_deadzone(127)
        ok = False
    except ValueError:
        pass
    return ok, "default={} raw={}".format(xy, raw)


def t_vsync():
    psp.vsync()
    t0 = time.ticks_us()
    for _ in range(30):
        psp.vsync()
    ms = time.ticks_diff(time.ticks_us(), t0) / 1000
    # 30 frames at 59.94 Hz is about 500 ms.
    return 450 < ms < 550, "{:.1f} ms for 30 frames".format(ms)


def t_battery():
    pct = psp.battery()
    mins = psp.battery_minutes()
    ok = pct is None or 0 <= pct <= 100
    ok = ok and (mins is None or mins >= 0)
    ok = ok and isinstance(psp.charging(), bool) and isinstance(psp.on_ac(), bool)
    return ok, "battery={} minutes={} charging={} on_ac={}".format(
        pct, mins, psp.charging(), psp.on_ac()
    )


def t_freq():
    start = psp.freq()
    ok = start == (333, 166)
    psp.freq(222)
    ok = ok and psp.freq() == (222, 111)
    psp.freq(333)
    ok = ok and psp.freq() == (333, 166)
    try:
        psp.freq(400)
        ok = False
    except ValueError:
        pass
    return ok, "start={}".format(start)


def t_emulator():
    return psp.emulator() is True, ""


for name, fn in (
    ("constants", t_constants),
    ("held", t_held),
    ("pressed", t_pressed),
    ("released", t_released),
    ("analog", t_analog),
    ("vsync", t_vsync),
    ("battery", t_battery),
    ("freq", t_freq),
    ("emulator", t_emulator),
):
    _check(name, fn)

print("psptest: PASS" if _failures == 0 else "psptest: FAIL ({} failed)".format(_failures))
